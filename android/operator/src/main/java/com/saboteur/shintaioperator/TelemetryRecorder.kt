package com.saboteur.shintaioperator

import android.util.Log
import com.saboteur.shintai.core.ShintaiReadings
import java.io.BufferedWriter
import java.io.File
import java.io.FileWriter
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Records the live BLE stream to a CSV on the phone — the Operator's mirror of
 * groundstation/shintai-logger.py, and a job the flaky glasses can't be trusted
 * to hold.
 *
 * IMPORTANT: this is a capture of the **BLE channels**, NOT the firmware CSV
 * schema in CONTRACT.md. BLE is a lossy per-sensor summary (accel arrives as one
 * "X.. Y.. Z.." string, GPS as "lat,lon alt speed"), so a faithful column-exact
 * firmware CSV can't be reconstructed here. We therefore write our own honest
 * header — a phone wall-clock plus each channel's raw payload — and quote every
 * field (the GPS payload itself contains a comma).
 */
class TelemetryRecorder(private val dir: File) {

    private var writer: BufferedWriter? = null
    var rows: Int = 0
        private set
    var fileName: String? = null
        private set

    val active: Boolean get() = writer != null

    /** Open a fresh capture file. Returns its name, or null on failure. */
    fun start(): String? {
        if (active) return fileName
        if (!dir.exists()) dir.mkdirs()
        val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val file = File(dir, "shintai_ble_$stamp.csv")
        return try {
            val w = BufferedWriter(FileWriter(file))
            w.write(HEADER)
            w.newLine()
            w.flush()
            writer = w
            rows = 0
            fileName = file.name
            Log.i(TAG, "recording -> ${file.absolutePath}")
            file.name
        } catch (e: IOException) {
            Log.w(TAG, "could not open recording file: $e")
            null
        }
    }

    /** Append one snapshot row, stamped with the phone wall clock. */
    fun writeRow(r: ShintaiReadings, wallMs: Long) {
        val w = writer ?: return
        val cells = listOf(
            wallMs.toString(),
            r.distanceLMm?.toString().orEmpty(), r.distanceRMm?.toString().orEmpty(),
            if (r.alertActive) "1" else "0", r.heading, r.accel,
            r.gps, r.climate, r.thermal, r.environment,
        )
        try {
            w.write(cells.joinToString(",") { quote(it) })
            w.newLine()
            rows++
        } catch (e: IOException) {
            Log.w(TAG, "row write failed: $e")
        }
    }

    /** Flush + close. Returns the finished file name (for the log), or null. */
    fun stop(): String? {
        val name = fileName
        try {
            writer?.flush()
            writer?.close()
        } catch (e: IOException) {
            Log.w(TAG, "close failed: $e")
        }
        writer = null
        return name
    }

    /** CSV-quote a cell (fields like GPS contain commas; values may contain quotes). */
    private fun quote(s: String): String = "\"" + s.replace("\"", "\"\"") + "\""

    companion object {
        private const val TAG = "ShintaiRec"
        const val HEADER =
            "wall_ms,distance_l,distance_r,alert,heading,accel,gps,climate,thermal,environment"
    }
}
