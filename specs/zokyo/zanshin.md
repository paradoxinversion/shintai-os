# Zanshin (残心) — spec

*Lingering awareness: the rear grown from two point-beams to a depth field — sensing not just "something's behind you" but its shape and where.*

**Status:** built (2026-07-11) · **Zōkyō:** Zanshin (supersedes [Kōei](./koei.md)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) — **new binary characteristic** (CSV unchanged) · **Shares:** [Kehai](../../REGISTRY.md#kehai-気配--sensed-presence) (feeds the reflex), the PCA9546 mux · **Date:** 2026-07-11

> Built on `research-development-jūroku`. Benched on the USB power bank (the VL53L5CX's firmware upload + 8×8 draw exceed the small LiPo). All solderless — Qwiic.

## What this is

Zanshin — 残心, the budo term for *sustained awareness of the space around and behind you* —
replaces Kōei's two rear [VL53L4CX](../../REGISTRY.md#sensors) point arcs with **one
[VL53L5CX](../../REGISTRY.md#sensors) 8×8 multizone ToF**: the rear stops being two beams and
becomes a **64-zone depth field**. Where Kōei distinguished "on your left" from "on your
right", Zanshin resolves a real rear **azimuth + range map** — a swept contact, not two blips.

This is exactly the upgrade Kōei's own [Forward path](./koei.md#forward-path) called for
("*a multizone ToF (VL53L5CX) → a real rear azimuth*"), on the infrastructure Kōei built: the
field rides the **PCA9546 mux on ch0** (the VL53L5CX is at `0x29` like the old arcs), same
select-before-touch discipline, no new address problem.

The clever part is the **seam continuity**: the field's left-half nearest and right-half
nearest zones derive `distance_l_mm` / `distance_r_mm`, and the nearest zone overall drives
`alert` and the on-body Kehai reflex — so **the CSV, the `Distance`/`Alert` string
characteristics, and the reflex are all unchanged from Kōei**. Only the *source* moved from
two point beams to one field. The field's full richness is then streamed live over a **new
binary characteristic** — the rear analogue of Metsuke's thermal grid.

## Why

- **The most useful rear upgrade left.** Kōei answered "left or right"; a 63° 8×8 field answers
  "*where* behind, and how near, across the whole arc" — a rear depth panel you can read at a
  glance, and the substrate for future azimuth-steered haptics.
- **One sensor for two.** A single VL53L5CX (63° FoV) covers the spread that needed two angled
  VL53L4CX — fewer parts, one mux channel, and it frees ch1.
- **Zero-churn downstream.** By deriving L/R + alert from the field, every existing consumer
  (CSV, Distance/Alert chars, Kehai reflex, the apps' L/R readouts) keeps working untouched;
  the new capability is purely additive (the depth grid).

## Goals

1. Drive one VL53L5CX (8×8 @ 15 Hz) on mux ch0, presence-gated / non-fatal.
2. Derive `distance_l_mm` / `distance_r_mm` (left/right-half nearest) + `alert`/Kehai (nearest
   zone) from the field — CSV + string chars + reflex **unchanged**.
3. Stream the 64-zone field over a **new binary characteristic** (128 B, one notification) →
   a rear depth panel in both apps.
4. No CSV schema change; the mux stays as reusable infra.

## Non-goals

- **No new CSV columns.** The field is BLE-live-only (like Metsuke's thermal grid); the logged
  rear representation stays `distance_l/r_mm` + `alert`.
- **No motion indicator / multi-target.** v1 uses single-target per zone (`NB_TARGET = 1`).
- **No azimuth-steered haptics yet** — that's the [Forward path](#forward-path) (needs Aizu's
  DRV2605L sink).

## Parts (Tsukiwaza)

- **VL53L5CX** — 8×8 (64-zone) multizone ToF, ~63° FoV, ~4 m, 15 Hz @ 8×8 (I²C `0x29`, behind
  the [PCA9546 mux](../../REGISTRY.md#host--infrastructure) on ch0). Replaces the two VL53L4CX
  arcs. Uploads an ~85 KB firmware blob at init (slow boot; presence-gated).
- **PCA9546A mux (0x70)** — retained from Kōei; ch0 = the field, ch1–3 free for future arcs.

## Behaviour — read, derive, stream

`serviceReflex` (the sole rear-ToF read site) selects mux ch0, pulls a fresh 8×8 frame when
`check_data_ready`, and `ZanshinGrid.h` (pure, host-tested) does the rest:

- **`zanshinDeriveLR`** — nearest valid zone (`target_status` 5 or 9) in the left columns
  (0–3) → `distance_l_mm`, right columns (4–7) → `distance_r_mm`. Which physical side is which
  is a mount detail, validated on-wrist. These feed the existing `alert` + Kehai reflex via the
  unchanged `nearerMm(cachedMmL, cachedMmR)`.
- **`zanshinPackGrid`** — 64 zones → 128 bytes (`uint16` LE mm, row-major; **0 = no valid
  target**), notified over the new characteristic while a central is subscribed.

## Contract impact

- **New binary characteristic** — **Rear Depth Grid** (`abcd5c88…`), 128 B = 64 × `uint16` LE
  mm. BLE-live-only, **not logged**. Documented in [CONTRACT.md](../../CONTRACT.md#rear-depth-grid-binary).
- **CSV / string chars unchanged** — `distance_l/r_mm` + `alert` keep their shape; only their
  on-device *source* changed (field halves instead of two sensors). Mirror sites (firmware
  `#define`, `:core` `ShintaiGatt`) moved in lock-step; `tools/check-contract.py` green.
- **Bunshin** — the Rear Depth Grid joins the `distance_*`/`alert` row of the authority table
  (**aft → fwd**: the rear field lives on the pack).

## Firmware integration

- `firmware/shintai-os/ZanshinGrid.h` — pure L/R derivation + grid pack (host-tested in
  `tools/zanshin-grid-test.cpp`).
- `shintai-os.ino` — one `VL53L5CX zanshinTof` (replacing `sensorL`/`sensorR`); `initZanshin`
  (8×8 @ 15 Hz on ch0); `serviceZanshinField` (sole read); `serviceReflex` derives L/R + posts
  the Kehai cue (unchanged) + notifies the depth grid; new `zanshinGridChar` + CCCD.
- `sketch.yaml` — `STM32duino VL53L4CX` → `STM32duino VL53L5CX`.

## Consumer / app integration

- `:core` — `ShintaiGatt.REAR_DEPTH_GRID` (+ `BINARY` set), a `DepthGrid` parse + shared
  near→far colour ramp + `argb`, folded into `ShintaiReadings.rearDepthGrid`; `Merge.kt`
  carries it on the **Distance** channel (aft → fwd).
- **Both apps** subscribe to it and render a **rear depth panel** (`RearDepthPanel` on Glass,
  `DepthField` on Operator) from the shared `:core` palette — the rear complement to Metsuke's
  forward thermal panel. The L/R numeric readouts (from the derived `distance_l/r_mm`) are
  unchanged.

## Acceptance criteria

1. With the field present, `distance_l_mm` / `distance_r_mm` track objects on the correct side
   (verify with `'T'`), `alert` fires within `NEAR_MM`, and the Kehai reflex behaves as under Kōei.
2. Both apps render a live 8×8 rear depth panel while subscribed; absent → no empty box.
3. Firmware compiles (`firmware/verify.sh`); host + Android (`android/build.sh detekt lint`) +
   `tools/check-contract.py` all green.

## Decisions

- **D-1 — Replace the arcs, don't add.** One 63° field subsumes the two narrow beams; fewer
  parts, and it realizes Kōei's forward path rather than layering on it.
- **D-2 — Derive L/R + alert from the field.** Keeps the whole Kōei downstream (CSV, chars,
  reflex) unchanged — the field is a drop-in source, plus a new grid.
- **D-3 — New binary char, no new CSV column.** The depth field is a live image (like Metsuke's
  thermal grid), not a logged scalar; the logged rear stays `distance_l/r_mm`.
- **D-4 — Keep the mux.** The field is a single sensor and could sit direct on the bus, but the
  PCA9546 is Kōei's reusable infra: ch1–3 stay open for future arcs with no address rework.
- **D-5 — 8×8 @ 15 Hz.** The field's value is resolution; 15 Hz still exceeds the ~8 Hz reflex,
  so one setting serves both the panel and the reflex.

## Forward path

- **Azimuth-steered haptics.** Once Aizu's DRV2605L sink lands, the field's nearest-zone
  *column* drives left/centre/right motors — you feel *where* behind, not just that.
- **Motion / approach.** The VL53L5CX motion indicator could flag *approaching* zones (closing
  range) distinctly from static ones.
- **Second field on ch1.** A side or upward field drops onto a free mux channel with the same
  discipline — the spread Kōei's mux always anticipated.
