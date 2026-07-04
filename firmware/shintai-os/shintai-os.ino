#include <Wire.h>
#include <vl53l4cx_class.h>
#include <Adafruit_LSM6DSOX.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_GPS.h>
#include <Adafruit_MLX90640.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_BME680.h>
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
#include "ThermalGrid.h" // Metsuke thermal sight (specs/zokyo/metsuke.md): MLX frame -> packed 8x8 heat grid
#include "NesshiBand.h"  // Nesshi heat-sight (specs/zokyo/nesshi.md): hold BOOT -> surface temp -> Aizu cue
#include "HokanDsp.h"    // Hokan step-reckoning (specs/zokyo/hokan.md): fast IMU -> steps + fall SOS

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

const int NEAR_MM  = 200;
const int FAR_MM   = 2000;   // Kehai-Hikari Approach/Clear boundary (was reserved)
const int UPDATE_MS = 1500;  // telemetry (CSV/BLE) update interval

// Kehai-Hikari proximity reflex — a fast loop decoupled from the 1500 ms telemetry
// cadence. serviceReflex() services the ToF here, caches the range, and posts the
// distance band as an Aizu cue. The telemetry block consumes the cached range so
// the two never fight over the sensor's dataReady (Kehai-Hikari note 2).
const int      REFLEX_MS               = 120;    // reflex tick (spec: ~100-150 ms)
const uint32_t REFLEX_TIMING_BUDGET_US = 50000;  // lower ToF budget -> fresher range (D-4)
const uint32_t KEHAI_MAX_AGE_MS        = 500;    // Aizu drops the cue if we stop posting
int16_t       cachedMm     = -1;                 // latest ToF range; -1 = no target
unsigned long lastReflexMs = 0;

VL53L4CX sensor(&Wire, -1);
Adafruit_LSM6DSOX imu;
Adafruit_LIS3MDL  mag;
Adafruit_GPS GPS(&Wire);
Adafruit_MLX90640 mlx;
SensirionI2cScd4x scd4x;   // SCD-40: CO2 + ambient air temp + humidity (I2C 0x62)
Adafruit_BME680 bme;       // BME688: gas + pressure, plus authoritative air temp/RH (I2C 0x77)

float thermalFrame[32 * 24];

// Metsuke (目付) — live thermal grid over BLE. The MLX90640 frame is now serviced
// on its own ~2 Hz tick (serviceThermal), the single read site; the 1500 ms
// telemetry block consumes the cached frame for its summary temps, so the two
// never fight over getFrame (same pattern Kehai uses for the ToF). On each fresh
// frame Metsuke downsamples 32x24 -> 8x8, packs 68 bytes, and notifies the grid
// characteristic — gated on a subscribed central. NOTE: getFrame() blocks while
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
BLECharacteristic *thermalGridChar;   // Metsuke: binary 8x8 heat grid
BLECharacteristic *hokanChar;         // Hokan: "steps heading cadence" string (live PDR breadcrumb)
BLE2902           *thermalGridCccd = nullptr;   // its CCCD — emit only while subscribed
uint32_t           gridNotifyCount = 0;         // grid notifies sent (for the 'G' probe)

bool deviceConnected = false;
bool alertSent = false;
unsigned long lastUpdate = 0;

// Per-module presence — set at boot. Any sensor may be physically absent for a
// config test; we warn and continue rather than hang, then gate its reads and
// output below. (SCD-40 has its own scdPresent flag further down.)
bool tofPresent     = false;
bool imuPresent     = false;
bool magPresent     = false;
bool thermalPresent = false;

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

static inline float cToF(float c) { return c * 9.0 / 5.0 + 32.0; }

// Output mode — toggle live over Serial: send 'h' (human), 'c' (CSV only), 'b' (both).
// Default BOTH so the Python logger captures CSV rows while the terminal stays readable.
enum OutputMode { HUMAN, CSV, BOTH };
OutputMode outputMode = BOTH;
bool csvHeaderPrinted = false;

// CSV column order — shared by the Serial stream and the onboard flash log.
const char* CSV_HEADER =
  "timestamp_ms,distance_mm,alert,heading_deg,cardinal,"
  "accel_x,accel_y,accel_z,gps_fix,lat,lon,alt_m,speed_kmh,"
  "sats,thermal_min,thermal_ctr,thermal_max,thermal_mean,"
  "hotspot_delta,co2_ppm,air_temp_c,humidity_pct,pressure_hpa,gas_ohms,"
  "steps";   // Hokan: cumulative pedometer count (appended — CSV-half contract change)

