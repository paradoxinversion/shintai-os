# Bunshin (分身) — spec

*Divided body: a second Shintai-OS host clipped elsewhere on you — two bodies sensing in parallel, folded back into one perception at the glasses and the phone.*

**Status:** built (2026-07-05) — code-complete across all three modules + contract, and **validated end-to-end on hardware**: both boards flashed + roled (fwd `68:EE:8F:6E:77:BD`, aft `10:20:BA:0C:D3:51`, MACs read via the `'M'` command), and **both consumers hold two simultaneous GATT connections and render both pods merged** — Operator scans/links both (with the "Link Another Unit" flow), Glass connects both hardcoded MACs. · **Zōkyō:** Bunshin (sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) — **changes it** (adds a multi-producer model + a CSV column) · **Shares:** every existing channel (federates them) · **Date:** 2026-07-04

> Needs a **second QT Py ESP32-S3**. All solderless — each pod is its own Qwiic chain; the two never share a bus, only the contract. Benched on two USB banks (or two LiPos) — the pods are physically independent.

## What this is

Bunshin — "divided body," the ninjutsu body-double — is the first Zōkyō that runs on
**two Shintai-OS hosts at once**. Today one board reads every sensor it can see and
publishes them three ways; Bunshin splits the sensor set across **two identical boards**
by *what's physically plugged into each* — a **forward pod** (`fwd`, head-side) and an
**aft pod** (`aft`, pack-side) — and **reunites their streams at the consumer**, so the
glasses and the phone still see one unified body of readings, not two.

It leans entirely on a property the contract already has: output is **presence-driven** —
a channel goes blank / stops notifying when its sensor is absent
([CONTRACT.md](../../CONTRACT.md) climate-slot + "only notifies when its sensor is present"
rules). Flash the **same binary** to both boards; plug the rear ToF + torso IMU into `aft`
and the thermal camera + head IMU into `fwd`, and each board *already* emits only the
channels it owns. The split is **physical, not coded** — the firmware barely changes. The
new work lives where two producers meet one consumer: **identity** (telling the pods apart),
**the merge** (folding two per-pod readings into one, with a precedence rule when both
supply the same channel), and a **live precedence control** the wearer drives from the
Operator app.

