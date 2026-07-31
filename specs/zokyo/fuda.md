# Fuda (札) — spec

*The tag you tap has a name — read it, log it, show it.*

**Status:** active (2026-07-07) — **validated on hardware**: PN532 @ `0x24`, ISO14443A UID read end-to-end (serial panel + CSV `nfc_uid`/`nfc_type` + BLE). Plumbing complete across all seam sites. · **Zōkyō:** Fuda (sibling to [Enmu](./enmu.md), the other new-BOM sense) · **Seam:** [CONTRACT.md](../../CONTRACT.md) (**both halves** — 2 CSV columns + 1 BLE characteristic) · **Shares:** none in v1 · **Date:** 2026-07-05

## What this is

Fuda — "tag / card / talisman" (and *ofuda*, the paper talisman enshrined at a Shinto shrine —
apt for a platform called *Shintai*) — is an **NFC / RFID identity sense**. It adds the
[PN532](../../REGISTRY.md#sensors) (Adafruit #364) — a 13.56 MHz NFC controller — to the sensor
chain and publishes the **UID of any ISO14443A tag** held in its RF field, as two new CSV columns
(`nfc_uid` / `nfc_type`) and a new **event-driven Fuda** BLE characteristic. The consumers render it:
the phone console shows the full `UID + type`, the glasses surface the **UID alone**.

It is the platform's **first identity sense**. Every prior Zōkyō answers *how much* / *how far* /
*how hot* — a physical scalar off a physical field. Fuda answers a different kind of question:
***who / what***. A tag is not a measurement; it is a name the wearer has attached to a place, an
object, a person, or a checkpoint. Tap a tagged toolbox, doorway, or waypoint and the platform reads
its identity, logs *when* you were there, and heads it up on the glass. It turns the wearable from a
sensor of the ambient world into a reader of the **marked** world.

## Why (the case for adding a chip)

Fuda is a **new-BOM sensor**, landing alongside [Enmu](./enmu.md); like Enmu it earns the exception
to the platform's thrift rule by reaching a signal no part already on the chain can. (This spec was
written calling Fuda "the second new-BOM sensor, after Enmu" — true of the pair in isolation, but
both land after Enrai's AS3935 and Zanshin's VL53L5CX, so the ordinal no longer holds. The argument
below is unaffected.)

