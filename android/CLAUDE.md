# android/ — BLE consumers (:core + :glass + :operator)

Two Kotlin/Compose apps that consume the board over BLE, plus a shared `:core` module
that holds the one Kotlin mirror of the data contract. The full tour — the two apps,
their per-app channel subsets, and the phosphor style — is in
[`android/README.md`](README.md); read it first. The GATT contract itself lives in
[`../CONTRACT.md`](../CONTRACT.md) (see root [`CLAUDE.md`](../CLAUDE.md) for the seam).

```
android/
  core/       :core      — ShintaiGatt (UUIDs) · ShintaiBleClient (transport) · Units · Readings. No UI.
  glass/      :glass      — RayNeo X3 Pro HUD          (com.saboteur.shintaiglass)
  operator/   :operator   — phone field console        (com.saboteur.shintaioperator)
```

## Commands

See `README.md` for full toolchain (Gradle 8.9 + Android Studio JBR / JDK 21). The
shell's default `java` is too old; `android/build.sh` points `JAVA_HOME` at the Studio
JBR so Gradle runs headless:

```sh
android/build.sh              # assembleDebug + lint (default)
android/build.sh detekt       # Kotlin static analysis (config: android/detekt.yml)
android/build.sh detekt lint  # what the pre-commit hook runs when android/ changed
```

## Invariants (easy to break)

- **`ShintaiGatt.kt` is a contract mirror site.** The GATT UUIDs in `:core` mirror
  `../CONTRACT.md`; both apps import them, so a UUID can never drift *between* apps —
  but it can drift from the firmware/contract. Change a UUID → edit `CONTRACT.md`
  first, then `ShintaiGatt`. Run `python3 tools/check-contract.py`.
- **BLE CCCD gotcha:** the notify-enable descriptor UUID is
  `00002902-0000-1000-8000-00805f9b34fb` — the `8000` (not `0000`) matters; the typo
  silently kills all notifications.
- **Which characteristics an app subscribes to is a per-app choice**, not a second
  source of truth. Both apps now take all string channels + Thermal Grid; the
  difference is what each *renders* — `:glass` subscribes to Environment only to
  derive Kyūkaku's smell **spike** badge, not to show the raw pressure/gas readout
  (that's `:operator`'s). Don't "fix" per-app subset differences by editing `:core`.
- **`:glass` hardcodes the board MAC** (the glasses' radio starves a scan);
  `:operator` scans & pairs. See `README.md`.