// Onboard flash logging (FFat) — autonomous field capture, pulled over USB.
// Each power-up writes a new sequential file (/shtNNNN.csv). Rows are logged
// only while UNtethered (no USB host), so tethered live sessions are unaffected.
Preferences prefs;
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

// Scan the I2C bus and print every responding address. NOTE: the VL53L4CX (0x29)
// does NOT ACK a bare address-only scan while it's continuously ranging, so it
// will be MISSING here even when it's healthy — use 'T' (probeTof) as the
// authoritative ToF check, not this scan. A 0x29 absence here is only meaningful
// when the ToF also fails to init at boot (probeTof present=0).
void scanI2C() {
  Serial.println("<<<I2C scan>>>");
  int n = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("  0x%02X\n", a); n++; }
  }
  Serial.printf("  %d device(s) (expect ToF=0x29, IMU=0x6a, mag=0x1c/0x1e, GPS=0x10, thermal=0x33, SCD=0x62, BME=0x77)\n", n);
}

// Probe the VL53L4CX directly: report the boot presence flag, then poll for a
// fresh measurement and dump each object's RangeStatus + range. Distinguishes
// not-present (InitSensor failed) from not-ranging (dataReady never fires) from
// ranging-but-filtered (objects come back with RangeStatus != 0).
void probeTof() {
  Serial.printf("<<<TOF present=%d budget=%lu us>>>\n",
                tofPresent ? 1 : 0, (unsigned long)REFLEX_TIMING_BUDGET_US);
  if (!tofPresent) {
    Serial.println("  not initialized at boot — InitSensor(0x29) failed (check the I2C scan)");
    return;
  }
  for (int attempt = 0; attempt < 25; attempt++) {   // ~500 ms window
    uint8_t dr = 0;
    sensor.VL53L4CX_GetMeasurementDataReady(&dr);
    if (dr) {
      VL53L4CX_MultiRangingData_t d;
      sensor.VL53L4CX_GetMultiRangingData(&d);
      Serial.printf("  dataReady after %d ms: objects=%d\n", attempt * 20, d.NumberOfObjectsFound);
      for (int i = 0; i < d.NumberOfObjectsFound; i++) {
        Serial.printf("    [%d] status=%d range=%d mm\n",
                      i, d.RangeData[i].RangeStatus, d.RangeData[i].RangeMilliMeter);
      }
      sensor.VL53L4CX_ClearInterruptAndStartMeasurement();
      return;
    }
    delay(20);
  }
  Serial.println("  dataReady never asserted over ~500 ms — sensor present but not measuring");
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s)    { deviceConnected = true;  Serial.println(">> Phone connected"); }
  void onDisconnect(BLEServer *s) { deviceConnected = false; alertSent = false;
                                    Serial.println(">> Phone disconnected");
                                    s->startAdvertising(); }
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

  // ToF — non-fatal: warn and continue if absent (mirrors the other sensors).
  // InitSensor() boots the device + DataInit over I2C, so its status is a
  // reliable presence probe; gate reads on it so an absent ToF can't spin.
  sensor.begin();
  sensor.VL53L4CX_Off();
  tofPresent = (sensor.InitSensor(0x29) == VL53L4CX_ERROR_NONE);
  if (tofPresent) {
    // Favour reflex latency over the old slow cadence (Kehai-Hikari D-4) — a fresh
    // range then lands close to the reflex tick. Non-fatal: keep the default budget
    // if the device rejects this value.
    if (sensor.VL53L4CX_SetMeasurementTimingBudgetMicroSeconds(REFLEX_TIMING_BUDGET_US) != VL53L4CX_ERROR_NONE)
      Serial.println("[WARN] ToF timing-budget set failed — using default cadence");
    sensor.VL53L4CX_StartMeasurement();
    Serial.println("[OK] VL53L4CX ToF (reflex-tuned)");
  } else {
    Serial.println("[WARN] VL53L4CX not found — distance disabled");
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

  // BLE
  BLEDevice::init("ShintaiOS");
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

  service->start();
  server->getAdvertising()->start();
  Serial.println("[OK] BLE advertising as 'ShintaiOS'");

  // Aizu — shared feedback arbiter: brings up the onboard NeoPixel (+ power pin)
  // and the BOOT-button gesture layer. Sole writer of the pixel; sources post
  // cues, they never paint. With no source posting it just renders Idle.
  Aizu.begin();
  Serial.println("[OK] Aizu feedback arbiter (NeoPixel)");

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

// Kehai-Hikari reflex tick — the sole ToF read site. Self-rate-limits to REFLEX_MS.
// Services the sensor into cachedMm (holding the last range between fresh samples),
// then posts the distance band as an Aizu cue (or clears it when quiescent). Never
// prints to the telemetry stream, never touches lastUpdate or the flash gate.
void serviceReflex() {
  if (millis() - lastReflexMs < REFLEX_MS) return;
  lastReflexMs = millis();

  if (tofPresent) {
    uint8_t dataReady = 0;
    sensor.VL53L4CX_GetMeasurementDataReady(&dataReady);
    if (dataReady) {
      VL53L4CX_MultiRangingData_t data;
      sensor.VL53L4CX_GetMultiRangingData(&data);
      int16_t newMm = -1;
      for (int i = 0; i < data.NumberOfObjectsFound; i++) {
        if (data.RangeData[i].RangeStatus == 0) {
          newMm = data.RangeData[i].RangeMilliMeter;
          break;
        }
      }
      cachedMm = newMm;
      sensor.VL53L4CX_ClearInterruptAndStartMeasurement();
    }
  }

  // Post the band (Approach/Reflex) or withdraw (Clear/no-target -> Aizu Idle).
  KehaiCue kc = kehaiCueFor(cachedMm, NEAR_MM, FAR_MM);
  if (kc.post) Aizu.postCue(AIZU_KEHAI, kc.priority, kc.colour, kc.motion, KEHAI_MAX_AGE_MS);
  else         Aizu.clearCue(AIZU_KEHAI);
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
  if (!deviceConnected || thermalGridCccd == nullptr || !thermalGridCccd->getNotifications())
    return;

  uint8_t packed[METSUKE_GRID_BYTES];
  if (!metsukePackGrid(thermalFrame, packed)) return;   // all-NaN frame -> skip
  thermalGridChar->setValue(packed, METSUKE_GRID_BYTES);
  thermalGridChar->notify();
  gridNotifyCount++;
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
  }

  // Kehai reflex (ToF -> Aizu cue) + Aizu render, both on their own fast clocks —
  // call every iteration, BEFORE the 1500 ms telemetry gate so neither is starved.
  // Each self-rate-limits and never touches lastUpdate, the telemetry stream, BLE,
  // or the flash-logging gate.
  serviceReflex();
  serviceThermal();   // Metsuke: ~2 Hz MLX read + grid notify (sole thermal read site)
  serviceNesshi();    // Nesshi: while BOOT held, cached-frame surface temp -> Aizu cue
  serviceHokan();     // Hokan: fast IMU DSP -> cumulative steps + latching fall SOS
  Aizu.tick();

  if (millis() - lastUpdate < UPDATE_MS) return;
  lastUpdate = millis();

  bool human = (outputMode == HUMAN || outputMode == BOTH);
  bool csv   = (outputMode == CSV   || outputMode == BOTH);

  // ── ToF ── serviced in the reflex tick (serviceReflex), not here — telemetry
  // consumes the cached range so the two consumers don't fight over dataReady
  // (Kehai-Hikari note 2). alert stays coherent with the Reflex band: both key off
  // the same cached mm against NEAR_MM.
  int16_t mm = cachedMm;
  bool alertNow = (mm > 0 && mm <= NEAR_MM);

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

  // ── Human-readable output ──
  if (human) {
    Serial.println("-----------------------------");
    Serial.println("DISTANCE : " + (mm > 0 ? String(mm) + " mm" : String("no reading")));
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
    }
    Serial.println();
  }

  // ── CSV row: built once, persisted to flash (untethered) and/or streamed ──
  {
    String row;
    row.reserve(240);            // one alloc up front (rows run ~150–175 chars);
                                 // avoids ~40 incremental reallocs/row of heap churn
    row += lastUpdate;
    row += ',';  row += (mm > 0 ? String(mm) : String(""));
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
    String distStr = mm > 0 ? String(mm) + " mm" : String("no reading");
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