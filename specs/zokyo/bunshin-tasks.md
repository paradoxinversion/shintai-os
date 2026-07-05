# Bunshin (分身) — implementation tasks

Task breakdown for [`bunshin.md`](bunshin.md). Ordered by dependency; estimates in
story points (1/2/3/5/8). Check off `Done:` as each lands.

---

### Task 1: Firmware — role identity (NVS role, `ShintaiOS-<role>` name, `'R'` setter)
**What:** The same binary reads a `role` (`fwd`/`aft`) from NVS at boot, names itself `ShintaiOS-<role>`, and exposes a serial command to set/persist the role.
**Files:** `firmware/shintai-os/shintai-os.ino`
**Done when:** Flashing the identical build to a board and sending `'R'` sets the role, persists it across reboot, and the board advertises as `ShintaiOS-fwd` or `ShintaiOS-aft`; the human/CSV boot banner names the role.
**Depends on:** none
**Estimate:** 3
**Notes:** `Preferences` already `#include`d (`shintai-os.ino:16`, `prefs` at `:272`). Name is set at `BLEDevice::init` (`:535`) so a role change applies at next boot — document set-then-reboot. `'R'` is free in the serial command loop (`:916`). No sensor logic changes. Linter does not assert the device name (verified), so no contract edit needed here.
**Done:** [x] — `podRole` NVS-backed, BLE name `ShintaiOS-<role>`, `'R'` cycles+persists; compiles clean.

### Task 2: Contract — multi-producer model + authority table + `board` CSV column (atomic seam edit)
**What:** Add the multi-producer section (roles, identity scheme, authority table) to `CONTRACT.md` and append a `board` CSV column, mirrored across firmware, groundstation, and the linter in lockstep.
**Files:** `CONTRACT.md`, `firmware/shintai-os/shintai-os.ino` (`CSV_HEADER` + emit role as `board`), `groundstation/` (hardcoded column list), `tools/check-contract.py`
**Done when:** `python3 tools/check-contract.py` is green with the 27-column schema; `CONTRACT.md` documents the `ShintaiOS-<role>` identity scheme and the per-channel authority table; each pod stamps its role in the `board` column.
**Depends on:** 1
**Estimate:** 3
**Notes:** `board` is **end-appended** (Hokan precedent) so old logs still parse. The authority table here is the shared default all three consumers implement. Populating `board` needs the role from Task 1. Keep all four mirror sites atomic or the linter breaks mid-commit.
**Done:** [x] — CONTRACT.md Multi-producer section + authority table + `board` col; firmware `CSV_HEADER`+row emit; linter now guards the `ShintaiOS-<role>` scheme. Groundstation reads the header dynamically, so no code change there (its behaviour lands in Tasks 7–8). 27 cols, linter+compile green.

### Task 3: `:core` — Role enum, Precedence type, `mergeReadings` reducer, `perBoard`
**What:** A pure reducer that folds `Map<Role, ShintaiReadings>` into one merged `ShintaiReadings` using a `Precedence` (defaulting to the authority table), skipping invalid sources.
**Files:** `android/core/src/main/java/com/saboteur/shintai/core/` — new `Role`/`Precedence`/merge file, `Readings.kt` (add `perBoard`), new unit test
**Done when:** `mergeReadings` picks the highest-precedence *valid* source per channel (blank/no-fix/null skipped first), sums `packets`, populates `perBoard`; unit tests cover skip-invalid + precedence + single-pod; `android/build.sh detekt lint` green.
**Depends on:** 2
**Estimate:** 5
**Notes:** Field-by-field pick across all channels incl. rich types (`hokan: HokanPdr?`, `thermalGrid`, `kyukaku`). `ShintaiReadings` gains `perBoard: Map<Role, ConnectionState>` — additive, existing fields untouched, so parsers/`ShintaiGatt`/CCCD are not modified. Hokan arbitrated whole (BU-5). **Highest-value correctness surface — test it hard.**
**Done:** [x] — `Merge.kt` (`Role`/`Channel`/`Precedence`/`DEFAULT_PRECEDENCE`/`mergeReadings`), `perBoard` added to `ShintaiReadings`, 8 unit tests (precedence, skip-invalid, override, fix-gated GPS, single-pod, packets/perBoard, empty). Strict detekt + `:core:test` (8/0/0) + assemble all green. First test source set in `:core` (added `testImplementation(junit)`).

