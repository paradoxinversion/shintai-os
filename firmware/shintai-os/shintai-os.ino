#include <Wire.h>
#include <vl53l5cx_class.h>
#include <Adafruit_LSM6DSOX.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_GPS.h>
#include <Adafruit_MLX90640.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_BME680.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_APDS9960.h>
#include <SparkFun_AS3935.h>   // Enrai lightning sense (specs/zokyo/enrai.md): AS3935 Franklin detector
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <FFat.h>
#include <Preferences.h>
#include <math.h>
#include "Aizu.h"        // shared on-body output bus (specs/platform/aizu.md): sole NeoPixel writer
#include "KehaiBand.h"   // Kehai-Hikari proximity reflex (specs/zokyo/kehai-hikari.md): distance -> Aizu cue
#include "KankiBand.h"   // Kanki air-quality sense (specs/zokyo/kanki.md): co2_ppm -> Aizu ambient cue
#include "ThermalGrid.h" // Metsuke thermal sight (specs/zokyo/metsuke.md): MLX frame -> denoised 16x12 heat grid
#include "NesshiBand.h"  // Nesshi heat-sight (specs/zokyo/nesshi.md): hold BOOT -> surface temp -> Aizu cue
#include "HokanDsp.h"    // Hokan step-reckoning (specs/zokyo/hokan.md): fast IMU -> steps + fall SOS
#include "KyukakuBand.h" // Kyūkaku sense of smell (specs/zokyo/kyukaku.md): bmeGas -> baseline ratio -> Aizu cue
#include "KiatsuBand.h"  // Kiatsu barometric sense (specs/zokyo/kiatsu.md): bmePressure -> 3 h trend -> Aizu cue
#include "AndonPanel.h"  // Andon LED-matrix lantern (specs/zokyo/andon.md): raw-Wire 8x12 mono panel, raindrop flair
#include "ZanshinGrid.h" // Zanshin rear depth field (specs/zokyo/zanshin.md): VL53L5CX 8x8 -> L/R derive + depth grid

// BLE UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define DISTANCE_UUID       "abcd1234-ab12-ab12-ab12-abcdef123456"
#define ALERT_UUID          "abcd5678-ab12-ab12-ab12-abcdef123456"
#define HEADING_UUID        "abcd9012-ab12-ab12-ab12-abcdef123456"
#define ACCEL_UUID          "abcdef12-ab12-ab12-ab12-abcdef123456"
#define GPS_UUID            "abcd3456-ab12-ab12-ab12-abcdef123456"
#define THERMAL_UUID        "abcd6789-ab12-ab12-ab12-abcdef123456"
#define CLIMATE_UUID        "abcdba98-ab12-ab12-ab12-abcdef123456"
#define ENVIRONMENT_UUID    "abcdc0de-ab12-ab12-ab12-abcdef123456"
#define THERMAL_GRID_UUID   "abcd7890-ab12-ab12-ab12-abcdef123456"  // Metsuke: binary heat grid
#define HOKAN_UUID          "abcdf007-ab12-ab12-ab12-abcdef123456"  // Hokan: steps/heading/cadence ("f007" = foot)
#define ZANSHIN_GRID_UUID   "abcd5c88-ab12-ab12-ab12-abcdef123456"  // Zanshin: binary rear depth grid ("5c88" = 53L5CX 8x8)
#define LIGHTNING_UUID      "abcda535-ab12-ab12-ab12-abcdef123456"  // Enrai: lightning km/energy/strikes ("a535" = AS3935)
#define LIGHTNING_CTRL_UUID "abcda53c-ab12-ab12-ab12-abcdef123456"  // Enrai: WRITABLE AS3935 tuning control ("a53c" = AS3935 ctrl)

const int NEAR_MM  = 200;
const int FAR_MM   = 2000;   // Kehai-Hikari Approach/Clear boundary (was reserved)
const int UPDATE_MS = 1500;  // telemetry (CSV/BLE) update interval

// Kehai-Hikari proximity reflex — a fast loop decoupled from the 1500 ms telemetry
// cadence. serviceReflex() services the ToF here, caches the range, and posts the
// distance band as an Aizu cue. The telemetry block consumes the cached range so
// the two never fight over the sensor's dataReady (Kehai-Hikari note 2).
const int      REFLEX_MS               = 120;    // reflex tick (spec: ~100-150 ms)
const uint32_t KEHAI_MAX_AGE_MS        = 500;    // Aizu drops the cue if we stop posting
// Zanshin (残心) — rear depth field (specs/zokyo/zanshin.md): one VL53L5CX 8x8 multizone
// ToF (superseding Kōei's two VL53L4CX arcs) behind the PCA9546 mux. Reports at 0x29 like
// the old arcs, so it rides mux ch0 with the same select-before-touch discipline. Its
// left/right halves derive the LEFT/RIGHT nearest ranges below — so distance_l/r_mm + alert
// are unchanged, only the source moved from two point beams to one field (ZanshinGrid.h).
int16_t       cachedMmL    = -1;   // nearest valid range in the field's LEFT half;  -1 = none
int16_t       cachedMmR    = -1;   // nearest valid range in the field's RIGHT half; -1 = none
unsigned long lastReflexMs = 0;

VL53L5CX             zanshinTof(&Wire, -1);   // rear 8x8 depth field, mux ch0
VL53L5CX_ResultsData zanshinResults;          // last field frame (64 zones: distance_mm + status)
bool                 zanshinFresh = false;    // a new field frame was read this reflex tick

// PCA9546 4-channel I2C mux (0x70) — select a channel by writing a one-hot bitmask
// BEFORE every bus operation on that channel's sensor (the VL53L5CX hits I2C on init
// AND every read; the wrong channel = wrong sensor or errors).
const uint8_t TOF_MUX_ADDR = 0x70;
const uint8_t ZANSHIN_CH   = 0;   // the VL53L5CX rear field's mux channel
bool tofMuxPresent = false;       // PCA9546 acked at boot
bool zanshinDirect = false;       // field wired straight on the main bus (no mux) — bench fallback

void muxSelect(uint8_t ch) {
  Wire.beginTransmission(TOF_MUX_ADDR);
  Wire.write((uint8_t)(1 << ch));   // one-hot: 0x01 = ch0, 0x02 = ch1
  Wire.endTransmission();
}

// Select the rear field's mux channel before touching it — a no-op when the field is
// wired directly on the main bus (auto-detect bench fallback: no PCA9546 to gate on).
static inline void zanshinSelect() { if (!zanshinDirect) muxSelect(ZANSHIN_CH); }

// Nearest valid arc: the closer of two ranges, ignoring -1/no-target (-1 if neither
// has a target). The proximity reflex and the single `alert` bit key off this, so
// the wearer is warned by whichever arc sees the closest object.
static inline int16_t nearerMm(int16_t a, int16_t b) {
  if (a > 0 && b > 0) return a < b ? a : b;
  if (a > 0) return a;
  if (b > 0) return b;
  return -1;
}
Adafruit_LSM6DSOX imu;
Adafruit_LIS3MDL  mag;
Adafruit_GPS GPS(&Wire);
Adafruit_MLX90640 mlx;
SensirionI2cScd4x scd4x;   // SCD-40: CO2 + ambient air temp + humidity (I2C 0x62)
Adafruit_BME680 bme;       // BME688: gas + pressure, plus authoritative air temp/RH (I2C 0x77)

// Tertiary info pane — 1.3" 128x64 mono OLED (Adafruit #938) on the STEMMA QT bus at
// 0x3D. A flare / secondary-readout surface, distinct from Aizu's single-pixel
// glanceable cue. Presence-gated like every sensor: absent -> warn and boot on. -1 =
// no hardware reset pin (shared I2C). Two gotchas confirmed on THIS unit via the 'I'
// bus scan + on-glass test: (1) its address-select sits at 0x3D, not the 0x3C most
// breakouts default to; (2) it's an SSD1306 controller (Adafruit_SSD1306), NOT the
// SH1106 the #938 label implies — SH110X left the panel dark (its init omits the
// SSD1306 charge-pump enable, so the bias supply never came up).
#define OLED_W    128
#define OLED_H     64
#define OLED_ADDR 0x3D
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
bool oledPresent = false;

// The pane is a swipe-paged carousel: a HOME identity splash + four live screens.
// The APDS9960's gesture engine (below) pages left/right between them.
enum { PANE_HOME = 0, PANE_NAV, PANE_ENV, PANE_PROX, PANE_THERM, PANE_COUNT };
uint8_t  oledPage     = PANE_HOME;
bool     oledDirty    = true;    // force a redraw (set on a page change)
bool     paneReady    = false;   // first telemetry snapshot captured (live pages have data)
uint32_t oledLastDraw = 0;
const uint32_t OLED_REFRESH_MS = 750;   // live-page redraw cadence
const uint32_t OLED_ANIM_MS    = 33;    // HOME redraw cadence (~30 fps) — spins the splash cube
                                        // (a full SSD1306 display() is ~25 ms over I2C, so this
                                        // is near the bus ceiling; the loop blocks in it while on HOME)

// APDS9960 gesture / proximity / light sensor (STEMMA QT, 0x39) — the pane's swipe
// input. Only the gesture engine is used here (proximity must be on to feed it); the
// ambient-light channel is left for later. Polled (not INT-wired: the QT cable is I2C
// only), rate-limited so it doesn't flood the bus the reflex ToF also rides.
Adafruit_APDS9960 apds;
bool apdsPresent = false;
uint32_t apdsLastPoll = 0;
const uint32_t APDS_POLL_MS = 50;

// Snapshot of the pane's live fields — captured at the end of each telemetry cycle
// (these values are otherwise local to loop()), so the renderer can repaint on a swipe
// from the fast section without waiting for the next 1500 ms cycle.
struct PaneSnapshot {
  bool  magOk;   float heading;  char cardinal[4];
  bool  gpsFix;  float lat, lon, alt;  int sats;
  int16_t mmL, mmR;  bool alert;
  bool  thermalOk;  float tCtr, tMin, tMax, hotDelta;
} pane = {};

float thermalFrame[32 * 24];
MetsukeFilter metsukeFilter;   // temporal denoise + steadied palette range (ThermalGrid.h)

// Metsuke chunked-grid TX pacer. A 32x24 frame is METSUKE_CHUNKS notifications; firing
// them back-to-back overruns the BLE tx path so chunks drop/tear (garbage header +
// scrambled panel). Stage the built grid here and let serviceThermalTx() emit one chunk
// per ~connection interval instead.
uint8_t  metsukeGrid[METSUKE_GRID_BYTES];   // staged canonical grid awaiting chunked TX
uint8_t  metsukeFrameSeq   = 0;             // ++ per built frame (the chunk frame_seq)
int      metsukeChunksLeft = 0;             // chunks staged but unsent (0 = idle)
uint32_t metsukeLastChunkMs = 0;
const uint32_t METSUKE_CHUNK_TX_MS = 30;    // one chunk per ~30 ms (>= a BLE conn interval)

// Metsuke (目付) — live thermal grid over BLE. The MLX90640 frame is now serviced
// on its own ~2 Hz tick (serviceThermal), the single read site; the 1500 ms
// telemetry block consumes the cached frame for its summary temps, so the two
// never fight over getFrame (same pattern Kehai uses for the ToF). On each fresh
// frame Metsuke downsamples 32x24 -> 16x12, denoises + packs 196 bytes, and notifies
// the grid characteristic — gated on a subscribed central. NOTE: getFrame() blocks while
// reading; validate on-wrist that this 2 Hz cadence doesn't hitch the Kehai
// reflex (see specs/platform/follow-up-work.md).
const int     THERMAL_MS     = 500;      // ~2 Hz camera cadence (MD-5)
unsigned long lastThermalMs  = 0;
bool          thermalFrameOk = false;    // last getFrame() succeeded (telemetry reads this)

