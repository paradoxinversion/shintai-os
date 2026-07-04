package com.saboteur.shintai.core

/**
 * Kyūkaku (嗅覚) — the sense of smell, DERIVED app-side (specs/zokyo/kyukaku.md).
 *
 * Kyūkaku posts no BLE characteristic — on the body its output is the NeoPixel cue
 * via Aizu. To echo it in the apps WITHOUT a contract change, this mirrors the
 * firmware's `KyukakuBand.h` math over the gas resistance already carried in the
 * ENVIRONMENT characteristic ("1007.2hPa 84200ohm 22.8C 39%RH"). Calibration-free
 * (KY-1): it tracks an adaptive clean-air baseline R0 and works in the ratio
 * r = gas/R0 (never raw ohms). A fast drop of r below a medium reference is a
 * [Smell.Spike] ("the air just changed"); a sustained low r is [Smell.Foul] /
 * [Smell.Taint]. Humidity from the same payload vetoes a moisture-only drop (KY-4).
 *
 * Immutable and folded once per ENVIRONMENT notify (like [HokanPdr]), so both apps
 * agree and the logic is host-testable. Timing is fold-count based, no clock: each
 * notify is one BME reading (~1.5 s), so a Spike is held for [SPIKE_HOLD_FOLDS]
 * notifies then decays to the resolved band — the phone/HUD twin of the on-body
 * violet pulse. Unlike the firmware's ~120 s cold-boot burn-in, the app settle is
 * SHORT ([SETTLE_FOLDS]): it joins a board whose gas plate is already warm, so a
 * handful of readings seed R0.
 */
enum class Smell { Settling, Clean, Taint, Foul, Spike }

/** A short lowercase label for a smell state (UI-free; apps case it as they like). */
val Smell.label: String
    get() = when (this) {
        Smell.Settling -> "warming"
        Smell.Clean -> "clean"
        Smell.Taint -> "taint"
        Smell.Foul -> "foul"
        Smell.Spike -> "spike"
    }

data class KyukakuState(
    val baseline: Float = 0f,          // R0 (ohms), 0 = unseeded
    val rRef: Float = 1f,              // medium EMA of the ratio (spike reference)
    val lastHum: Float = 0f,           // %RH at the previous reading (humidity veto)
    val ratio: Float = 1f,             // last ratio r (for display)
    val band: Int = BAND_CLEAN,        // hysteretic ambient band
    val count: Int = 0,                // ENVIRONMENT notifies folded (settle counter)
    val spikeHold: Int = 0,            // notifies remaining to hold a Spike
) {
    val seeded: Boolean get() = count >= SETTLE_FOLDS

    /** The resolved smell state the apps render (Spike overrides the ambient band). */
    val smell: Smell
        get() = when {
            !seeded -> Smell.Settling
            spikeHold > 0 -> Smell.Spike
            band == BAND_FOUL -> Smell.Foul
            band == BAND_TAINT -> Smell.Taint
            else -> Smell.Clean
        }

    /**
     * Fold one ENVIRONMENT payload ("…84200ohm…22.8C 39%RH…") into the next state.
     * A payload with no `…ohm` field (BME688 absent) leaves the state unchanged.
     * The Spike is measured against the PRE-update rRef so a sudden drop this reading
     * is caught, then rRef is advanced — the same order as the firmware.
     */
    fun fold(environment: String): KyukakuState {
        val gas = OHM.find(environment)?.groupValues?.get(1)?.toFloatOrNull() ?: return this
        if (gas <= 0f) return this
        val hum = RH.find(environment)?.groupValues?.get(1)?.toFloatOrNull() ?: lastHum
        if (count == 0) {   // first reading — seed R0, don't arm
            return copy(baseline = gas, rRef = 1f, lastHum = hum, ratio = 1f,
                band = BAND_CLEAN, count = 1, spikeHold = 0)
        }
        val armed = count >= SETTLE_FOLDS
        val base = baselineStep(baseline, gas, armed)
        val r = gas / base
        val fresh = armed && (rRef - r) >= SPIKE_DROP && (hum - lastHum) < HUM_VETO
        return copy(
            baseline = base,
            rRef = rRef + RREF_ALPHA * (r - rRef),
            lastHum = hum,
            ratio = r,
            band = step(r, band),
            count = if (count < Int.MAX_VALUE - 1) count + 1 else count,
            spikeHold = if (fresh) SPIKE_HOLD_FOLDS else (spikeHold - 1).coerceAtLeast(0),
        )
    }

    companion object {
        const val BAND_CLEAN = 0
        const val BAND_TAINT = 1
        const val BAND_FOUL = 2

        // Thresholds mirror firmware/shintai-os/KyukakuBand.h (KY-6).
        const val TAINT_R = 0.60f
        const val FOUL_R = 0.35f
        const val HYST_R = 0.05f
        const val SPIKE_DROP = 0.25f
        const val HUM_VETO = 3.0f
        const val SEED_ALPHA = 0.10f
        const val BASE_UP = 0.02f
        const val BASE_DOWN = 0.0006f
        const val RREF_ALPHA = 0.25f

        /** App-side settle is short: the board's plate is already warm when we
         *  connect, so a few readings seed R0 (firmware burns in ~120 s from cold). */
        const val SETTLE_FOLDS = 8

        /** A Spike holds this many notifies (~1.5 s each ≈ the firmware's ~3 s hold). */
        const val SPIKE_HOLD_FOLDS = 2

        private val OHM = Regex("""(\d+(?:\.\d+)?)ohm""")
        private val RH = Regex("""(\d+(?:\.\d+)?)%RH""")

        /** Asymmetric-EMA baseline: fast while settling; once armed, rise moderate /
         *  decay very slow so a smell can't drag R0 down after itself (KY-1). */
        private fun baselineStep(baseline: Float, gas: Float, armed: Boolean): Float =
            if (!armed) baseline + SEED_ALPHA * (gas - baseline)
            else baseline + (if (gas > baseline) BASE_UP else BASE_DOWN) * (gas - baseline)

        /** Instantaneous band from r with no hysteresis (lower r = worse air). */
        fun rawBand(r: Float): Int = when {
            r < FOUL_R -> BAND_FOUL
            r < TAINT_R -> BAND_TAINT
            else -> BAND_CLEAN
        }

        /** Hysteretic band walk — mirror of KyukakuBand.h `kyukakuStep`, inverted for
         *  a falling signal: worsen only past edge-HYST, recover only past edge+HYST. */
        fun step(r: Float, prev: Int): Int {
            if (prev < BAND_CLEAN || prev > BAND_FOUL) return rawBand(r)
            val edge = floatArrayOf(TAINT_R, FOUL_R)
            var b = prev
            while (b < BAND_FOUL && r < edge[b] - HYST_R) b++
            while (b > BAND_CLEAN && r > edge[b - 1] + HYST_R) b--
            return b
        }
    }
}