### Task 4: Operator — connect both pods (scan + prefix-match, two clients → merger)
**What:** Operator scans, prefix-matches `ShintaiOS-`, pairs both pods, runs two `ShintaiBleClient`s, and renders one merged readout (default precedence) with per-pod liveness; its BLE recording tags rows with `board`.
**Files:** `android/operator/…` (scan/connection layer, main readout, recording writer)
**Done when:** Operator connects to both pods and shows one merged readout; dropping one pod blanks only its channels and marks it disconnected in `perBoard`; the on-phone recording carries the `board` column.
**Depends on:** 3
**Estimate:** 5
**Notes:** **Risk:** two simultaneous GATT connections on Android — connection management / MTU negotiation per pod, autoConnect behaviour. `ShintaiBleClient` is already per-device (`ShintaiBleClient.kt:36-38`), so it's instantiation + lifecycle, not a rewrite.
**Done:** [x] — VM holds `clients`/`podReadings` maps keyed by `Role`, one `listenerFor(role)` folding into per-pod snapshots + `remerge()`; scanner prefix-matches `ShintaiOS-`, role derived from the name suffix; `connect(DeviceEntry)`/`reconnectLast()` (per-role persisted addrs); header shows per-pod liveness from `perBoard`; recorder writes one `board`-tagged row per pod. Assemble + strict detekt + lint green. (Live two-pod BLE reliability still wants on-hardware validation.)

### Task 5: Operator — Sources precedence screen (live override, persisted)
**What:** A settings screen listing currently-contested channels, letting the wearer flip which pod wins each, persisted in app prefs and re-merging live.
**Files:** `android/operator/…` (new Compose screen + prefs-backed precedence store, wired into the merger's `precedence` arg)
**Done when:** The screen shows only channels both connected pods supply (single-source channels shown as "fwd/aft only", no toggle); flipping one changes the merged value immediately and survives app restart; "reset to defaults" restores the authority table; Glass is unaffected.
**Depends on:** 4, 3
**Estimate:** 5
**Notes:** Contested-channel detection keys off live per-pod presence (`perBoard` + which fields each pod fills). Override is Operator-local (BU-6). Model precedence as an ordered list, not a bool, so a third pod needs no rework.
**Done:** [x] — `:core` gains `suppliedChannels()` (+test, 9/0/0); VM holds a persisted `Precedence` flow + `podSupply` flow, `setPreferred`/`resetPrecedence` re-merge live; `MultiPodSources` panel self-hides <2 pods, shows contested channels as pod toggles and single-source as "X only", with reset-to-defaults. Precedence stored as preferred-pod-per-channel (ordered-list model kept for >2 pods). Assemble + strict detekt + lint green.

### Task 6: Glass — connect both pods (two hardcoded MACs → two clients → merger)
**What:** Glass holds two hardcoded MACs, runs two clients, and renders one merged readout with default precedence.
**Files:** `android/glass/…` (MAC constants, connection layer, readout wiring)
**Done when:** Glass renders a merged readout from both pods; a dropped pod blanks only its channels; behaviour is identical whether or not Operator has set an override (Glass uses defaults).
**Depends on:** 3
**Estimate:** 3
**Notes:** Simpler than Operator — no scan (the glasses' radio can't), no override UI. Same two-simultaneous-connection BLE risk as Task 4. UI consumes one merged `ShintaiReadings` as before; only the wiring (one client → two + merge) changes.
**Done:** [ ]

### Task 7: Groundstation — dual-port capture, host-time stamping, `board` tagging
**What:** `shintai-logger.py` reads both serial ports concurrently, tags each stream's rows with `board`, and stamps host receipt time.
**Files:** `groundstation/shintai-logger.py`
**Done when:** Running the logger against two boards produces per-pod captures tagged by `board`, each row carrying a host-time stamp; single-board capture still works.
**Depends on:** 2
**Estimate:** 3
**Notes:** Concurrency across two serial ports (threads or async). Host time is the key — the two pods' `millis()` share no clock (BU-4).
**Done:** [ ]

### Task 8: Groundstation — host-time merge + authority rule in analysis
**What:** `analyze.py` aligns the two captures on host time (not `millis()`) and applies the default authority table to produce one merged series.
**Files:** `groundstation/analyze.py` (+ any shared merge helper)
**Done when:** Feeding two pod logs yields one merged series aligned on host time, with each channel sourced per the contract-default authority table (matching what the apps show).
**Depends on:** 7, 2
**Estimate:** 3
**Notes:** Implements the same authority table as `:core` (Task 3) — keep the default order in one documented place (the contract) so base and glasses agree.
**Done:** [ ]

## Summary
- **Total tasks:** 8
- **Total estimated effort:** 30 points
- **Critical path:** Tasks 1 → 2 → 3 → 4 → 5 (21 pts). After Task 3, Glass (6), and the groundstation chain (7 → 8) run in parallel with the Operator chain (4 → 5).
- **Risks:**
  - **Task 3** — merge correctness across mixed value types (strings + `HokanPdr?`/`ThermalGrid`) with skip-invalid semantics; the spec's highest-leverage code. Mitigate with thorough unit tests.
  - **Tasks 4 & 6** — two simultaneous Android GATT connections is a known BLE-stack reliability hazard (per-connection MTU, autoConnect, dropouts); may need real-hardware iteration.
  - **Task 5** — live contested-channel detection depends on accurate per-pod presence; edge cases when a pod connects/drops mid-session.
  - **Cross-cutting** — the authority-table default lives once in `CONTRACT.md` but is implemented three times (`:core`, Glass via `:core`, groundstation); drift between them would make base and glasses disagree.