BLECharacteristic *distanceChar;
BLECharacteristic *alertChar;
BLECharacteristic *headingChar;
BLECharacteristic *accelChar;
BLECharacteristic *gpsChar;
BLECharacteristic *thermalChar;
BLECharacteristic *climateChar;
BLECharacteristic *environmentChar;
BLECharacteristic *thermalGridChar;   // Metsuke: binary 16x12 heat grid
BLECharacteristic *hokanChar;         // Hokan: "steps heading cadence" string (live PDR breadcrumb)
BLECharacteristic *lightningChar;     // Enrai: "km=.. e=.. n=.." lightning string (event-driven notify)
BLECharacteristic *lightningCtrlChar; // Enrai: WRITABLE AS3935 tuning control (app→board) + config notify
BLE2902           *thermalGridCccd = nullptr;   // its CCCD — emit only while subscribed
BLECharacteristic *zanshinGridChar;             // Zanshin: binary 8x8 rear depth grid
BLE2902           *zanshinGridCccd = nullptr;   // its CCCD — emit only while subscribed
uint32_t           gridNotifyCount = 0;         // grid notifies sent (for the 'G' probe)

bool deviceConnected = false;
bool alertSent = false;
unsigned long lastUpdate = 0;

// Per-module presence — set at boot. Any sensor may be physically absent for a
// config test; we warn and continue rather than hang, then gate its reads and
// output below. (SCD-40 has its own scdPresent flag further down.)
bool hasZanshin     = false;   // VL53L5CX rear depth field inited (mux ch0)
bool imuPresent     = false;
bool magPresent     = false;
bool thermalPresent = false;

// Enrai (遠雷) — AS3935 "Franklin" lightning detector (specs/zokyo/enrai.md): direct on the
// main bus at 0x03, polled every loop (no IRQ pin wired). A strike updates the snapshot
// below + notifies the Lightning characteristic; the CSV logs the last-strike snapshot +
// cumulative count. Config mirrors the bench bring-up that caught the storm.
#define ENRAI_ADDR       0x03
#define ENRAI_INDOOR     0x12   // AFE gain: high (sensor indoors / weak signals)
#define ENRAI_OUTDOOR    0x0E   // AFE gain: lower (strong storm signals) — spreads the distance estimate
#define ENRAI_LIGHTNING  0x08   // readInterruptReg() value for a validated strike
#define ENRAI_DISTURBER  0x04
#define ENRAI_NOISE      0x01
SparkFun_AS3935 enrai(ENRAI_ADDR);
bool     hasEnrai         = false;   // AS3935 answered + inited at boot
int16_t  lightningKm      = 0;       // last strike distance (km; 1=overhead, 63=out of range, 0=none yet)
uint32_t lightningEnergy  = 0;       // last strike raw energy (not physical units)
uint32_t lightningStrikes = 0;       // cumulative validated strikes since boot
uint8_t  enraiWatchdog    = 1;       // AS3935 watchdog threshold (1-10): lower = more
                                     // sensitive → catches distant strikes; higher rejects
                                     // more man-made disturbers. Live-tunable via 'w'/'W'.
bool     enraiOutdoor     = false;   // AFE gain: false=INDOOR (high/sensitive), true=OUTDOOR (lower — spreads distance in a strong storm)
uint8_t  enraiSpike       = 2;       // spike rejection (1-11): higher = stricter waveform validation
uint8_t  enraiTuneCap     = 0;       // antenna tuning cap (0-15); empirical — no IRQ wired to measure the LCO
// The AS3935 tuning is live-adjustable from the app (writable Lightning Control char) AND
// serial ('o' gain · 's'/'S' spike · 'x' clear-stats · 'y'/'Y' tune-cap). One apply path:
void applyEnraiConfig();                    // push all of the above to the sensor
void enraiConfigStr(char *buf, size_t n);   // "gain=out spike=2 wdog=1 tune=0"
void applyEnraiCommand(const char *cmd);    // one control token -> update + apply + notify
uint32_t enraiDisturbers  = 0;       // man-made disturbers seen (diagnostic, 'K')
uint32_t enraiNoise       = 0;       // noise-floor-high events seen (diagnostic, 'K')
unsigned long lastEnraiMs = 0;       // poll timer — throttles the AS3935 read (see serviceEnrai)
const unsigned long ENRAI_POLL_MS = 10;   // ≥10 ms between polls. The AS3935's interrupt register
                                          // needs ~2 ms to settle after an event; polling every loop
                                          // iteration (sub-ms bursts) read-and-clears it inside that
                                          // window and LOSES strikes. The bench's delay(10) never hit
                                          // this and caught strikes reliably — so we match it.

// SCD-40 state. Present is set at boot; it updates every ~5s (slower than our
// loop), so we cache the last good reading between fresh samples.
bool     scdPresent = false;
bool     scdHasData = false;
uint16_t scdCo2     = 0;
float    scdTemp    = 0;   // °C
float    scdHum     = 0;   // %RH

// Kanki (換気) — air-quality light. The hysteretic CO2 band derived from scdCo2,
// posted to Aizu as an Ambient cue (Kanki never touches the pixel). -1 = unseeded
// (warm-up); it snaps to the true band on the first reading, then steps with ±50
// ppm hysteresis. maxAge outlives the SCD's ~5 s cadence so the cue holds (and
// Aizu breathes it) between fresh samples without lapsing to Idle. See KankiBand.h.
int            kankiBandIdx    = -1;
const uint32_t KANKI_MAX_AGE_MS = 15000;   // ~3 SCD cycles before the cue is dropped

// Nesshi (熱視) — heat-sight. While BOOT is held, read the surface temperature at
// the centre of the thermal FOV (spot) — or the scene's hottest point (scene, via a
// double-hold, ND-3) — and post it to Aizu as an INTERACTIVE, STEADY colour cue
// (blue->green->amber->orange->red on the burn-safety line). Consumes the cached
// MLX90640 frame (serviceThermal is the sole read site): no new sensor servicing,
// no telemetry disturbance, no contract change. Aizu owns the pixel and GPIO0;
// Nesshi only subscribes to the HOLD gesture and posts cues. See NesshiBand.h.
bool           nesshiHeld          = false;   // BOOT currently held (HOLD_START..HOLD_END)
bool           nesshiScene         = false;   // this hold reads the scene max, not the centre
int            nesshiBandIdx       = -1;      // hysteretic band; -1 re-seeds to the true band each hold
uint32_t       nesshiLastHoldEndMs = 0;       // last HOLD_END — for double-hold (scene) detection
const uint32_t NESSHI_DOUBLE_HOLD_MS = 500;   // a new hold within this of the last release => scene mode
const uint32_t NESSHI_MAX_AGE_MS     = 1000;  // Aizu drops the cue if we stop refreshing (we post every loop while held)

// Hokan (歩勘) — step-reckoning. The first module to do live on-device DSP: the IMU
// is sampled fast (serviceHokan, ~50 Hz — well above gait rate and the 1.5 s
// telemetry gate) and fed to two pure detectors (HokanDsp.h). Steps accumulate into
// a cumulative counter logged as the CSV `steps` column (the ground-station
// dead-reckons a GPS-denied path from Δsteps × step_length @ heading). A detected
// fall posts a latching Aizu SOS (AZ-11, top-tier ALERT), cleared when the wearer
// gets up. Consumes the existing LSM6DSOX; no new BOM, no telemetry disturbance.
const uint32_t     HOKAN_SAMPLE_MS   = 20;      // ~50 Hz IMU DSP tick (self-rate-limited)
const uint32_t     HOKAN_SOS_MAX_AGE_MS = 2000; // re-posted each tick while latched; drop if servicing stops
unsigned long      lastHokanMs       = 0;
uint32_t           hokanSteps        = 0;        // cumulative step count since boot (CSV `steps`)
HokanStepDetector  hokanStep;
HokanFallDetector  hokanFall;

// BME688 state. Present set at boot; performReading() fires the gas heater and
// blocks ~150 ms, so we read once per loop and cache. Owns air temp/humidity
// (precedence over SCD-40) plus the BME-only pressure + gas fields.
bool  bmePresent  = false;
bool  bmeHasData  = false;
float bmeTemp     = 0;   // °C
float bmeHum      = 0;   // %RH
float bmePressure = 0;   // hPa
float bmeGas      = 0;   // ohms

// Kyūkaku (嗅覚) — sense of smell. Watches bmeGas against an adaptive clean-air
// baseline (calibration-free, KY-1): a fast drop of the ratio = a violet→red Spike
// ("the air just changed", transient ALERT), a sustained low ratio = a violet
// Foul/Taint ambient. Humidity-vetoed from bmeHum (same reading, KY-4). Settles
// ~120 s before arming (KY-5). Rolling state lives in KyukakuState; the spike-hold
// timer is Arduino-domain. POST, never paint; derives from the existing bmeGas — no
// contract change, no telemetry disturbance. See KyukakuBand.h.
KyukakuState   kyukakuState        = {0.0f, 1.0f, 0.0f, 1.0f, KYUKAKU_BAND_CLEAN, 0};
uint32_t       kyukakuSpikeUntilMs = 0;       // Spike cue held until this millis (0 = inactive)
const uint32_t KYUKAKU_SPIKE_HOLD_MS = 3000;  // a Spike startles for ~3 s, then decays to the band
const uint32_t KYUKAKU_MAX_AGE_MS    = 6000;  // ~4 BME cadences before Aizu drops a stale cue

// Kiatsu (気圧) — barometric sense. Sub-samples bmePressure into a 3-hour ring buffer
// (~45 s cadence) and posts the calmest cue in the system — a cyan weather-turn — when
// the barometer falls (KiD-1, AZ-13). The floor-detection half is BASE-SIDE
// (groundstation/kiatsu.py): its signal survives in the CSV untouched, so the firmware
// only keeps the slow weather trend. POST, never paint; derives from the existing
// bmePressure — no contract change, no telemetry disturbance. See KiatsuBand.h.
KiatsuState    kiatsuState     = {{0}, 0, 0, KIATSU_WX_STEADY};
KiatsuObs      kiatsuObs       = {false, 0.0f, KIATSU_WX_STEADY};  // last resolved trend (re-posted each loop)
uint32_t       kiatsuLastPushMs = 0;          // last ring sub-sample (0 = none yet)
const uint32_t KIATSU_MAX_AGE_MS = 6000;      // re-posted every loop, so a modest liveness backstop

static inline float cToF(float c) { return c * 9.0 / 5.0 + 32.0; }

// Output mode — toggle live over Serial: send 'h' (human), 'c' (CSV only), 'b' (both).
// Default BOTH so the Python logger captures CSV rows while the terminal stays readable.
enum OutputMode { HUMAN, CSV, BOTH };
OutputMode outputMode = BOTH;
bool csvHeaderPrinted = false;

// CSV column order — shared by the Serial stream and the onboard flash log.
const char* CSV_HEADER =
  "timestamp_ms,distance_l_mm,distance_r_mm,alert,heading_deg,cardinal,"
  "accel_x,accel_y,accel_z,gps_fix,lat,lon,alt_m,speed_kmh,"
  "sats,thermal_min,thermal_ctr,thermal_max,thermal_mean,"
  "hotspot_delta,co2_ppm,air_temp_c,humidity_pct,pressure_hpa,gas_ohms,"
  "steps,lightning_km,lightning_energy,lightning_strikes,board";
  // steps: Hokan pedometer · lightning_*: Enrai AS3935 last-strike snapshot + count ·
  // board: Bunshin pod role (fwd/aft) — board stays terminal; lightning cols inserted before it

// Onboard flash logging (FFat) — autonomous field capture, pulled over USB.
// Each power-up writes a new sequential file (/shtNNNN.csv). Rows are logged
// only while UNtethered (no USB host), so tethered live sessions are unaffected.
Preferences prefs;