- **Identity is unreachable from the existing BOM.** Nothing on the chain reads a tag. The senses so
  far are all *analog fields* — distance, heat, gas, particulate, motion, pressure. A tag UID is a
  *discrete digital identity*; it requires a part that speaks a 13.56 MHz NFC protocol
  ([FU-1](#decisions)). There is no thrifty path to a UID.
- **It adds a new *category* of sense, not a richer analog one.** Enmu completed the *air* trilogy
  (CO₂ / VOC / particulate). Fuda opens a category the platform never had: **event / identity**. The
  data is not a continuous stream but a **discrete read** — a tag is present for a moment, then gone.
  That shape (event-driven, held-then-blank) is new to the contract and is the template for future
  discrete senses.
- **It exercises the seam cleanly.** Two end-appended string CSV columns and one new string
  characteristic — no existing field touched, no binary payload. The one *new* seam property is that
  the characteristic is **event-driven**: it notifies only on a read, not every cycle
  ([FU-3](#decisions)).
- **Plain bus, no mux.** The PN532 answers at 7-bit `0x24`, a free address — it drops onto the STEMMA
  QT chain and is read like the BME688. (Its DIP switch selects the *interface*, not the address; set
  to **I²C**.)

## Goals

1. Read the UID of an ISO14443A tag in the PN532's field each telemetry cycle and publish it as
   `nfc_uid` (uppercase hex, e.g. `04A1B2C3`) with its RF family `nfc_type` (`ISO14443A`) — CSV + flash log.
2. Notify an **event-driven Fuda** BLE characteristic (`UID:<hex> <type>`) **only when a tag is read**
   — the live twin of those columns, fired as the scan happens.
3. Render it on **both** consumers: the Operator console shows the full `UID + type`; the Glass HUD
   surfaces the **UID alone** (the lean-surface rule — the identity is the heads-up, the RF family is
   console detail).
4. **Hold** a read ~5 s so a brief tap is logged across a couple of telemetry cycles, then blank —
   the columns/BLE reflect "no tag" once the hold lapses ([FU-2](#decisions)).
5. Degrade cleanly: absent PN532 → blank columns, no notify, no crash; old logs without the columns
   still parse (append-at-end).
6. Add the sensor to the ground-station capture panel and the Bunshin authority table — the base-side mirror.

## Non-goals

- **No writing / no NDEF (v1).** Fuda only **reads** a UID. It does not write tags, read NDEF records,
  or parse tag *content* — the UID is the identity, and that is the whole sense in v1.
- **No on-body cue (v1).** Fuda does **not** post to [Aizu](../platform/aizu.md) — no NeoPixel flash on
  a read. A tap→confirm cue is a natural next step but is deferred.
- **No tag→meaning mapping on-device.** The firmware publishes the raw UID; naming a UID ("toolbox 3",
  "north door") is a consumer-side concern, not a contract field.
- **No card auth / secure element.** Fuda does not do Mifare key auth, DESFire, or any secured sector
  read. It takes the UID the anticollision phase already exposes — no keys, no secrets.
- **No non-A families (v1).** ISO14443**A** (Mifare / NTAG / most tags) only; FeliCa / ISO14443B /
  ISO15693 are out of scope for v1.
- **No continuous stream.** Unlike the analog senses, Fuda is **event-driven**: no tag → no notify.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [PN532](../../REGISTRY.md#sensors) | NFC/RFID reader — ISO14443A tag UID over I²C `0x24` | **new BOM** (Adafruit #364) |

## Decisions

- **FU-1 — a chip, not a reuse.** Identity has no analog proxy on the chain; it needs an NFC front-end.
  Fuda accepts the new-BOM cost for the same reason Enmu did — the sense is otherwise unreachable.
- **FU-2 — read, hold ~5 s, then blank.** A tag is present only while physically tapped. The read runs
  once per telemetry cycle (a short 50 ms RF window, never in the fast Kehai reflex loop, so a scan
  never stalls proximity). A successful read latches the UID and **holds** it ~5 s so a brief tap is
  captured across a couple of log rows; once the hold lapses with no fresh tag the columns blank. This
  mirrors [Kyūkaku](./kyukaku.md)'s spike-hold idiom.
- **FU-3 — the characteristic is event-driven.** Every other string characteristic notifies each
  cycle its sensor has data. Fuda notifies **only on a fresh read** — a scan is an event, not a
  stream — so a central sees a tap as it happens and nothing between taps. (The CSV column, by
  contrast, uses the ~5 s hold so the *log* still records the tap across cycles.)
- **FU-4 — 7-bit `0x24`, not `0x12`.** The PN532's I²C address is 7-bit `0x24` (= 8-bit write `0x48`
  / read `0x49`). `0x12` is a double-shift artifact — halving an already-7-bit address again — and is
  also the [Enmu](./enmu.md) PMSA003I's address; the two must not be conflated. Fuda targets `0x24`.
- **FU-5 — string columns, appended.** `nfc_uid` / `nfc_type` are **strings** (like `cardinal` and
  `board`), appended at the end of the schema. The numeric-row framing (`line[0].isdigit()`, which
  only tests `timestamp_ms`) is untouched, and pre-Fuda logs still parse.
- **FU-6 — Bunshin precedence fwd → aft.** When both pods supply a tag, the head-side pod wins — you
  scan the tag you're facing. (Only one pod carries the PN532 in practice; this is the tiebreak default.)

## Forward path

- **On-body confirm cue.** A short Aizu flash on a successful read — the tap's haptic-less "got it".
- **Tag → name mapping (consumer-side).** A phone-side table turning a UID into a label
  ("north door", "kit 3"), shown on the glass instead of the raw hex.
- **NDEF / write.** Read a tag's stored record, or write a waypoint onto a blank tag.
- **Access / presence log.** With named tags, the logged UID timeline becomes a checkpoint / presence
  trail — where the wearer was, and when — alongside Hokan's dead-reckoned path.
