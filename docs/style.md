# SHINTAI-OS — Interface Style Guide

_"The used future." A cassette-futurism instrument language for the SHINTAI-OS groundstation (web), the Android companion app, and the RayNeo X3 Pro HUD._

**Version 0.1 · single source of truth for color, type, form, motion, and the app icon.**

---

## 0. Intent & lineage

The whole system reads as **field instrumentation from a future imagined between 1979 and 1993** — hardware you'd find bolted to a dropship, not an app you'd download.

**Draws from:** the _Aliens_ (1986) motion tracker and the Nostromo/Sulaco consoles (MU/TH/UR); the pulse-rifle ammo counter; 80s–early-90s OVA HUDs (_Bubblegum Crisis_, _Patlabor_, _Akira_, _Gunnm/Battle Angel_, early _Ghost in the Shell_ manga); Eurostile-era hardware labelling; monochrome phosphor CRTs.

**Explicitly rejects:** the _Cyberpunk 2077_ look — neon gradients, holographic rainbows, decorative chromatic aberration, RGB glow, glossy game UI, and Japanese text used as wallpaper. If a choice could appear in a 2020s AAA game menu, it's wrong for SHINTAI-OS.

**Priority order — resolve every conflict in this order:**

1. **Functional / legible** — a readout you can parse in a half-second glance under load.
2. **Retrofuturistic** — the cassette-futurism instrument language.
3. **Nostalgic** — the CRT/anime flourishes.

Nostalgia never wins over legibility. On the glasses, it barely gets a vote.

---

## 1. The five principles

1. **Instruments, not apps.** Every surface is a readout, gauge, or console panel. No cards, no pills, no drop-shadowed material. Things look milled, etched, or printed on a CRT.
2. **One color per channel; color is meaning, never decoration.** A surface is dominated by a single emissive color at a time, exactly like a real phosphor display. Green/amber/red carry _state_, never style. This single rule is the biggest thing separating SHINTAI-OS from modern neon cyberpunk.
3. **Black is transparent — design the HUD first.** On the RayNeo waveguide, black pixels emit no light and read as see-through. Design every layout so it survives as bright strokes on pure black; the web and phone then inherit that discipline and look intentionally like a CRT.
4. **Few, large, glanceable values.** 80s instruments showed a handful of big numbers, not dashboards of tiny ones. This serves priority #1 directly.
5. **Physical honesty.** Chamfered panels, segmented bars, scanlines, boot rituals, reticle corners — ornament that evokes real hardware, applied with restraint. One flourish per surface, not five.

---

## 2. Color

Emissive tokens on a near-black void. Every color has a job; do not use them off-label.

| Token          | Hex       | Role / meaning                                                                                    |
| -------------- | --------- | ------------------------------------------------------------------------------------------------- |
| `VOID`         | `#05080A` | Background. On glasses: **`#000000`** (renders transparent).                                      |
| `PANEL`        | `#0C1410` | Raised panel fill / dim surface. **Web + phone only** — never on glasses.                         |
| `GRID`         | `#1C4028` | Hairlines, inactive strokes, graticule, dotted leaders.                                           |
| `PHOSPHOR`     | `#58F07A` | **Primary.** Text, live data, "online / nominal / in-threshold."                                  |
| `PHOSPHOR_DIM` | `#2E7A45` | Secondary green — idle values, range rings, structure.                                            |
| `AMBER`        | `#F2A93B` | **Caution.** Approaching a threshold; degraded but running; optional sensor absent.               |
| `AMBER_DIM`    | `#7A5620` | Idle amber / caution structure.                                                                   |
| `ALERT`        | `#FF4438` | **Alarm.** Breach, fault, critical. Proximity inside `NEAR_MM`, CO₂ over limit, battery critical. |
| `ALERT_DIM`    | `#8A2820` | Idle alarm structure / spent segments.                                                            |
| `BONE`         | `#C9CDBC` | Aged-white **chrome**: static labels, units, panel titles. Non-emissive "printed" bits.           |
| `BONE_DIM`     | `#6B6F62` | Inactive labels, disabled text, footnotes.                                                        |

**Semantic mapping to your telemetry — this is where the palette earns its keep:**

- **Green** = nominal. ToF beyond `FAR_MM`; CO₂ in range; battery healthy; a sensor whose `hasHardware` flag is `true`.
- **Amber** = caution / graceful degradation. Object between `FAR_MM` and `NEAR_MM`; CO₂ climbing; battery low; **a non-fatal sensor init that returned `false`** (the module is simply reported as `— OFFLINE` in amber, which is exactly your degradation model made visible).
- **Red** = alarm. Inside `NEAR_MM`; CO₂ over limit; battery critical; a _required_ subsystem fault.
- **Bone** = the physical chrome the readouts sit on.