// Bunshin (分身): this pod's role — "fwd" (forward / head-side) or "aft" (pack-side).
// The SAME binary runs on both pods; role is data (NVS key "role"), not a build flag.
// It names the BLE device ShintaiOS-<role> so a central can tell the two pods apart.
// Set/cycled live with the 'R' serial command; applied at boot (the BLE name is fixed
// at init), so a change takes effect on the next reset. Defaults to "fwd" until set.
String podRole = "fwd";

File logFile;
bool fsReady = false;
char logPath[24];

// Durability: close() commits the FAT directory entry to flash; per-row flush()
// alone does not survive a field power cut (leaves orphaned clusters — allocated
// but with no directory entry, so unlistable/unpullable). Close on a row cadence
// so at most this many rows are at risk of a power cut, then reopen in APPEND.
const uint32_t FLASH_COMMIT_ROWS = 16;
uint32_t rowsSinceCommit = 0;

// Open a fresh session file named by a persistent (NVS) boot counter.
void openNewLog() {
  uint32_t seq = prefs.getUInt("seq", 0) + 1;
  prefs.putUInt("seq", seq);
  snprintf(logPath, sizeof(logPath), "/sht%04u.csv", (unsigned)seq);  // 8.3-safe
  logFile = FFat.open(logPath, FILE_WRITE);
  if (logFile) { logFile.println(CSV_HEADER); logFile.flush(); }
}

// Enumerate flash logs by their deterministic /shtNNNN.csv names, iterating the
// NVS boot counter 1..seq. The ESP32 core's FFat root openNextFile() enumerates
// NOTHING on this board even for well-formed closed files (confirmed: exists()
// and open-by-name work, but directory iteration returns 0) — so listing/dumping
// via openNextFile silently found no files. We only ever create /shtNNNN.csv, so
// name-based iteration is complete and sidesteps the broken enumeration.
void listLogs() {
  if (!fsReady) { Serial.println("<<<NOFS>>>"); return; }
  uint32_t seq = prefs.getUInt("seq", 0);
  int count = 0;
  for (uint32_t i = 1; i <= seq; i++) {
    char p[24]; snprintf(p, sizeof(p), "/sht%04u.csv", (unsigned)i);
    if (!FFat.exists(p)) continue;
    File f = FFat.open(p);
    if (!f) continue;
    Serial.printf("  %s\t%u bytes\n", p, (unsigned)f.size());
    count++;
    f.close();
  }
  Serial.printf("  %d file(s), %u bytes used / %u total\n",
                count, (unsigned)FFat.usedBytes(), (unsigned)FFat.totalBytes());
}

// Dump every flash file over Serial, wrapped in markers the host parser keys on.
// Enumerated by seq-name (see listLogs) since openNextFile() is broken here.
void dumpAllLogs() {
  if (!fsReady) { Serial.println("<<<NOFS>>>"); return; }
  if (logFile) { logFile.close(); rowsSinceCommit = 0; }  // commit current session before dumping
  uint32_t seq = prefs.getUInt("seq", 0);
  for (uint32_t i = 1; i <= seq; i++) {
    char p[24]; snprintf(p, sizeof(p), "/sht%04u.csv", (unsigned)i);
    if (!FFat.exists(p)) continue;
    File f = FFat.open(p);
    if (!f) continue;
    Serial.printf("<<<BEGIN %s %u>>>\n", p, (unsigned)f.size());
    uint8_t buf[256]; int n;
    while ((n = f.read(buf, sizeof(buf))) > 0) Serial.write(buf, n);
    Serial.printf("\n<<<END %s>>>\n", p);
    f.close();
  }
  Serial.println("<<<DONE>>>");
}

// Erase by seq-name (see listLogs) since openNextFile() is broken here.
void eraseLogs() {
  if (!fsReady) { Serial.println("<<<NOFS>>>"); return; }
  if (logFile) { logFile.close(); rowsSinceCommit = 0; }
  uint32_t seq = prefs.getUInt("seq", 0);
  int erased = 0;
  for (uint32_t i = 1; i <= seq; i++) {
    char p[24]; snprintf(p, sizeof(p), "/sht%04u.csv", (unsigned)i);
    if (FFat.exists(p) && FFat.remove(p)) erased++;
  }
  openNewLog();                       // resume logging into a fresh file
  fsReady = (bool)logFile;
  Serial.printf("<<<ERASED %d>>>\n", erased);
}

// ── Diagnostics (serial commands 'I' / 'T') ── the boot banner is uncatchable on
// native-USB, so these let us probe the I2C bus and the ToF live after boot.

// Scan the I2C bus and print every responding address. NOTE: the VL53L5CX rear field
// (0x29) sits BEHIND the PCA9546 mux (0x70), which gates it off a naive scan — so 0x29 is
// EXPECTED to be absent here (the mux, 0x70, should show). And even when reachable it does
// NOT ACK a bare address-only scan while continuously ranging. Use 'T' (probeTof) as the
// authoritative field check, not this scan.
void scanI2C() {
  Serial.println("<<<I2C scan>>>");
  int n = 0;
  // Start at 0x03 (not the usual 0x08): the AS3935 (Enrai) sits at the I2C-RESERVED
  // address 0x03, so a normal 0x08-floor scan can't see it. 0x03-0x07 are harmless to
  // bare-address probe; 0x00-0x02 (general-call / CBUS) stay skipped.
  for (uint8_t a = 0x03; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("  0x%02X\n", a); n++; }
  }
  Serial.printf("  %d device(s) (expect ToF mux=0x70 [gates 0x29 field], IMU=0x6a, mag=0x1c/0x1e, GPS=0x10, thermal=0x33, SCD=0x62, BME=0x77, APDS=0x39, Andon matrix=0x3f, lightning=0x03)\n", n);
}

// Probe the VL53L5CX rear field ('T'): report the boot presence flag, then poll for a
// fresh frame and print the nearest zone per half (the L/R the reflex derives).
// Distinguishes not-present (init failed) from not-ranging (data never ready).
void probeTof() {
  Serial.printf("<<<TOF field=%d mux=%d direct=%d>>>\n",
                hasZanshin ? 1 : 0, tofMuxPresent ? 1 : 0, zanshinDirect ? 1 : 0);
  if (!hasZanshin) {
    Serial.println("  VL53L5CX not initialized at boot — mux missing and no field on the bus");
    return;
  }
  zanshinSelect();                      // select-before-touch (no-op if direct): probe hits I2C
  for (int attempt = 0; attempt < 25; attempt++) {   // ~500 ms window
    uint8_t ready = 0;
    zanshinTof.vl53l5cx_check_data_ready(&ready);
    if (ready) {
      zanshinTof.vl53l5cx_get_ranging_data(&zanshinResults);
      int16_t l, r;
      zanshinDeriveLR(zanshinResults.distance_mm, zanshinResults.target_status, &l, &r);
      Serial.printf("  frame after %d ms: L=%d mm  R=%d mm  (nearest=%d)\n",
                    attempt * 20, l, r, nearerMm(l, r));
      return;
    }
    delay(20);
  }
  Serial.println("  data never ready over ~500 ms — field present but not measuring");
}

// Bring up the VL53L5CX rear depth field. When viaMux, selects ch0 FIRST (init hits I2C —
// it uploads the sensor firmware, ~hundreds of ms); otherwise talks to it straight on the
// main bus at 0x29 (bench fallback, no PCA9546). Sets 8x8 @ 15 Hz and starts ranging.
// Non-fatal: a missing field returns false and must not block advertising.
bool initZanshin(bool viaMux) {
  if (viaMux) muxSelect(ZANSHIN_CH);
  zanshinTof.begin();
  if (zanshinTof.init_sensor() != 0) {   // default addr 0x52 (8-bit) == 0x29 7-bit
    Serial.printf("[WARN] VL53L5CX rear field not found %s — Zanshin disabled\n",
                  viaMux ? "on mux ch0" : "on the main bus (0x29)");
    return false;
  }
  zanshinTof.vl53l5cx_set_resolution(VL53L5CX_RESOLUTION_8X8);
  zanshinTof.vl53l5cx_set_ranging_frequency_hz(15);
  zanshinTof.vl53l5cx_start_ranging();
  Serial.printf("[OK] VL53L5CX rear depth field (Zanshin, 8x8 @ 15 Hz, %s)\n",
                viaMux ? "mux ch0" : "direct 0x29");
  return true;
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s)    { deviceConnected = true;  Serial.println(">> Phone connected"); }
  void onDisconnect(BLEServer *s) { deviceConnected = false; alertSent = false;
                                    Serial.println(">> Phone disconnected");
                                    s->startAdvertising(); }
};

// Enrai's writable Lightning Control characteristic — the first app→board path. A central
// writes a short command token ("gain", "spike+", "wdog-", "tune+", "clear"); we apply it
// to the AS3935 and notify the new config back. Same tokens the serial knobs use.
class EnraiCtrlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    if (v.length() > 0) applyEnraiCommand(v.c_str());
  }
};