It earns its firsts (see [Firsts](#firsts)):

1. **First multi-host Zōkyō.** Every prior Tsukiwaza ran on one board. Bunshin is the
   first to span **two Shintai-OS hosts**, federated at the consumer — the contract's
   "*the* board produces" becomes "boards produce."
2. **First consumer-side merge / authority policy.** Introduces the **authority table**
   and a pure `:core` reducer — the first time a channel's *source* is arbitrated at the
   consumer, not decided by the lone producer.
3. **First runtime-configurable contract behaviour.** Operator's **precedence override**
   is the first user-facing control that changes *how the contract data is interpreted*,
   live and per-channel.
4. **First board-identity scheme.** Role-suffixed device names (`ShintaiOS-fwd` /
   `ShintaiOS-aft`) — the first time two boards must be told apart.

## Why (the thrifty case)

- **A second $12 board doubles the sensor budget without a bigger board.** The QT Py's
  I²C chain, power draw, and firmware loop have a practical ceiling; a second host is
  cheaper and simpler than a beefier single board (N4R2 + a longer chain), and it puts
  sensors where they belong on the body (thermal forward, torso pedometer at the waist).
- **Nothing about the seam is thrown away.** Same firmware binary, same GATT service,
  same CSV columns, same `:core` parsers. The producer side is copy-the-board; the only
  net-new code is one reducer and one settings screen.
- **The merge is reusable.** A third pod (a boot-side ToF, a wrist IMU) drops onto the
  same authority table and reducer with no new machinery — the pattern scales past two.
- **Redundancy for free.** Because a channel can live on either pod, the wearer can put a
  spare IMU/GPS on both and let the merge prefer whichever has better data — resilience
  the single-board build never had.

## Goals

1. **Same binary, two roles.** One `.ino` build flashed to both boards; each reads its
   role (`fwd`/`aft`) from NVS at boot and advertises as `ShintaiOS-<role>`. Sensor logic
   unchanged — presence-driven output does the split.
2. **Federate every channel at the consumer.** Run **one `ShintaiBleClient` per pod** and
   fold the two per-pod `ShintaiReadings` into a single merged `ShintaiReadings` in
   `:core`, so both apps' UIs keep consuming one readings object.
3. **Deterministic overlap.** When both pods supply the same channel, resolve it by a
   **precedence order** — contract-default per channel, and **live-overridable in Operator**.
   A source with no valid data is always skipped regardless of precedence.
4. **Combine base-side too.** The ground-station captures **both serial streams**, tags
   each row with its board, and aligns/merges them on **host wall-clock** (not per-board
   `millis()`), applying the same authority rule so base and glasses agree.
5. **Non-fatal per pod.** Either pod absent → the other's channels still render; the merged
   readings show per-pod connection state so a dropped pod is visible, not silent.

## Non-goals

- **No inter-pod link.** The two boards never talk to each other — no ESP-NOW, no mesh.
  They are independent producers; **all** combining happens at the consumer ([BU-1](#decisions)).
- **No new GATT characteristic and no per-pod service.** Both pods expose the **identical**
  service/UUIDs; they're told apart by **device name**, never by service ([BU-2](#decisions)).
- **No precedence sync between consumers (v1).** Operator's override is **Operator-local**;
  Glass and the ground-station use the contract defaults. Cross-consumer sync is
  [forward-path](#forward-path) ([BU-6](#decisions)).
- **No cross-board fusion of a single derived signal (v1).** Hokan is arbitrated as a
  whole channel (steps+heading+cadence from one pod), not reconstructed from `aft` steps ×
  `fwd` heading — that finer PDR is [forward-path](#forward-path) ([BU-5](#decisions)).
- **No shared clock.** The pods keep independent `millis()`; alignment is host-time on the
  base and notify-arrival on the phones. No time-sync protocol ([BU-4](#decisions)).

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [QT Py ESP32-S3](../../REGISTRY.md#host--infrastructure) ×2 | `fwd` pod + `aft` pod — identical binary, role from NVS | second host added |
| its own [STEMMA QT chain](../../REGISTRY.md#host--infrastructure) per pod | each pod is an independent bus; the two never share I²C | existing |
| `:core` **merger** (new) | folds two per-pod `ShintaiReadings` → one, by the authority table | new Kotlin, `android/core` |
| [Operator](../../REGISTRY.md#output--feedback) precedence screen (new) | live per-channel precedence override, persisted on the phone | new Compose UI |
| [Glass](../../REGISTRY.md#output--feedback) | two hardcoded MACs → two clients → merger (defaults) | existing app, rewired |
| [ground-station](../../groundstation) | two serial ports → tagged capture → host-time merge | existing tooling, extended |

## Behaviour — identify, stream per pod, reconcile

**Identity.** Each board stores a `role` string (`fwd`/`aft`) in NVS (the firmware already
carries `Preferences`). At boot it names itself `ShintaiOS-<role>` and echoes the role in
the human/CSV banner. The GATT **service UUID is identical** on both — a central tells
them apart by the name suffix (Operator scans and prefix-matches `ShintaiOS-`; Glass
hardcodes the two MACs). A pod defaults to `fwd` until set.

**Per-pod readings.** `ShintaiBleClient` is already per-device (it takes a `deviceAddress`
and connects direct, no scan). Each app runs **two** of them — one per pod — producing two
independent `ShintaiReadings`, each already sparse (only the channels that pod's sensors
fill are non-`—`).

**The authority table** (contract-default precedence; every row **overridable in Operator**
except where noted). *Precedence only arbitrates among sources that currently supply a
**valid** value — a blank / no-fix / null source is skipped first, so precedence never
selects "nothing over something."*

| Channel(s) | Default precedence | Rationale |
|---|---|---|
| `distance_l/r`, `alert` (Kōei) | **aft** → fwd | Rear arc lives on the pack |
| `heading`, `cardinal` | **fwd** → aft | HUD wants head orientation |
| `accel_*` | **fwd** → aft | Head IMU |
| `thermal*`, Thermal Grid | **fwd** → aft | Forward-looking thermal |
| `climate`, `environment`, `kyukaku` | **aft** → fwd | Air chem rides the pack |
| `gps_*` | **fwd** → aft (**fix-gated**) | A pod with no fix supplies nothing, so it's skipped before precedence |
| `steps` / Hokan | **aft** → fwd | Torso pedometer beats head-bob |

The two judgment calls raised in design — *which IMU owns `heading` vs `steps`*, and
*GPS when both have a fix* — are resolved by these defaults (`heading`→fwd, `steps`→aft,
GPS fix-gated then fwd) **and** made moot at runtime: the wearer can flip any of them in
Operator.

**The merge (one rule).** For each channel: filter the two pods to those supplying a valid
value, then take the one highest in that channel's precedence order. `packets` sums across
pods; the merged readings carry `perBoard: Map<Role, ConnectionState>` so the UI shows
`fwd ●  aft ○`. Because every channel in `ShintaiReadings` is already an independent value
(`heading: String`, `environment: String`, `hokan: HokanPdr?`…), the merge is a clean
per-field pick — **no model restructure**.

**Degradation.** One pod absent → its channels blank, the other pod and every consumer
keep running; `perBoard` marks it disconnected. Both absent → the apps sit in their
existing disconnected state. A pod that connects but has a sensor absent contributes
nothing for that channel, so the other pod (if any) wins by the skip-invalid rule.

## User-selectable precedence (Operator)

The precedence order is contract-*default*, not contract-*fixed*: Operator exposes a
**Sources** screen that lets the wearer decide which pod wins each contested channel, live.

- **Only contested channels get a control.** The screen lists channels the two connected
  pods **both** currently supply; a channel only one pod offers shows "fwd only" / "aft
  only" (informational, no toggle). Nothing to arbitrate ⇒ nothing to set.
- **Per-channel toggle / reorder.** Each contested channel shows the two pods in current
  precedence order with the active source marked; tapping swaps the order for that channel.
  (Two pods ⇒ a toggle; the model is an ordered list so a third pod later needs no rework.)
- **Persisted + live.** The override map lives in Operator's app prefs; changing it
  **re-merges immediately** (the reducer is pure — feed it a new precedence map and the
  merged flow updates). It survives app restarts.
- **Reset to contract defaults** is one tap — the defaults are the authority table above.
- **Scope is Operator-local (v1).** Glass and the ground-station apply the contract
  defaults. This keeps the override a lightweight consumer-side policy — no board round-trip,
  no consumer-to-consumer channel (the apps are both centrals; they don't see each other).
  Broadcasting the override to Glass is [forward-path](#forward-path) ([BU-6](#decisions)).

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`. **No sensor-logic change** — presence-driven
output already emits only present channels. Identity + a role setter only:

1. **Role in NVS.** Read `prefs` key `role` at boot (default `"fwd"`); expose a new serial
   command (`'R'`) that sets/cycles it (`fwd`↔`aft`) and persists. Applied at boot (the
   BLE name is set at init) — set-then-reboot, echoed on the banner.
2. **Name from role.** `BLEDevice::init("ShintaiOS")` → `"ShintaiOS-" + role`; update the
   `[OK] BLE advertising as …` banner and the human/CSV boot banner to name the role.
3. **Stamp the role in CSV.** Append a `board` column (value = the role) to the row and to
   `CSV_HEADER` — end-appended, Hokan-style, so old logs still parse and the row is
   self-identifying to the logger (see [Contract impact](#contract-impact)).
4. **Nothing else moves.** Same service, same UUIDs, same sensors, same cadence. A pod with
   no sensors on a channel blanks it exactly as today.

## Consumer integration (`:core` + apps)

- **`:core`.** Add a `Role` enum (`Fwd`, `Aft`) and a **pure** reducer
  `mergeReadings(perPod: Map<Role, ShintaiReadings>, precedence: Precedence): ShintaiReadings`
  implementing the one-rule merge above. `precedence` defaults to the authority table.
  Extend `ShintaiReadings` with `perBoard: Map<Role, ConnectionState>` (additive; existing
  fields unchanged). `ShintaiGatt`, the CCCD, and the parsers are **untouched** — same
  service, so `tools/check-contract.py`'s UUID checks stay green.
- **`:operator`.** Scan, prefix-match `ShintaiOS-`, pair **both** pods → two clients →
  merger. Add the **Sources** precedence screen (above), persisted in app prefs, wired into
  the merger's `precedence` argument. Its BLE recording tags rows with `board`.
- **`:glass`.** Hold **two** hardcoded MACs (the glasses' radio still can't scan), run two
  clients → merger with **default** precedence. UI unchanged — it still renders one merged
  `ShintaiReadings`; only the wiring (one client → two + merge) changes.

## Ground-station integration

Target: `groundstation/`. Combine base-side, on host time.

- **Capture both.** `shintai-logger.py` opens **both** serial ports concurrently and reads
  the `board` column (or banner) to tag each stream; each pod's raw CSV is logged as today,
  now carrying its `board` value.
- **The clock trap.** `timestamp_ms` is per-pod `millis()` since *that pod's* boot — the two
  share no clock. Combining on `timestamp_ms` is wrong. The logger **stamps host receipt
  time** and the merge/analysis aligns the two streams on host time.
- **Same rule as the apps.** `analyze.py` applies the **contract-default** authority table
  when it needs a single merged series, so base-side and glasses agree channel-for-channel.
- **`tools/check-contract.py`** learns the new `board` column and the `ShintaiOS-<role>`
  name scheme (still stdlib-only).

## Contract impact

Adds a **multi-producer model** to `CONTRACT.md`; the GATT table itself is unchanged.

- **New "Multi-producer" section.** Documents roles (`fwd`/`aft`), the `ShintaiOS-<role>`
  device-name identity scheme, "identical service on both pods, each notifies only its
  present channels," and the **authority table** (the new shared source of truth all three
  consumers implement as defaults).
- **CSV.** One **appended** column, `board` (`fwd`/`aft`) — end-appended like Hokan's
  `steps`, so old logs still parse and consumers keying on column *names* are unaffected.
  Mirror sites: firmware `CSV_HEADER`, `groundstation` columns, `tools/check-contract.py`.
- **BLE.** **No change** — same service, same characteristics, same CCCD on both pods. The
  Kotlin UUID mirror and the CCCD `8000` invariant are untouched.

`python3 tools/check-contract.py` stays green after the `board`-column + name-scheme
mirror edits.

## Acceptance criteria

1. **Two roles, one binary:** the same `.ino` build, flashed to both boards, advertises as
   `ShintaiOS-fwd` and `ShintaiOS-aft`; `'R'` sets/persists the role; the CSV `board` column
   reflects it.
2. **Federated readout:** with `fwd` carrying (e.g.) thermal + head IMU and `aft` carrying
   rear ToF + torso IMU + climate, both apps show **one** merged readout containing every
   channel, sourced from the correct pod.
3. **Overlap resolves by precedence:** with the same sensor on both pods, the merged value
   comes from the higher-precedence pod; if that pod's value is blank/invalid, the other
   wins (skip-invalid).
4. **Live override (Operator):** flipping a contested channel's source in the Sources screen
   changes the merged value immediately and persists across restart; Glass is unaffected
   (uses defaults).
5. **Per-pod liveness:** dropping one pod blanks only its channels, keeps the other live,
   and shows the pod disconnected in `perBoard`; both absent → normal disconnected state.
6. **Base-side merge on host time:** the logger captures both streams tagged by `board`;
   analysis aligns on host time (not `millis()`) and applies the default authority table.
7. **No regression:** contract linter green; firmware compiles; `android/build.sh detekt
   lint` green; single-pod operation (one board only) still works end-to-end.

## Decisions

- **BU-1 — Combine at the consumer, no inter-pod link (committed).** The pods never talk to
  each other; all federation happens where two producers meet one consumer (`:core` merger,
  base-side merge). ESP-NOW/mesh between boards is explicitly not taken — it would put fusion
  on the constrained producer and need a shared clock. Cost: each consumer merges
  independently.
- **BU-2 — Identical service, name-suffix identity (committed).** Both pods expose the same
  GATT service/UUIDs and are told apart by `ShintaiOS-<role>`, over per-pod services or a
  board-id characteristic. Keeps the GATT table + all mirror sites lean; the name carries
  identity.
- **BU-3 — Skip-invalid before precedence (committed).** Precedence arbitrates only among
  sources currently supplying a **valid** value, so a preferred-but-blank pod never beats a
  present one (this is what makes GPS "fix-gated" fall out of the general rule).
- **BU-4 — Host-time alignment, no clock sync (committed).** Per-pod `millis()` stays
  independent; the base aligns on host receipt time and the phones on notify arrival. A
  time-sync handshake is not taken — the merge doesn't need sub-second cross-pod alignment.
- **BU-5 — Hokan arbitrated whole (committed).** Steps+heading+cadence come from one pod
  (default `aft`); reconstructing a PDR from `aft` steps × `fwd` heading is deferred to
  [forward-path](#forward-path). Keeps v1's `HokanPdr` a single-source unit.
- **BU-6 — Operator-local override (committed for v1).** The precedence override is a
  consumer-side policy Operator edits and persists; Glass and the ground-station use the
  contract defaults. Broadcasting the override to Glass (via a board-relayed setting or a
  shared config) is forward-path — the apps are both centrals and don't see each other.
- **BU-7 — `board` column, end-appended (committed).** Combined logs carry a `board`
  discriminator as a trailing CSV column (Hokan precedent), so old logs parse and each row
  self-identifies, over a per-file-per-pod scheme with no in-band role.
- **BU-8 — `fwd` / `aft` role names (committed).** Forward pod (head-side) and aft pod
  (pack-side), over `head`/`pack` or `a`/`b` — directional, matches the body-worn framing,
  and reads cleanly in the device name.

## Cross-spec impact

- **Registry (build-time):** Bunshin earns a row in the [Zōkyō table](../../REGISTRY.md#zōkyō)
  beside its siblings (status `spec`); the second QT Py is noted in the
  [parts catalog](../../REGISTRY.md#host--infrastructure) as the `aft` host.
- **Contract:** `CONTRACT.md` gains the Multi-producer section + authority table and the
  `board` CSV column; firmware `CSV_HEADER`, `groundstation` cols, and
  `tools/check-contract.py` move with it.
- **Every prior Zōkyō:** unchanged in code — Bunshin doesn't alter any sensor's behaviour,
  it only lets that sensor live on either pod and be chosen at the merge. Kōei's nearer-arc
  reflex, Kyūkaku's spike, Kiatsu's floor sense, etc. all run per-pod exactly as specced;
  the merge picks which pod's copy the consumer shows.

## Forward path

- **Cross-consumer precedence sync.** Broadcast Operator's override so Glass honours it too
  — via a board-relayed config characteristic (a pod re-advertises the chosen precedence) or
  a phone→glasses side channel. Lifts [BU-6](#decisions)'s Operator-local limit.
- **Cross-board PDR.** Reconstruct Hokan from `aft` steps × `fwd` heading — a truer
  dead-reckoning than either pod alone, lifting [BU-5](#decisions).
- **N pods.** The authority table and reducer are ordered lists, not pairs — a third pod
  (boot ToF, wrist IMU) drops in with a new precedence row and a MAC, no machinery change.
- **Auto-role.** Instead of setting `role` by hand, a pod could infer `fwd`/`aft` from which
  sensors it enumerates at boot — but explicit NVS role is deterministic and survives a
  sensor swap.
- **Health merge.** Surface both pods' battery / RSSI in `perBoard` so the wearer sees which
  body-node is about to drop before it does.