**Monochrome discipline.** Never mix all four emissive colors in one dense cluster. A panel is green _or_ amber _or_ red at the top level; the other channels appear only as small state indicators. Rainbow density is the cyberpunk tell you're avoiding.

---

## 3. Typography

Four roles, four faces. All open-license; the paid ideal is noted where one exists.

| Role                    | Face                       | Ideal (paid)            | Use                                                                                                |
| ----------------------- | -------------------------- | ----------------------- | -------------------------------------------------------------------------------------------------- |
| **STRUCTURE / TITLES**  | **Michroma**               | Eurostile Bold Extended | Wordmark, panel titles, module names. Wide, uppercase, tracked.                                    |
| **INTERFACE MONO**      | **IBM Plex Mono**          | —                       | Labels, data rows, terminal text, tables. The workhorse. (Alt: Space Mono.)                        |
| **INSTRUMENT NUMERALS** | **DSEG7 Classic / DSEG14** | —                       | The one big glanceable value — distance, heading, ppm, ammo-counter energy. (Alt: Departure Mono.) |
| **CRT / BOOT FLAVOR**   | **VT323**                  | —                       | Boot logs, scan lines, sign-on ritual only. Never body copy. (Alt: Departure Mono.)                |

**Rules**

- Titles and labels are **UPPERCASE** with `+0.10–0.14em` tracking (Michroma is already wide — don't over-track it).
- Data values are the only thing that gets big. Labels stay small and quiet in `BONE`.
- Label/value rows use a **dotted leader**: `RANGE ···················· 1.42 M`.
- **Glasses caveat (functional priority):** no pixel fonts at small sizes on the waveguide — VT323 and Departure Mono turn to mush. On RayNeo, use **IBM Plex Mono** for text and **DSEG** only at large sizes for the hero value. Pixel/CRT fonts live on web and phone.

Type scale (web baseline, 8px grid): 12 / 14 / 16 / 20 / 28 / 40 / 64. The hero instrument value is 64; everything else is 12–16.

---

## 4. Form, grid & layout

- **8px base grid.** Everything snaps to it; 4px allowed for tick marks and hairlines.
- **Chamfers, not radii.** Panel corners are cut at 45° (2–6px), never rounded. `border-radius: 0` everywhere. Rounded corners read modern/consumer.
- **Reticle corner ticks.** Frame important panels with L-shaped brackets at the corners (Aliens HUD framing) rather than a continuous heavy border.
- **Hairlines.** 1px `GRID` rules; graticule/graduation ticks on gauges. No thick fills.
- **Low density.** Generous negative space. A panel does one job. Resist the urge to fill the frame — that instinct is what makes modern game UI feel cluttered.
- **Alignment.** Left-aligned monospace columns; numbers right-aligned in their field so digits line up like a meter.

---

## 5. Components

Signature component first.

**5.1 Motion-tracker gauge (the signature).** The proximity module's hero. Bottom-anchored origin = the wearer; concentric range rings in `PHOSPHOR_DIM`; a sweep line that rotates; blips that fade with CRT persistence; graduation ticks on the outer ring; the distance as a DSEG value beside it. Green while clear, amber as a contact enters `FAR_MM`, red + blink inside `NEAR_MM`. This _is_ the project's soul (it started as rear-radar) and it's the most direct Aliens quotation — spend the design budget here.

**5.2 Panel.** `VOID`/`PANEL` fill, 1px `GRID` border, chamfered corners, reticle ticks. Title bar: Michroma label in `BONE` + a status LED. That's it.

**5.3 Readout row.** `LABEL` (BONE, small) · dotted leader · `VALUE` (PHOSPHOR) · `UNIT` (BONE_DIM). Fixed columns so a stack of them reads like a ledger.

**5.4 Segmented bar meter.** Discrete blocks (not a smooth fill) — pulse-rifle / LED-VU energy. Fills green → amber → red as a value crosses thresholds. Spent segments drop to the `_DIM` of the current channel. Use for CO₂, battery, particulate, signal.

**5.5 Button.** Chamfered, 1px border, uppercase IBM Plex Mono label. Idle = `GRID` border + `BONE` text. Hover = border → `PHOSPHOR`, faint `PHOSPHOR_DIM` fill. Active = solid `PHOSPHOR` fill, `VOID` text. Momentary and physical; no gradients, no glow.

**5.6 Status LED.** A 6–8px square (not a circle). Solid `PHOSPHOR` = on; hollow = off; `AMBER` = degraded; blinking `ALERT` = fault.

**5.7 Alert banner.** Full-width strip. `AMBER` for caution, `ALERT` for alarm. Stencil icon + short imperative line, blinking at ~1Hz (not glitching). Errors state what happened and what to do, in the interface's voice: `CO₂ 1840 PPM — VENTILATE`.

**5.8 Boot / sensor roll-call (the ritual).** On launch, a scanline sweep, the SHINTAI-OS mark, then a typed roll-call of every module keyed to its `hasHardware` flag:

```
SHINTAI-OS  v0.1   //  INITIALIZING
> BUS  I2C  0x00..0x7F .......... SCAN
> ToF  VL53L4CX  0x29 ........... OK
> IMU  LSM6DSOX+LIS3MDL ......... OK
> GPS  PA1010D ................... OK
> AIR  SCD-40 ................... OK
> NOSE BME688 .................. — OFFLINE
> HUD  RAYNEO  GATT ............. LINK
SHINTAI-OS  //  ONLINE
```

`OK` in green, `— OFFLINE` in amber, `FAULT` in red. This turns your graceful-degradation architecture into the nostalgic centerpiece — the thing that makes it feel like booting a real machine.

---

## 6. Iconography

- **Single-weight line art**, drawn on the 8px grid, as if etched into a CRT or stencilled on a panel. No fills, no gradients, no duotone.
- **Schematic / military-stencil vocabulary:** chamfered frames, reticles, crosshairs, radial sweeps, waveforms.
- Each Tsukiwaza module can carry a stencil glyph (ToF = radial fan; IMU = 3-axis gyro ring; GPS = crosshair-in-globe; thermal = concentric heat rings; nose = molecule dots; RF = broadcast arcs).
- Icons must read at 16px and in a single color — test both before shipping one.

---

## 7. Motion

- **Scanline sweep** on load/refresh. **Phosphor persistence:** a value that changes leaves a brief ghost/decay rather than snapping. **Blink** for alerts (steady ~1Hz, not stutter). **Radar sweep** rotation on the tracker. **Segment cascade** when a bar fills.
- Timing is snappy or slightly _laggy like a real instrument_ — not the smooth material easing of modern apps.
- **Forbidden:** decorative chromatic aberration, RGB-split glitch, holographic shimmer, particle bloom. Those are the exact modern-cyberpunk tells to avoid.
- **Respect reduced-motion** everywhere. **Glasses:** motion causes fatigue and eats the FOV — near-zero animation; the sweep may rotate slowly, alerts may blink, nothing else moves.

---

## 8. Platform adaptation

Same tokens, three postures. The waveguide is the strict master; the others are allowed to relax _toward_ flavor, never away from legibility.

| Concern          | Web groundstation                                             | Android phone                          | RayNeo X3 Pro HUD                                                           |
| ---------------- | ------------------------------------------------------------- | -------------------------------------- | --------------------------------------------------------------------------- |
| **Role**         | Mission control / Sulaco console                              | Field remote                           | Glanceable overlay                                                          |
| **Background**   | `VOID #05080A`                                                | `VOID #05080A`                         | **`#000000` (transparent)**                                                 |
| **Panel fills**  | Yes (`PANEL`)                                                 | Yes, subtle                            | **None** — strokes only                                                     |
| **CRT flourish** | Full: subtle scanlines, faint vignette/curvature, boot ritual | Scanlines subtle/optional              | **None** — no scanlines, no vignette, no curvature                          |
| **Density**      | Multi-panel instrument cluster + log terminal                 | Single hero + scrollable readout stack | 1 hero value + ≤2 supporting lines                                          |
| **Type**         | Full stack incl. VT323/DSEG                                   | Full stack, larger sizes               | IBM Plex Mono + large DSEG only                                             |
| **Color use**    | All channels                                                  | All channels                           | Green default; **amber/red reserved for real alerts** (avoid alarm fatigue) |
| **Touch/target** | Mouse                                                         | ≥48dp chamfered targets                | None (gesture/voice/host app)                                               |
| **Motion**       | Full (respect reduced-motion)                                 | Reduced                                | Near-zero                                                                   |
| **Placement**    | Whole viewport                                                | Whole viewport                         | Central-comfortable FOV; keep critical readouts off the extreme edges       |

**Waveguide rules (non-negotiable):** pure black ground; only bright emissive strokes; no mid-tones (they render as murky ghosts over the world); no fills; thin but not hairline (1px can disappear — use 2px). Assume everything is semi-transparent over reality, so contrast and sparseness are survival, not style.

---

## 9. The app icon (Android + RayNeo X3 Pro)

**Concept — "THE SWEEP."** A bottom-anchored radar fan inside a chamfered instrument bezel, framed by reticle ticks, phosphor-on-black. The origin at 6 o'clock is the wearer (a direct nod to the rear-radar genesis); range rings climb outward; one sweep line and a single blip give it life; a small crosshair marks _you-are-here_. It's the Aliens motion tracker distilled to a mark, and it reads instantly as "this senses what's around you."

**Why this over the alternatives:** a `身` kanji lockup is more overtly nostalgic but muddies at 48px and is opaque to non-JP users; a generic hex/chip glyph says "electronics," not "sensing." The sweep is the project's actual soul and survives a circular launcher mask.

**Construction**

- 512×512 master, 8px grid. Chamfered square bezel, 2px `PHOSPHOR_DIM`. Reticle ticks near each inner corner.
- Radar fan: three range arcs (`PHOSPHOR_DIM`), outer arc + sweep + blip in `PHOSPHOR`. Graduation ticks on the outer arc. Small `PHOSPHOR` crosshair at the bottom-center origin.
- Reads correctly in a single color (weight and position carry it, not hue) so the monochrome variant holds up.

**Deliverable variants**

- **`shintai_icon.svg`** — master, two-tone green on black (the launcher look).
- **`shintai_icon_mono.svg`** — single `currentColor`, transparent ground. Use for Android 13+ **themed/monochrome** icon layer _and_ the RayNeo HUD mark (set color to `PHOSPHOR`).
- **Android adaptive icon:** 108×108dp. **Foreground** = the fan + reticles, kept inside the central **72dp safe zone** (outer 18dp may be masked/parallaxed). **Background** = solid `VOID` (optionally a faint `GRID` graticule). **Monochrome** = the mono SVG.
- **Wordmark lockup:** the mark + `SHINTAI-OS` set in Michroma, tracked, in `BONE` — for splash/boot only, never the launcher tile.

**Do / Don't**

- ✅ One color, chamfered bezel, sparse, high-contrast.
- ❌ No gradient bezel, no glow/bloom, no rounded corners, no rainbow blips, no drop shadow, no kanji in the small tile.

---

## 10. Design tokens (drop-in)

**CSS (web groundstation)**

```css
:root {
    --void: #05080a;
    --panel: #0c1410;
    --grid: #1c4028;
    --phosphor: #58f07a;
    --phosphor-dim: #2e7a45;
    --amber: #f2a93b;
    --amber-dim: #7a5620;
    --alert: #ff4438;
    --alert-dim: #8a2820;
    --bone: #c9cdbc;
    --bone-dim: #6b6f62;

    --font-title: "Michroma", "Eurostile", sans-serif;
    --font-mono: "IBM Plex Mono", "Space Mono", monospace;
    --font-num: "DSEG7 Classic", "Departure Mono", monospace;
    --font-crt: "VT323", "Departure Mono", monospace;

    --chamfer: 4px; /* corner cut */
    --grid-unit: 8px;
}
/* Glasses override */
.hud {
    --void: #000000;
    --panel: transparent;
}
```

**Android — `res/values/colors.xml`** (ARGB)

```xml
<color name="void">#FF05080A</color>
<color name="panel">#FF0C1410</color>
<color name="grid">#FF1C4028</color>
<color name="phosphor">#FF58F07A</color>
<color name="phosphor_dim">#FF2E7A45</color>
<color name="amber">#FFF2A93B</color>
<color name="amber_dim">#FF7A5620</color>
<color name="alert">#FFFF4438</color>
<color name="alert_dim">#FF8A2820</color>
<color name="bone">#FFC9CDBC</color>
<color name="bone_dim">#FF6B6F62</color>
<!-- HUD activity: set windowBackground = @android:color/transparent -->
```

**Compose**

```kotlin
val Void        = Color(0xFF05080A)
val Panel       = Color(0xFF0C1410)
val Grid        = Color(0xFF1C4028)
val Phosphor    = Color(0xFF58F07A)
val PhosphorDim = Color(0xFF2E7A45)
val Amber       = Color(0xFFF2A93B)
val AmberDim    = Color(0xFF7A5620)
val Alert       = Color(0xFFFF4438)
val AlertDim    = Color(0xFF8A2820)
val Bone        = Color(0xFFC9CDBC)
val BoneDim     = Color(0xFF6B6F62)
// HUD: paint on Color.Black; keep the RayNeo activity window transparent.
```

**Font sourcing:** Michroma, IBM Plex Mono, VT323 — Google Fonts. Departure Mono — free (OFL), departuremono.com. DSEG7/DSEG14 — free (OFL), github keshikan/DSEG. Eurostile Bold Extended is the paid ideal for titles if the budget ever appears.

---

_Resolve anything unspecified by the priority order in §0: functional first, retrofuturistic second, nostalgic third — and on the waveguide, when in doubt, remove one thing._