void setup() {
  Serial.begin(115200);
  unsigned long serialWait = millis();
  while (!Serial && millis() - serialWait < 1500) delay(10);  // boot even untethered

  Serial.println("=============================");
  Serial.println("      SHINTAI-OS — FIELD TEST");
  Serial.println("=============================");

  // ── Onboard flash logging (FFat) ──
  prefs.begin("shintai", false);

  // Bunshin: load this pod's role (NVS) before BLE init names the device from it.
  // (This boot line is uncatchable after a reset — native-USB re-enum outruns the
  //  serial wait; verify role via the 'R' echo and the advertised ShintaiOS-<role>.)
  podRole = prefs.getString("role", "fwd");
  Serial.println("[OK] Bunshin pod role: '" + podRole + "' — advertises as 'ShintaiOS-" + podRole + "'");
  if (FFat.begin(true)) {          // format on first use
    openNewLog();
    fsReady = (bool)logFile;
    if (fsReady) Serial.println("[OK] FFat — field log: " + String(logPath));
    else         Serial.println("[WARN] FFat file open failed");
  } else {
    Serial.println("[WARN] FFat mount failed — onboard logging disabled");
  }

  Wire.begin(41, 40);
  // I2C Fast Mode (400 kHz). The MLX90640 grid read is ~3.3 KB over the bus; at the
  // default 100 kHz it blocks the loop long enough (~175 ms) to stretch the telemetry
  // cadence and hitch Kehai's reflex once Metsuke reads at 2 Hz. 400 kHz roughly
  // quarters every I2C transfer (and speeds the ToF reflex read too). Every sensor
  // on the chain supports Fast Mode; the MLX itself goes to 1 MHz.
  Wire.setClock(400000);
  delay(100);

  // Rear depth field (Zanshin) — one VL53L5CX behind the PCA9546 mux (0x70). Non-fatal: a
  // missing mux or field warns and continues so BLE still advertises. The field inits only
  // while its channel is selected (init hits I2C — it uploads the sensor firmware).
  Wire.beginTransmission(TOF_MUX_ADDR);
  tofMuxPresent = (Wire.endTransmission() == 0);
  if (tofMuxPresent) {
    Serial.println("[OK] PCA9546 ToF mux @ 0x70");
    hasZanshin = initZanshin(true);            // production: field behind mux ch0
  } else {
    // Auto-detect fallback: no mux, but the VL53L5CX may be wired straight on the main
    // bus (bench bring-up). Try it at its default 0x29 with no channel gating.
    Serial.println("[WARN] PCA9546 mux not found @ 0x70 — trying rear field direct on bus");
    hasZanshin = initZanshin(false);
    zanshinDirect = hasZanshin;
  }

  // IMU — non-fatal: warn and continue if absent
  imuPresent = imu.begin_I2C();
  Serial.println(imuPresent ? "[OK] LSM6DSOX IMU"
                            : "[WARN] LSM6DSOX not found — accel disabled");

  // Magnetometer — non-fatal: warn and continue if absent
  magPresent = mag.begin_I2C();
  Serial.println(magPresent ? "[OK] LIS3MDL Magnetometer"
                            : "[WARN] LIS3MDL not found — heading disabled");

  // GPS
  GPS.begin(0x10);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
  Serial.println("[OK] PA1010D GPS");

  // Thermal — non-fatal: warn and continue if absent
  thermalPresent = mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire);
  if (thermalPresent) {
    mlx.setMode(MLX90640_CHESS);
    mlx.setResolution(MLX90640_ADC_18BIT);
    // Refresh at 8 Hz, well ABOVE Metsuke's 2 Hz grid read (serviceThermal). Reading
    // slower than the sensor produces means getFrame() finds data already waiting and
    // returns without polling — the ~175 ms blocking wait that stretched the telemetry
    // cadence and threatened the reflex was that poll, not the I2C transfer (400 kHz
    // alone didn't move it). Faster refresh is noisier per pixel, but the 4x3 block
    // average per grid cell absorbs it.
    mlx.setRefreshRate(MLX90640_8_HZ);
    Serial.println("[OK] MLX90640 Thermal");
  } else {
    Serial.println("[WARN] MLX90640 not found — thermal disabled");
  }

  // SCD-40 (CO2 / air temp / humidity) — non-fatal: warn and continue if absent
  scd4x.begin(Wire, SCD40_I2C_ADDR_62);
  scd4x.stopPeriodicMeasurement();              // clear any prior session (~500ms)
  delay(500);
  if (scd4x.startPeriodicMeasurement() == 0) {
    scdPresent = true;
    Serial.println("[OK] SCD-40 Climate (first reading in ~5s)");
  } else {
    Serial.println("[WARN] SCD-40 not found — climate readings disabled");
  }

  // BME688 (env: gas + pressure, and the authoritative air temp/RH) — non-fatal.
  // SparkFun SEN-19096 answers at 0x77 (the Adafruit_BME680 default is 0x76).
  bmePresent = bme.begin(0x77);
  if (bmePresent) {
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150);   // 320°C for 150 ms
    Serial.println("[OK] BME688 Env (gas/pressure + air T/RH)");
  } else {
    Serial.println("[WARN] BME688 not found — pressure/gas disabled, air T/RH falls back to SCD-40");
  }

  // Enrai (遠雷) — AS3935 lightning detector, direct on the main bus at 0x03 — non-fatal.
  // No IRQ pin is wired, so serviceEnrai() polls the interrupt-source register. Config
  // mirrors the bench bring-up that caught the storm: INDOOR gain (most sensitive),
  // watchdog 3 to reject man-made disturbers, disturbers unmasked so a real strike is
  // never gated behind one.
  // Presence-gate on the address-ACK at 0x03 (the AS3935's I2C-reserved address) — same
  // idiom as every other sensor. A register read-back confirm was tried and reverted: run
  // right after begin() it false-NEGATIVES a present sensor (the AS3935's oscillators need
  // to settle first), and the ACK already detects the sensor reliably (confirm with 'I',
  // which now scans down to 0x03).
  Wire.beginTransmission(ENRAI_ADDR);
  if (Wire.endTransmission() == 0 && enrai.begin(Wire)) {
    hasEnrai = true;
    applyEnraiConfig();            // gain / noise / watchdog / spike / tune-cap / mask, one place
    Serial.println("[OK] AS3935 Lightning (Enrai, 0x03, polled)");
  } else {
    Serial.println("[WARN] AS3935 not found @ 0x03 — lightning disabled");
  }

  // BLE
  // Bunshin: name carries the pod's identity — ShintaiOS-fwd / ShintaiOS-aft. The
  // service UUID is identical on both pods; a central tells them apart by this name.
  String bleName = "ShintaiOS-" + podRole;
  BLEDevice::init(bleName.c_str());
  // Metsuke's 68-byte grid exceeds the default 20-byte ATT payload, so allow a
  // larger MTU; the central drives the actual negotiation (the apps request 247).
  BLEDevice::setMTU(247);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *service = server->createService(BLEUUID(SERVICE_UUID), 64);

  auto makeChar = [&](const char* uuid, const char* label) {
    BLECharacteristic *c = service->createCharacteristic(uuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    c->addDescriptor(new BLE2902());
    BLEDescriptor *d = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
    d->setValue(label);
    c->addDescriptor(d);
    return c;
  };

  distanceChar = makeChar(DISTANCE_UUID, "Distance (mm)");
  alertChar    = makeChar(ALERT_UUID,    "Proximity Alert");
  headingChar  = makeChar(HEADING_UUID,  "Heading (deg)");
  accelChar    = makeChar(ACCEL_UUID,    "Accelerometer");
  gpsChar      = makeChar(GPS_UUID,      "GPS");
  thermalChar  = makeChar(THERMAL_UUID,  "Thermal (C)");
  climateChar  = makeChar(CLIMATE_UUID,  "Climate (CO2/Temp/RH)");
  environmentChar = makeChar(ENVIRONMENT_UUID, "Environment (P/gas/T/RH)");
  hokanChar       = makeChar(HOKAN_UUID,       "Hokan (steps/heading/cadence)");
  lightningChar   = makeChar(LIGHTNING_UUID,   "Lightning (km/energy/n)");
  // Enrai control — the one WRITABLE characteristic (READ|WRITE|NOTIFY). A central writes a
  // tuning token; onWrite applies it and notifies the config back. Built directly (not via
  // makeChar) for the WRITE property + the write callback.
  lightningCtrlChar = service->createCharacteristic(LIGHTNING_CTRL_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  lightningCtrlChar->addDescriptor(new BLE2902());
  {
    BLEDescriptor *cd = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
    cd->setValue("Lightning Control (AS3935 tuning)");
    lightningCtrlChar->addDescriptor(cd);
  }
  lightningCtrlChar->setCallbacks(new EnraiCtrlCallbacks());
  { char cfg[48]; enraiConfigStr(cfg, sizeof(cfg)); lightningCtrlChar->setValue(cfg); }
  // Metsuke: the one BINARY characteristic (68-byte packed heat grid). Built
  // directly (not via makeChar) so we KEEP the BLE2902 pointer — we gate emission
  // on the client having subscribed (getNotifications, AC-4), and on this stack
  // getDescriptorByUUID(0x2902) returns null (16-bit-vs-stored-UUID mismatch),
  // which silently left the grid gated off. The string chars never hit this: they
  // notify() unconditionally, so they never needed the CCCD handle.
  thermalGridChar = service->createCharacteristic(THERMAL_GRID_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  thermalGridCccd = new BLE2902();
  thermalGridChar->addDescriptor(thermalGridCccd);
  {
    BLEDescriptor *d = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
    d->setValue("Thermal Grid (16x12)");
    thermalGridChar->addDescriptor(d);
  }

  // Zanshin: the SECOND binary characteristic (128-byte rear depth grid). Same pattern as
  // the thermal grid — keep the BLE2902 pointer so emission is gated on a subscribed central.
  zanshinGridChar = service->createCharacteristic(ZANSHIN_GRID_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  zanshinGridCccd = new BLE2902();
  zanshinGridChar->addDescriptor(zanshinGridCccd);
  {
    BLEDescriptor *d = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
    d->setValue("Rear Depth Grid (8x8)");
    zanshinGridChar->addDescriptor(d);
  }

  service->start();
  server->getAdvertising()->start();
  Serial.println("[OK] BLE advertising as '" + bleName + "'");

  // Aizu — shared feedback arbiter: brings up the onboard NeoPixel (+ power pin)
  // and the BOOT-button gesture layer. Sole writer of the pixel; sources post
  // cues, they never paint. With no source posting it just renders Idle.
  Aizu.begin();
  Serial.println("[OK] Aizu feedback arbiter (NeoPixel)");

  // ── Tertiary info pane (SH1106 OLED): pre-probe 0x3C (the same non-fatal ack test the
  // ToF mux uses) so a missing panel warns cleanly, then draw the SHINTAI-OS wordmark —
  // a first bring-up flare. Nothing else writes the panel yet, so the splash persists.
  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission() == 0 && oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledPresent = true;   // SWITCHCAPVCC drives the internal charge pump (the missing piece)
    renderPane(PANE_HOME);   // the identity splash is page 0 — draw it as the boot screen
    Serial.println("[OK] SSD1306 OLED tertiary pane @ 0x3D");
  } else {
    Serial.println("[WARN] SSD1306 OLED not found @ 0x3D — tertiary pane disabled");
  }

  // APDS9960 gesture sensor — the pane's swipe input. The gesture engine needs
  // proximity enabled to feed it. Presence-gated: absent -> the pane still shows and
  // auto-refreshes, just with no swipe paging. begin() reads the ID register, so it's
  // a real probe. Defaults suit hand swipes at ~10-20 cm; tune gain on-wrist if needed.
  apdsPresent = apds.begin();
  if (apdsPresent) {
    apds.enableProximity(true);
    apds.enableGesture(true);
    Serial.println("[OK] APDS9960 gesture sensor @ 0x39 (swipe to page the pane)");
  } else {
    Serial.println("[WARN] APDS9960 not found @ 0x39 — pane swipe paging disabled");
  }

  // Andon — the LED-matrix lantern (output surface, no telemetry). Presence-gated
  // like every device: absent -> the panel stays dark, nothing else changes. The
  // Modulino matrix ships at 0x39 (which the APDS9960 above already owns), so it is
  // readdressed once to 0x3F on the bench before it goes on the shared bus.
  andonBegin();

  // Nesshi — subscribe to Aizu's BOOT HOLD gesture (AZ-9): hold to read the surface
  // temp at the centre of the thermal FOV as a colour; a double-hold reads the
  // scene's hottest point. A short CLICK still toggles Aizu's mute (no collision).
  Aizu.onHold(nesshiOnHold);
  Serial.println("[OK] Nesshi heat-sight (hold BOOT to read surface temp)");
  Serial.println(imuPresent ? "[OK] Hokan step-reckoning (pedometer + fall SOS)"
                            : "[WARN] Hokan disabled — no IMU (steps stay 0)");
  Serial.println("Output: 'h'=human  'c'=CSV  'b'=both (current: both)");
  Serial.println("=============================\n");
}

// Zanshin rear-field read — the sole VL53L5CX read site (replaces the two arc reads).
// select-before-touch: zanshinSelect (ch0, or no-op if direct), pull a fresh 8x8 frame and derive the
// nearer target in each half into cachedMmL/cachedMmR (so alert/Distance/CSV are unchanged).
// Holds the last L/R between frames; flags zanshinFresh so the reflex can notify the grid.
void serviceZanshinField() {
  if (!hasZanshin) return;
  zanshinSelect();
  uint8_t ready = 0;
  if (zanshinTof.vl53l5cx_check_data_ready(&ready) != 0 || !ready) return;
  zanshinTof.vl53l5cx_get_ranging_data(&zanshinResults);
  zanshinDeriveLR(zanshinResults.distance_mm, zanshinResults.target_status, &cachedMmL, &cachedMmR);
  zanshinFresh = true;
}

// Kehai-Hikari reflex tick — the sole rear-ToF read site. Self-rate-limits to REFLEX_MS.
// Reads the VL53L5CX field into cachedMmL/cachedMmR, posts the distance band for the NEARER
// half as an Aizu cue (or clears it), and — while a central is subscribed — notifies the
// depth grid. Never prints to the telemetry stream, never touches lastUpdate or the flash gate.
void serviceReflex() {
  if (millis() - lastReflexMs < REFLEX_MS) return;
  lastReflexMs = millis();

  zanshinFresh = false;
  serviceZanshinField();

  // Post the band (Approach/Reflex) for the nearer half, or withdraw (-> Aizu Idle).
  KehaiCue kc = kehaiCueFor(nearerMm(cachedMmL, cachedMmR), NEAR_MM, FAR_MM);
  if (kc.post) Aizu.postCue(AIZU_KEHAI, kc.priority, kc.colour, kc.motion, KEHAI_MAX_AGE_MS);
  else         Aizu.clearCue(AIZU_KEHAI);

  // Zanshin depth grid over BLE — only on a fresh frame while a central is subscribed.
  if (zanshinFresh && deviceConnected && zanshinGridCccd != nullptr &&
      zanshinGridCccd->getNotifications()) {
    uint8_t grid[ZANSHIN_GRID_BYTES];
    zanshinPackGrid(zanshinResults.distance_mm, zanshinResults.target_status, grid);
    zanshinGridChar->setValue(grid, ZANSHIN_GRID_BYTES);
    zanshinGridChar->notify();
    gridNotifyCount++;
  }
}

// Metsuke thermal tick — the sole MLX90640 read site. Self-rate-limits to
// THERMAL_MS (~2 Hz), reads a fresh frame into the cached thermalFrame (telemetry
// consumes it for its summary temps), then downsamples + packs + notifies the
// binary grid IF a central is subscribed. Never prints to the telemetry stream,
// never touches lastUpdate, the CSV, or the flash gate (the grid is BLE-live-only).
void serviceThermal() {
  if (!thermalPresent) return;
  if (millis() - lastThermalMs < THERMAL_MS) return;
  lastThermalMs = millis();

  thermalFrameOk = (mlx.getFrame(thermalFrame) == 0);
  if (!thermalFrameOk) return;

  // Emit only while the grid CCCD is subscribed (AC-4) — no central, no airtime.
  // On a fresh (re)subscribe, reset the denoise filter so a new viewer doesn't see the
  // previous scene bleed in through the EMA (the filter only advances while streaming).
  static bool gridWasSubscribed = false;
  bool gridSubscribed = deviceConnected && thermalGridCccd != nullptr &&
                        thermalGridCccd->getNotifications();
  if (!gridSubscribed) { gridWasSubscribed = false; return; }
  if (!gridWasSubscribed) { metsukeFilterReset(&metsukeFilter); gridWasSubscribed = true; }

  // Build the 32x24 grid, then STAGE it for paced chunked TX (serviceThermalTx) rather
  // than bursting all METSUKE_CHUNKS notifications here — the burst overruns the BLE tx
  // path (dropped/torn chunks -> a glitchy panel with a garbage min/max header). A new
  // frame supersedes any still-unsent chunks (the consumer drops the partial by frame_seq).
  if (!metsukePackGridFiltered(thermalFrame, &metsukeFilter, metsukeGrid)) return;  // all-NaN -> skip
  metsukeFrameSeq++;
  metsukeChunksLeft = METSUKE_CHUNKS;
  gridNotifyCount++;
}

// Paced thermal-grid chunk sender: emits ONE staged chunk per METSUKE_CHUNK_TX_MS so a
// frame's chunks don't burst (which the BLE stack drops/tears into a glitchy panel).
// Called every loop() iteration; a no-op unless serviceThermal has staged a frame.
void serviceThermalTx() {
  if (metsukeChunksLeft <= 0) return;
  if (!deviceConnected || thermalGridCccd == nullptr || !thermalGridCccd->getNotifications()) {
    metsukeChunksLeft = 0;   // central gone mid-frame — abandon the rest
    return;
  }
  if ((uint32_t)(millis() - metsukeLastChunkMs) < METSUKE_CHUNK_TX_MS) return;
  metsukeLastChunkMs = millis();

  int idx = METSUKE_CHUNKS - metsukeChunksLeft;   // send 0,1,2,3 in order
  uint8_t chunk[METSUKE_CHUNK_BYTES];
  metsukeChunk(metsukeGrid, metsukeFrameSeq, idx, chunk);
  thermalGridChar->setValue(chunk, METSUKE_CHUNK_BYTES);
  thermalGridChar->notify();
  metsukeChunksLeft--;
}

// Nesshi's HOLD subscriber (registered with Aizu.onHold in setup). Aizu owns the
// button and emits the primitives HOLD_START/HOLD_END (AZ-9); Nesshi composes them
// into its two reads: a plain hold measures the CENTRE (spot); two holds in quick
// succession — a "double-hold" (ND-3) — put the SECOND hold into scene mode (the
// scene's hottest point). Runs inside Aizu.tick(); only flips flags + clears the
// cue on release, so it never blocks. The band re-seeds each hold (nesshiBandIdx
// = -1) so the first frame snaps to the true colour.
void nesshiOnHold(AizuGesture ev) {
  if (ev == AIZU_HOLD_START) {
    uint32_t now  = millis();
    nesshiScene   = (nesshiLastHoldEndMs != 0) &&
                    (uint32_t)(now - nesshiLastHoldEndMs) < NESSHI_DOUBLE_HOLD_MS;
    nesshiBandIdx = -1;                 // snap to the true band on this hold's first frame
    nesshiHeld    = true;
  } else {                              // AIZU_HOLD_END
    nesshiHeld          = false;
    nesshiLastHoldEndMs = millis();
    Aizu.clearCue(AIZU_NESSHI);         // release -> LED falls back to whatever's underneath (AC-5)
  }
}

// Nesshi tick — posts the temperature cue while BOOT is held. Not held -> nothing
// (the cue was cleared on HOLD_END). No MLX90640 -> a distinct "no sensor" cue so
// the hold still gives feedback (integration point 5). Otherwise it reduces the
// cached thermal frame to the one number this read needs — the centre pixel (spot,
// same index as the telemetry thermalCtr) or the hottest valid pixel (scene) — and
// posts the hysteretic band's colour. The frame is NOT read here (serviceThermal is
// the sole read site); Nesshi only consumes the cache. Never prints, never touches
// lastUpdate, the telemetry cadence, BLE, or the flash gate.
void serviceNesshi() {
  if (!nesshiHeld) return;

  if (!thermalPresent) {                        // hold works, but there's nothing to read
    NesshiCue nc = nesshiNoSensorCue();
    Aizu.postCue(AIZU_NESSHI, nc.priority, nc.colour, nc.motion, NESSHI_MAX_AGE_MS);
    return;
  }
  if (!thermalFrameOk) return;                  // no valid frame yet — hold the last cue (maxAge covers a brief gap)

  float readC;
  if (nesshiScene) {                            // scene: the hottest point anywhere in view
    float mx = -INFINITY;
    for (int i = 0; i < 768; i++) { float v = thermalFrame[i]; if (!isnan(v) && v > mx) mx = v; }
    if (mx == -INFINITY) return;                // all-NaN frame — skip this refresh
    readC = mx;
  } else {                                      // spot: the centre of the FOV — what you're pointing at
    readC = thermalFrame[(12 * 32) + 16];       // same centre index as the telemetry thermalCtr
    if (isnan(readC)) {                         // NaN centre -> frame mean (as the telemetry summary does)
      float sum = 0; int valid = 0;
      for (int i = 0; i < 768; i++) { float v = thermalFrame[i]; if (!isnan(v)) { sum += v; valid++; } }
      if (valid == 0) return;
      readC = sum / valid;
    }
  }

  nesshiBandIdx = nesshiStep(readC, nesshiBandIdx);   // hysteretic band (re-seeded at HOLD_START)
  NesshiCue nc  = nesshiCueFor(nesshiBandIdx);
  Aizu.postCue(AIZU_NESSHI, nc.priority, nc.colour, nc.motion, NESSHI_MAX_AGE_MS);
}

// Hokan step-reckoning tick — the fast IMU DSP loop (~50 Hz, self-rate-limited).
// Reads accel magnitude, feeds the pure step + fall detectors (HokanDsp.h), keeps
// the cumulative `steps` counter, and drives the latching fall SOS through Aizu.
// This is Hokan's "first on-device real-time DSP": the gait signal is faster than
// the 1.5 s telemetry cadence, so steps CANNOT be recovered from the CSV after the
// fact — they must be detected live here. Its own read site (register reads, no
// dataReady contention with the telemetry accel read). Never prints, never touches
// lastUpdate, the telemetry cadence, BLE, or the flash gate.
void serviceHokan() {
  if (!imuPresent) return;                        // no IMU -> steps stay 0, no falls (degrades)
  if (millis() - lastHokanMs < HOKAN_SAMPLE_MS) return;
  uint32_t now = millis();
  lastHokanMs = now;

  sensors_event_t a, g, t;
  imu.getEvent(&a, &g, &t);
  float mag = sqrtf(a.acceleration.x * a.acceleration.x
                  + a.acceleration.y * a.acceleration.y
                  + a.acceleration.z * a.acceleration.z);

  if (hokanStep.update(mag, now)) hokanSteps++;   // one cumulative step

  // Fall SOS: while latched (DOWN), re-post each tick so the ALERT cue never ages
  // out; clear it when the wearer gets up (RESOLVED). Aizu ranks it top-tier (AZ-11)
  // so it preempts everything but a Kehai Reflex, which it doesn't meaningfully
  // co-occur with. A mute gesture (Aizu CLICK) blanks it like any cue (AZ-8).
  HokanFallEvent fe = hokanFall.update(mag, now);
  if (hokanFall.down()) {
    HokanCue c = hokanFallCue();
    Aizu.postCue(AIZU_HOKAN, c.priority, c.colour, c.motion, HOKAN_SOS_MAX_AGE_MS);
  } else if (fe == HOKAN_FALL_RESOLVED) {
    Aizu.clearCue(AIZU_HOKAN);
  }
}

// Metsuke grid diagnostic (serial 'G'): is a central connected, did it subscribe
// to the grid CCCD, and are notifies actually going out? Splits "firmware not
// emitting" from "app not receiving" without needing the phone on USB.
void probeGrid() {
  Serial.printf("<<<GRID connected=%d thermalPresent=%d frameOk=%d cccd=%d subscribed=%d notifies=%lu>>>\n",
                deviceConnected ? 1 : 0, thermalPresent ? 1 : 0, thermalFrameOk ? 1 : 0,
                thermalGridCccd != nullptr ? 1 : 0,
                (thermalGridCccd && thermalGridCccd->getNotifications()) ? 1 : 0,
                (unsigned long)gridNotifyCount);
}

// ── Tertiary pane rendering ──────────────────────────────────────────────────────
// Each page draws from the `pane` snapshot (+ the already-global env state), so it can
// repaint from the fast loop section the instant a swipe changes the page.

// A little spinning wireframe cube, centred at (cx, cy) with half-size `s` px. Pure
// GFX lines — the display is mono, but the tumble reads as 3D on its own. The 8 unit
// corners are rotated by two millis()-driven angles (different rates -> a lazy tumble),
// orthographically projected, and joined by the 12 edges. Angle comes from millis(),
// so it advances every HOME redraw (OLED_ANIM_MS) with no extra state.
static void drawCube(int16_t cx, int16_t cy, float s) {
  float a = millis() * 0.0011f, b = millis() * 0.0008f;   // yaw / pitch — a slow, calm tumble
  float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b);
  int16_t px[8], py[8];
  for (int i = 0; i < 8; i++) {
    float x = (i & 1) ? 1.f : -1.f, y = (i & 2) ? 1.f : -1.f, z = (i & 4) ? 1.f : -1.f;
    float x1 =  x * ca + z * sa,  z1 = -x * sa + z * ca;    // rotate about Y (yaw)
    float y2 =  y * cb - z1 * sb;                           // rotate about X (pitch)
    px[i] = cx + (int16_t)(s * x1);
    py[i] = cy - (int16_t)(s * y2);
  }
  // 12 edges: corner pairs differing in exactly one axis bit (x, then y, then z).
  static const uint8_t E[12][2] = {
    {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7}
  };
  for (int e = 0; e < 12; e++)
    oled.drawLine(px[E[e][0]], py[E[e][0]], px[E[e][1]], py[E[e][1]], SSD1306_WHITE);
}

// Centre a string on the 128 px width via GFX text metrics.
static void oledCenter(const char* s, uint8_t size, int16_t y) {
  oled.setTextSize(size);
  int16_t x1, y1; uint16_t w, h;
  oled.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor((OLED_W - (int16_t)w) / 2, y);
  oled.print(s);
}

// A page header: title top-left, "n/N" top-right, a rule under. Returns content top y.
static int16_t oledHeader(const char* title, uint8_t page) {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(title);
  char idx[8];
  snprintf(idx, sizeof idx, "%u/%u", (unsigned)(page + 1), (unsigned)PANE_COUNT);
  int16_t x1, y1; uint16_t w, h;
  oled.getTextBounds(idx, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor(OLED_W - (int16_t)w, 0);
  oled.print(idx);
  oled.drawLine(0, 10, OLED_W - 1, 10, SSD1306_WHITE);
  return 14;
}

void renderPane(uint8_t page) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  char buf[24];
  switch (page) {
    case PANE_HOME:                             // identity splash (also the boot screen)
      oled.drawRect(0, 0, OLED_W, OLED_H, SSD1306_WHITE);
      oledCenter("SHINTAI-OS", 2, 3);
      oled.drawLine(14, 22, OLED_W - 14, 22, SSD1306_WHITE);
      drawCube(OLED_W / 2, 39, 7.0f);           // spinning cube in the lower splash zone
      oledCenter("field system", 1, 54);
      break;

    case PANE_NAV:
      oledHeader("NAV", page);
      if (pane.magOk) { snprintf(buf, sizeof buf, "%03.0f %s", pane.heading, pane.cardinal); oledCenter(buf, 2, 16); }
      else            oledCenter("no heading", 1, 20);
      oled.setTextSize(1);
      if (pane.gpsFix) {
        snprintf(buf, sizeof buf, "%.4f %.4f", pane.lat, pane.lon); oled.setCursor(0, 40); oled.print(buf);
        snprintf(buf, sizeof buf, "%.0fm  %d sat", pane.alt, pane.sats); oled.setCursor(0, 52); oled.print(buf);
      } else { oled.setCursor(0, 46); oled.print("GPS: no fix"); }
      break;

    case PANE_ENV:
      oledHeader("ENV", page);
      oled.setTextSize(1);
      if (bmeHasData) {
        snprintf(buf, sizeof buf, "%.1f hPa", bmePressure);       oled.setCursor(0, 16); oled.print(buf);
        snprintf(buf, sizeof buf, "%.1f kO gas", bmeGas / 1000.0); oled.setCursor(0, 26); oled.print(buf);
        snprintf(buf, sizeof buf, "%.1fC %.0f%%RH", bmeTemp, bmeHum); oled.setCursor(0, 36); oled.print(buf);
        const char* nose = kyukakuState.count < KYUKAKU_SETTLE_COUNT       ? "settling"
                         : ((int32_t)(kyukakuSpikeUntilMs - millis()) > 0) ? "SPIKE"
                         : kyukakuState.band == KYUKAKU_BAND_FOUL          ? "foul"
                         : kyukakuState.band == KYUKAKU_BAND_TAINT         ? "taint" : "clean";
        const char* wx = !kiatsuObs.spanning                     ? "fill"
                       : kiatsuObs.state == KIATSU_WX_STORM       ? "fall!"
                       : kiatsuObs.state == KIATSU_WX_FALLING     ? "fall" : "steady";
        snprintf(buf, sizeof buf, "nose:%s", nose); oled.setCursor(0, 50);  oled.print(buf);
        snprintf(buf, sizeof buf, "wx:%s", wx);     oled.setCursor(72, 50); oled.print(buf);
      } else oledCenter("no BME688", 1, 30);
      break;

    case PANE_PROX:
      oledHeader(pane.alert ? "PROX  *CLOSE*" : "PROX", page);
      oled.setTextSize(2);
      if (pane.mmL > 0) snprintf(buf, sizeof buf, "L%4dmm", pane.mmL); else snprintf(buf, sizeof buf, "L  --");
      oled.setCursor(0, 18); oled.print(buf);
      if (pane.mmR > 0) snprintf(buf, sizeof buf, "R%4dmm", pane.mmR); else snprintf(buf, sizeof buf, "R  --");
      oled.setCursor(0, 40); oled.print(buf);
      break;

    case PANE_THERM:
      oledHeader("THERMAL", page);
      if (pane.thermalOk) {
        snprintf(buf, sizeof buf, "%.1fC", pane.tCtr); oledCenter(buf, 2, 16);
        oled.setTextSize(1);
        snprintf(buf, sizeof buf, "scene %.0f-%.0fC", pane.tMin, pane.tMax); oled.setCursor(0, 40); oled.print(buf);
        snprintf(buf, sizeof buf, "hot +%.1fC", pane.hotDelta);              oled.setCursor(0, 52); oled.print(buf);
      } else oledCenter("no thermal", 1, 28);
      break;
  }
  oled.display();
}

// ── Enrai AS3935 tuning ── one apply path, driven by the writable Lightning Control char
// AND the serial knobs. Distance is a coarse storm-FRONT energy estimate; OUTDOOR gain +
// clearing statistics spread it, spike rejection + tune-cap refine it. See specs/zokyo/enrai.md.
void applyEnraiConfig() {
  if (!hasEnrai) return;
  enrai.setIndoorOutdoor(enraiOutdoor ? ENRAI_OUTDOOR : ENRAI_INDOOR);
  enrai.setNoiseLevel(2);
  enrai.watchdogThreshold(enraiWatchdog);
  enrai.spikeRejection(enraiSpike);
  enrai.tuneCap(enraiTuneCap);
  enrai.maskDisturber(false);
}

void enraiConfigStr(char *buf, size_t n) {
  snprintf(buf, n, "gain=%s spike=%u wdog=%u tune=%u",
           enraiOutdoor ? "out" : "in", enraiSpike, enraiWatchdog, enraiTuneCap);
}

// Apply one control token (from a BLE write or a serial key), then notify the new config back.
void applyEnraiCommand(const char *cmd) {
  if (!hasEnrai) return;
  if      (!strcmp(cmd, "gain"))   enraiOutdoor = !enraiOutdoor;
  else if (!strcmp(cmd, "spike+")) { if (enraiSpike    < 11) enraiSpike++; }
  else if (!strcmp(cmd, "spike-")) { if (enraiSpike    > 1)  enraiSpike--; }
  else if (!strcmp(cmd, "wdog+"))  { if (enraiWatchdog < 10) enraiWatchdog++; }
  else if (!strcmp(cmd, "wdog-"))  { if (enraiWatchdog > 1)  enraiWatchdog--; }
  else if (!strcmp(cmd, "tune+"))  { if (enraiTuneCap  < 15) enraiTuneCap++; }
  else if (!strcmp(cmd, "tune-"))  { if (enraiTuneCap  > 0)  enraiTuneCap--; }
  else if (!strcmp(cmd, "clear"))  { enrai.clearStatistics(true); }
  else return;                                       // unknown token — ignore
  applyEnraiConfig();
  char cfg[48]; enraiConfigStr(cfg, sizeof(cfg));
  Serial.printf("[enrai] %s\n", cfg);
  if (lightningCtrlChar) {
    lightningCtrlChar->setValue(cfg);
    if (deviceConnected) lightningCtrlChar->notify();
  }
}

// Enrai lightning poll — the sole AS3935 read site. No IRQ pin is wired, so we poll the
// interrupt-source register, but THROTTLED to ENRAI_POLL_MS: polling every loop iteration
// read-and-clears the register inside its ~2 ms post-event settle window and loses strikes
// (see ENRAI_POLL_MS). Disturbers/noise are tallied for the 'K' diagnostic but drive nothing;
// only a validated strike updates the snapshot + notifies. CSV logging reads it at its cadence.
void serviceEnrai() {
  if (!hasEnrai) return;
  if (millis() - lastEnraiMs < ENRAI_POLL_MS) return;
  lastEnraiMs = millis();
  uint8_t intVal = enrai.readInterruptReg();
  if (intVal == ENRAI_DISTURBER) { enraiDisturbers++; return; }
  if (intVal == ENRAI_NOISE)     { enraiNoise++; return; }
  if (intVal != ENRAI_LIGHTNING) return;                     // 0 = nothing latched
  lightningKm     = enrai.distanceToStorm();
  lightningEnergy = enrai.lightningEnergy();
  lightningStrikes++;
  if (deviceConnected && lightningChar) {
    char buf[48];
    snprintf(buf, sizeof(buf), "km=%d e=%lu n=%lu",
             lightningKm, (unsigned long)lightningEnergy, (unsigned long)lightningStrikes);
    lightningChar->setValue(buf);
    lightningChar->notify();
  }
}

void loop() {
  // Always read GPS in background
  GPS.read();
  if (GPS.newNMEAreceived()) GPS.parse(GPS.lastNMEA());

  // Live output-mode toggle from the host (Serial Monitor or logger)
  while (Serial.available()) {
    char cmd = Serial.read();
    if      (cmd == 'h' || cmd == 'H') outputMode = HUMAN;
    else if (cmd == 'c' || cmd == 'C') { outputMode = CSV;  csvHeaderPrinted = false; }
    else if (cmd == 'b' || cmd == 'B') { outputMode = BOTH; csvHeaderPrinted = false; }
    else if (cmd == 'L' || cmd == 'l') listLogs();
    else if (cmd == 'P' || cmd == 'p') { dumpAllLogs(); lastUpdate = millis(); }
    else if (cmd == 'E' || cmd == 'e') { eraseLogs();   lastUpdate = millis(); }
    else if (cmd == 'I' || cmd == 'i') { scanI2C();   lastUpdate = millis(); }
    else if (cmd == 'T' || cmd == 't') { probeTof();  lastUpdate = millis(); }
    else if (cmd == 'G' || cmd == 'g') { probeGrid(); lastUpdate = millis(); }
    else if (cmd == 'R' || cmd == 'r') {   // Bunshin: cycle this pod's role fwd<->aft
      podRole = (podRole == "fwd") ? "aft" : "fwd";
      prefs.putString("role", podRole);
      Serial.println("[role] set to '" + podRole +
                     "' — reboot to re-advertise as 'ShintaiOS-" + podRole + "'");
      lastUpdate = millis();
    }
    else if (cmd == 'M' || cmd == 'm') {   // print this board's BLE MAC (for :glass's hardcoded address)
      Serial.println("[ble] ShintaiOS-" + podRole + " MAC " + String(BLEDevice::getAddress().toString().c_str()));
      lastUpdate = millis();
    }
    // Enrai AS3935 tuning — same tokens as the writable Lightning Control char (app→board).
    else if (cmd == 'w') { applyEnraiCommand("wdog-");  lastUpdate = millis(); }   // watchdog down (more sensitive/distant)
    else if (cmd == 'W') { applyEnraiCommand("wdog+");  lastUpdate = millis(); }   // watchdog up (fewer disturbers)
    else if (cmd == 'o') { applyEnraiCommand("gain");   lastUpdate = millis(); }   // toggle INDOOR/OUTDOOR gain
    else if (cmd == 's') { applyEnraiCommand("spike-"); lastUpdate = millis(); }   // spike rejection down
    else if (cmd == 'S') { applyEnraiCommand("spike+"); lastUpdate = millis(); }   // spike rejection up
    else if (cmd == 'y') { applyEnraiCommand("tune-");  lastUpdate = millis(); }   // antenna tune-cap down
    else if (cmd == 'Y') { applyEnraiCommand("tune+");  lastUpdate = millis(); }   // antenna tune-cap up
    else if (cmd == 'x') { applyEnraiCommand("clear");  lastUpdate = millis(); }   // clear storm statistics
    else if (cmd == 'K' || cmd == 'k') {   // Enrai: live status — poll health (disturbers) + config
      char cfg[48]; enraiConfigStr(cfg, sizeof(cfg));
      Serial.printf("<<<ENRAI present=%d %s strikes=%lu disturbers=%lu noise=%lu last=%dkm/%lue>>>\n",
                    hasEnrai ? 1 : 0, cfg, (unsigned long)lightningStrikes,
                    (unsigned long)enraiDisturbers, (unsigned long)enraiNoise,
                    lightningKm, (unsigned long)lightningEnergy);
      lastUpdate = millis();
    }
  }

  // Kehai reflex (ToF -> Aizu cue) + Aizu render, both on their own fast clocks —
  // call every iteration, BEFORE the 1500 ms telemetry gate so neither is starved.
  // Each self-rate-limits and never touches lastUpdate, the telemetry stream, BLE,
  // or the flash-logging gate.
  serviceReflex();
  serviceThermal();   // Metsuke: ~2 Hz MLX read + stage 32x24 grid (sole thermal read site)
  serviceThermalTx(); // Metsuke: paced chunk TX — one chunk per ~30 ms, no burst
  serviceNesshi();    // Nesshi: while BOOT held, cached-frame surface temp -> Aizu cue
  serviceHokan();     // Hokan: fast IMU DSP -> cumulative steps + latching fall SOS
  serviceEnrai();     // Enrai: poll AS3935 -> last-strike snapshot + Lightning notify
  Aizu.tick();

  // ── Tertiary pane input + render (both on their own fast clocks, before the
  // telemetry gate so a swipe registers within ~50 ms, not on the 1500 ms cycle).
  // Gesture -> page change; then redraw on a change, or on the slow refresh for a
  // live page (HOME is static, so it only repaints when you land on it).
  if (apdsPresent && (uint32_t)(millis() - apdsLastPoll) >= APDS_POLL_MS) {
    apdsLastPoll = millis();
    uint8_t g = apds.readGesture();   // 0 = none; resolves once a swipe completes
    // Mapping calibrated on-glass: a physical L->R swipe should page FORWARD. If it
    // pages the wrong way (or the swipe axis reads as UP/DOWN), swap these directions.
    if      (g == APDS9960_RIGHT) { oledPage = (oledPage + 1) % PANE_COUNT;             oledDirty = true; }
    else if (g == APDS9960_LEFT)  { oledPage = (oledPage + PANE_COUNT - 1) % PANE_COUNT; oledDirty = true; }
    // APDS9960_UP / _DOWN reserved.
  }
  if (oledPresent) {
    uint32_t sinceDraw = (uint32_t)(millis() - oledLastDraw);
    // HOME animates the cube at ~20 fps; live pages refresh their data at 750 ms.
    bool refresh = (oledPage == PANE_HOME) ? (sinceDraw >= OLED_ANIM_MS)
                                           : (paneReady && sinceDraw >= OLED_REFRESH_MS);
    if (oledDirty || refresh) { renderPane(oledPage); oledLastDraw = millis(); oledDirty = false; }
  }

  // Andon LED-matrix flair — self-paced (~11 fps), before the telemetry gate so the
  // raindrops animate smoothly off the 1500 ms cycle. No-op when the panel is absent.
  andonService(millis());

  if (millis() - lastUpdate < UPDATE_MS) return;
  lastUpdate = millis();

  bool human = (outputMode == HUMAN || outputMode == BOTH);
  bool csv   = (outputMode == CSV   || outputMode == BOTH);

  // ── ToF ── both rear arcs serviced in the reflex tick (serviceReflex), not here —
  // telemetry consumes the cached ranges so the two consumers don't fight over
  // dataReady (Kehai-Hikari note 2). alert stays coherent with the Reflex band: both
  // key off the NEARER arc against NEAR_MM.
  int16_t mmL    = cachedMmL;
  int16_t mmR    = cachedMmR;
  int16_t mmNear = nearerMm(mmL, mmR);
  bool alertNow = (mmNear > 0 && mmNear <= NEAR_MM);

  // ── IMU + Magnetometer ── (each optional; zeroed when absent)
  sensors_event_t accel = {}, gyro = {}, temp = {}, magEvent = {};
  if (imuPresent) imu.getEvent(&accel, &gyro, &temp);
  if (magPresent) mag.getEvent(&magEvent);

  float heading = 0;
  const char* cardinal = "";
  if (magPresent) {
    heading = atan2(magEvent.magnetic.y, magEvent.magnetic.x) * 180.0 / M_PI;
    if (heading < 0) heading += 360.0;

    if      (heading <  22.5 || heading >= 337.5) cardinal = "N";
    else if (heading <  67.5) cardinal = "NE";
    else if (heading < 112.5) cardinal = "E";
    else if (heading < 157.5) cardinal = "SE";
    else if (heading < 202.5) cardinal = "S";
    else if (heading < 247.5) cardinal = "SW";
    else if (heading < 292.5) cardinal = "W";
    else                      cardinal = "NW";
  }

  // ── GPS snapshot ──
  bool  gpsFix   = GPS.fix;
  float gpsLat   = GPS.latitudeDegrees;
  float gpsLon   = GPS.longitudeDegrees;
  float gpsAlt   = GPS.altitude;
  float gpsSpeed = GPS.speed * 1.852;
  int   gpsSats  = (int)GPS.satellites;

  // ── Thermal camera (MLX90640): surface temps across the scene ── the frame is
  // read in serviceThermal (the sole read site, ~2 Hz for Metsuke); telemetry
  // consumes the cached frame so the two don't fight over getFrame. thermalFrameOk
  // is the last read's success; the summary logic below is otherwise unchanged.
  bool  thermalOk  = thermalPresent && thermalFrameOk;
  float thermalMin = 0, thermalMax = 0, thermalCtr = 0, thermalMean = 0;
  if (thermalOk) {
    thermalMin = INFINITY;
    thermalMax = -INFINITY;
    float sum = 0; int valid = 0;
    for (int i = 0; i < 768; i++) {        // skip occasional NaN pixels
      float v = thermalFrame[i];
      if (isnan(v)) continue;
      if (v < thermalMin) thermalMin = v;
      if (v > thermalMax) thermalMax = v;
      sum += v; valid++;
    }
    if (valid == 0) {
      thermalOk = false;                   // whole frame bad — treat as no reading
    } else {
      thermalMean = sum / valid;
      thermalCtr  = thermalFrame[(12 * 32) + 16];
      if (isnan(thermalCtr)) thermalCtr = thermalMean;
    }
  }

  // ── SCD-40 (climate): CO2 / ambient air temp / humidity, refreshes ~every 5s ──
  if (scdPresent) {
    bool ready = false;
    if (scd4x.getDataReadyStatus(ready) == 0 && ready) {
      uint16_t co2; float t, rh;
      if (scd4x.readMeasurement(co2, t, rh) == 0 && co2 != 0) {
        scdCo2 = co2; scdTemp = t; scdHum = rh; scdHasData = true;
        kankiBandIdx = kankiStep(scdCo2, kankiBandIdx);   // Kanki: advance the hysteretic band
      }
    }
  }

  // ── Kanki (換気): express the CO2 band as an Aizu Ambient cue — the arbiter
  // animates the calm colour breathe/pulse on the shared pixel. POST, never paint.
  // Fresh posts nothing and falls through to Aizu's Idle green (== the Fresh
  // state). Warming up (no reading yet) shows the distinct dim white/blue cue.
  // Standalone-safe: no SCD -> post nothing, arbiter falls through to whatever
  // else is active (or Idle). Derives entirely from the existing scdCo2 — no
  // contract change, no telemetry disturbance.
  if (scdPresent) {
    KankiCue kc = scdHasData ? kankiCueFor(kankiBandIdx) : kankiWarmupCue();
    if (kc.post) Aizu.postCue(AIZU_KANKI, kc.priority, kc.colour, kc.motion, KANKI_MAX_AGE_MS);
    else         Aizu.clearCue(AIZU_KANKI);
  } else {
    Aizu.clearCue(AIZU_KANKI);
  }

  // ── BME688 (env): pressure + gas resistance, plus the authoritative air T/RH.
  // performReading() fires the gas heater and blocks ~150 ms — fine at UPDATE_MS.
  if (bmePresent && bme.performReading()) {
    bmeTemp     = bme.temperature;
    bmeHum      = bme.humidity;
    bmePressure = bme.pressure / 100.0;   // Pa → hPa
    bmeGas      = bme.gas_resistance;      // ohms
    bmeHasData  = true;
  }

  // ── Kyūkaku (嗅覚): fold the fresh gas reading into the rolling baseline/ratio and
  // express it as an Aizu cue. A fresh Spike latches a short hold (the startle); while
  // held we post the violet→red pulse, otherwise the resolved Foul/Taint band (Clean
  // posts nothing and falls through to Idle). Settling (~120 s) and a missing BME both
  // post nothing. POST, never paint. Standalone-safe. No contract change.
  if (bmePresent && bmeHasData) {
    KyukakuObs ko = kyukakuStepState(kyukakuState, bmeGas, bmeHum);
    if (ko.spike) kyukakuSpikeUntilMs = millis() + KYUKAKU_SPIKE_HOLD_MS;
    bool spikeActive = ko.seeded && (int32_t)(kyukakuSpikeUntilMs - millis()) > 0;
    if (spikeActive) {
      KyukakuCue c = kyukakuSpikeCue();
      Aizu.postCue(AIZU_KYUKAKU, c.priority, c.colour, c.motion, KYUKAKU_MAX_AGE_MS);
    } else if (ko.seeded) {
      KyukakuCue c = kyukakuCueFor(ko.band);
      if (c.post) Aizu.postCue(AIZU_KYUKAKU, c.priority, c.colour, c.motion, KYUKAKU_MAX_AGE_MS);
      else        Aizu.clearCue(AIZU_KYUKAKU);
    } else {
      Aizu.clearCue(AIZU_KYUKAKU);   // settling — quiet, Aizu Idle signals "alive"
    }
  } else {
    Aizu.clearCue(AIZU_KYUKAKU);
  }

  // ── Kiatsu (気圧): sub-sample pressure into the 3 h ring every ~45 s, recompute the
  // weather tendency, and re-post the resolved cue every loop (so it stays live between
  // the slow pushes). Falling -> cyan breathe, Falling-fast -> deeper/slower; Steady and
  // a still-filling ring both post nothing (Aizu Idle). POST, never paint. Standalone-safe.
  if (bmePresent && bmeHasData) {
    uint32_t now = millis();
    if (kiatsuLastPushMs == 0 || (uint32_t)(now - kiatsuLastPushMs) >= KIATSU_SUBSAMPLE_MS) {
      kiatsuLastPushMs = now;
      kiatsuObs = kiatsuPush(kiatsuState, bmePressure);
    }
    if (kiatsuObs.spanning) {
      KiatsuCue c = kiatsuCueFor(kiatsuObs.state);
      if (c.post) Aizu.postCue(AIZU_KIATSU, c.priority, c.colour, c.motion, KIATSU_MAX_AGE_MS);
      else        Aizu.clearCue(AIZU_KIATSU);
    } else {
      Aizu.clearCue(AIZU_KIATSU);   // ring still filling toward 3 h — quiet
    }
  } else {
    Aizu.clearCue(AIZU_KIATSU);
  }

  // Air temp / humidity are shared semantic slots: BME688 when present (faster,
  // no photoacoustic self-heat offset), else SCD-40, else blank. See CONTRACT.md.
  bool  airHasData = bmeHasData || scdHasData;
  float airTemp    = bmeHasData ? bmeTemp : scdTemp;
  float airHum     = bmeHasData ? bmeHum  : scdHum;

  // Hot-spot: how much hotter the warmest point is than the ambient air.
  // Baseline is the resolved air temp when available, else the coldest scene pixel.
  float hotspotBase  = airHasData ? airTemp : thermalMin;
  float hotspotDelta = thermalOk ? (thermalMax - hotspotBase) : 0;
  bool  hotspot      = thermalOk && hotspotDelta >= 5.0;

  // ── Tertiary-pane snapshot ── copy this cycle's fields (otherwise local to loop())
  // into the global the swipe renderer reads, so a page change repaints instantly with
  // the latest data instead of waiting for the next telemetry cycle.
  pane.magOk = magPresent; pane.heading = heading;
  strncpy(pane.cardinal, cardinal, sizeof(pane.cardinal) - 1);
  pane.cardinal[sizeof(pane.cardinal) - 1] = '\0';
  pane.gpsFix = gpsFix; pane.lat = gpsLat; pane.lon = gpsLon; pane.alt = gpsAlt; pane.sats = gpsSats;
  pane.mmL = mmL; pane.mmR = mmR; pane.alert = alertNow;
  pane.thermalOk = thermalOk; pane.tCtr = thermalCtr; pane.tMin = thermalMin;
  pane.tMax = thermalMax; pane.hotDelta = hotspotDelta;
  paneReady = true;
  if (oledPage != PANE_HOME) oledDirty = true;   // live page: repaint with fresh data

  // ── Human-readable output ──
  if (human) {
    Serial.println("-----------------------------");
    Serial.println("DISTANCE : L " + (mmL > 0 ? String(mmL) + " mm" : String("no reading"))
                 + "   R " + (mmR > 0 ? String(mmR) + " mm" : String("no reading")));
    if (alertNow) Serial.println("ALERT    : *** OBJECT TOO CLOSE ***");
    if (magPresent)
      Serial.println("HEADING  : " + String(heading, 1) + "° " + cardinal);
    if (imuPresent)
      Serial.println("ACCEL    : X:" + String(accel.acceleration.x, 1)
                   + " Y:" + String(accel.acceleration.y, 1)
                   + " Z:" + String(accel.acceleration.z, 1) + " m/s²");
    if (gpsFix) {
      Serial.println("GPS      : " + String(gpsLat, 5) + "," + String(gpsLon, 5)
                   + " " + String(gpsAlt, 0) + "m " + String(gpsSpeed, 1) + "km/h");
      Serial.println("GPS SATS : " + String(gpsSats));
    } else {
      Serial.println("GPS      : no fix");
    }
    if (thermalOk) {
      Serial.println("THERMAL  : Center " + String(thermalCtr, 1) + "°C (" + String(cToF(thermalCtr), 1) + "°F)"
                   + " | Scene " + String(thermalMin, 1) + "–" + String(thermalMax, 1) + "°C ("
                   + String(cToF(thermalMin), 1) + "–" + String(cToF(thermalMax), 1) + "°F)");
      if (hotspot) {
        Serial.println("HEAT     : warm object in view — hottest " + String(thermalMax, 1) + "°C ("
                     + String(cToF(thermalMax), 1) + "°F), +" + String(hotspotDelta, 1) + "°C over ambient");
      }
    }
    if (scdHasData) {
      Serial.println("CLIMATE  : " + String(scdTemp, 1) + "°C (" + String(cToF(scdTemp), 1) + "°F) air, "
                   + String(scdHum, 0) + "%RH, " + String(scdCo2) + " ppm CO2");
    } else if (scdPresent) {
      Serial.println("CLIMATE  : warming up…");
    }
    if (bmeHasData) {
      Serial.println("ENV      : " + String(bmePressure, 1) + " hPa, "
                   + String(bmeGas / 1000.0, 1) + " kΩ gas, "
                   + String(bmeTemp, 1) + "°C air, " + String(bmeHum, 0) + "%RH");
      // Kyūkaku readout (on-wrist tuning): ratio vs baseline + the resolved state.
      const char* nose = kyukakuState.count < KYUKAKU_SETTLE_COUNT             ? "settling"
                       : ((int32_t)(kyukakuSpikeUntilMs - millis()) > 0)       ? "SPIKE"
                       : kyukakuState.band == KYUKAKU_BAND_FOUL                ? "foul"
                       : kyukakuState.band == KYUKAKU_BAND_TAINT               ? "taint"
                                                                              : "clean";
      Serial.println("NOSE     : r=" + String(kyukakuState.lastRatio, 2)
                   + " (base " + String(kyukakuState.baseline / 1000.0, 1) + " kΩ) " + nose);
      // Kiatsu readout (on-wrist tuning): the 3 h tendency + resolved weather, or the
      // fill state while the ring is still spanning up to WX_WINDOW_H.
      if (kiatsuObs.spanning) {
        const char* wx = kiatsuObs.state == KIATSU_WX_STORM   ? "FALLING FAST"
                       : kiatsuObs.state == KIATSU_WX_FALLING  ? "falling"
                                                               : "steady";
        Serial.println("BARO     : Δ" + String(kiatsuObs.delta, 2) + " hPa/3h — " + wx);
      } else {
        float fillH = (KIATSU_RING_N - kiatsuState.count) * (KIATSU_SUBSAMPLE_MS / 1000.0) / 3600.0;
        Serial.println("BARO     : trend filling (" + String(fillH, 1) + " h to go)");
      }
    }
    Serial.println();
  }

  // ── CSV row: built once, persisted to flash (untethered) and/or streamed ──
  {
    String row;
    row.reserve(240);            // one alloc up front (rows run ~150–175 chars);
                                 // avoids ~40 incremental reallocs/row of heap churn
    row += lastUpdate;
    row += ',';  row += (mmL > 0 ? String(mmL) : String(""));
    row += ',';  row += (mmR > 0 ? String(mmR) : String(""));
    row += ',';  row += (alertNow ? '1' : '0');
    row += ',';  row += (magPresent ? String(heading, 1) : String(""));
    row += ',';  row += cardinal;   // empty when magnetometer absent
    row += ',';  row += (imuPresent ? String(accel.acceleration.x, 2) : String(""));
    row += ',';  row += (imuPresent ? String(accel.acceleration.y, 2) : String(""));
    row += ',';  row += (imuPresent ? String(accel.acceleration.z, 2) : String(""));
    row += ',';  row += (gpsFix ? '1' : '0');
    row += ',';  row += (gpsFix ? String(gpsLat, 5)   : String(""));
    row += ',';  row += (gpsFix ? String(gpsLon, 5)   : String(""));
    row += ',';  row += (gpsFix ? String(gpsAlt, 1)   : String(""));
    row += ',';  row += (gpsFix ? String(gpsSpeed, 1) : String(""));
    row += ',';  row += (gpsFix ? String(gpsSats)     : String(""));
    row += ',';  row += (thermalOk ? String(thermalMin, 1)  : String(""));
    row += ',';  row += (thermalOk ? String(thermalCtr, 1)  : String(""));
    row += ',';  row += (thermalOk ? String(thermalMax, 1)  : String(""));
    row += ',';  row += (thermalOk ? String(thermalMean, 1) : String(""));
    row += ',';  row += (thermalOk ? String(hotspotDelta, 1): String(""));
    row += ',';  row += (scdHasData ? String(scdCo2)        : String(""));
    row += ',';  row += (airHasData ? String(airTemp, 1)    : String(""));
    row += ',';  row += (airHasData ? String(airHum, 1)     : String(""));
    row += ',';  row += (bmeHasData ? String(bmePressure, 1): String(""));
    row += ',';  row += (bmeHasData ? String(bmeGas, 0)     : String(""));
    row += ',';  row += (imuPresent ? String(hokanSteps)    : String(""));   // Hokan cumulative steps
    row += ',';  row += (hasEnrai ? String(lightningKm)      : String(""));  // Enrai last-strike km (0=none)
    row += ',';  row += (hasEnrai ? String(lightningEnergy)  : String(""));  // Enrai last-strike energy
    row += ',';  row += (hasEnrai ? String(lightningStrikes) : String(""));  // Enrai cumulative strikes
    row += ',';  row += podRole;   // Bunshin: which pod (fwd/aft) produced this row

    // Persist to onboard flash only while untethered (no USB host). close() on a
    // row cadence (and on host-connect, below) commits the directory entry so a
    // field power cut can't orphan the file; reopen in APPEND so we never
    // truncate. Loss on a cut is bounded to the last FLASH_COMMIT_ROWS rows.
    bool untethered = !Serial;
    if (fsReady && untethered) {
      if (!logFile) logFile = FFat.open(logPath, FILE_APPEND);   // (re)open at end — NOT FILE_WRITE
      if (logFile) {
        logFile.println(row);
        if (++rowsSinceCommit >= FLASH_COMMIT_ROWS) {
          logFile.close();          // f_close → directory entry + FAT flushed; reopened lazily
          rowsSinceCommit = 0;
        } else {
          logFile.flush();          // push data sectors out between commits
        }
      }
    } else if (fsReady && !untethered && logFile) {
      logFile.close();              // host just connected: commit + release so it's pullable now
      rowsSinceCommit = 0;
    }

    // Stream over Serial when a host asked for CSV (header first).
    if (csv) {
      if (!csvHeaderPrinted) {
        Serial.println(CSV_HEADER);
        csvHeaderPrinted = true;
      }
      Serial.println(row);
    }
  }

  // ── BLE notify (independent of output mode) ──
  if (deviceConnected) {
    // Both rear arcs in one string char (like Accel's "X:.. Y:.. Z:.."); an arc with
    // no target is "--". Consumers split on "L:"/"R:" — see CONTRACT.md.
    String distStr = "L:" + (mmL > 0 ? String(mmL) : String("--"))
                   + " R:" + (mmR > 0 ? String(mmR) : String("--")) + " mm";
    distanceChar->setValue(distStr.c_str());
    distanceChar->notify();

    if (alertNow && !alertSent) {
      alertChar->setValue("CLOSE");
      alertChar->notify();
      alertSent = true;
    }
    // Re-arm whenever we're not currently too-close — including a no-reading
    // (mm == -1), so a fresh incursion after a dropout always re-alerts. (A
    // strict > FAR_MM release would latch-stick the alert in open space.)
    if (!alertNow) alertSent = false;

    if (magPresent) {
      String headingStr = String(heading, 1) + "° " + cardinal;
      headingChar->setValue(headingStr.c_str());
      headingChar->notify();
    }

    if (imuPresent) {
      String accelStr = "X:" + String(accel.acceleration.x, 1)
                      + " Y:" + String(accel.acceleration.y, 1)
                      + " Z:" + String(accel.acceleration.z, 1);
      accelChar->setValue(accelStr.c_str());
      accelChar->notify();
    }

    if (gpsFix) {
      String gpsStr = String(gpsLat, 5) + "," + String(gpsLon, 5)
                    + " " + String(gpsAlt, 0) + "m " + String(gpsSpeed, 1) + "km/h";
      gpsChar->setValue(gpsStr.c_str());
      gpsChar->notify();
    }
    if (thermalOk) {
      String thermalStr = "Ctr:" + String(thermalCtr, 1)
                        + " Min:" + String(thermalMin, 1)
                        + " Max:" + String(thermalMax, 1) + "C";
      thermalChar->setValue(thermalStr.c_str());
      thermalChar->notify();
    }
    if (scdHasData) {
      String climateStr = String(scdTemp, 1) + "C "
                        + String(scdHum, 0) + "%RH "
                        + String(scdCo2) + "ppm";
      climateChar->setValue(climateStr.c_str());
      climateChar->notify();
    }
    if (bmeHasData) {
      String envStr = String(bmePressure, 1) + "hPa "
                    + String(bmeGas, 0) + "ohm "
                    + String(bmeTemp, 1) + "C "
                    + String(bmeHum, 0) + "%RH";
      environmentChar->setValue(envStr.c_str());
      environmentChar->notify();
    }
    // Hokan (歩勘): "steps heading cadence" — the live PDR breadcrumb the apps
    // integrate into a dead-reckoned mini-map. Space-separated so the consumer just
    // splits; heading is the same value reported to the Heading char (0 if no mag).
    if (imuPresent) {
      String hokanStr = String(hokanSteps) + " " + String(heading, 1)
                      + " " + String(hokanStep.cadenceSpm(millis()));
      hokanChar->setValue(hokanStr.c_str());
      hokanChar->notify();
    }
  }
}