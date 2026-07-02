#include <Wire.h>
#include <vl53l4cx_class.h>
#include <Adafruit_LSM6DSOX.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_GPS.h>
#include <Adafruit_MLX90640.h>
#include <SensirionI2cScd4x.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <FFat.h>
#include <Preferences.h>
#include <math.h>

// BLE UUIDs
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define DISTANCE_UUID       "abcd1234-ab12-ab12-ab12-abcdef123456"
#define ALERT_UUID          "abcd5678-ab12-ab12-ab12-abcdef123456"
#define HEADING_UUID        "abcd9012-ab12-ab12-ab12-abcdef123456"
#define ACCEL_UUID          "abcdef12-ab12-ab12-ab12-abcdef123456"
#define GPS_UUID            "abcd3456-ab12-ab12-ab12-abcdef123456"
#define THERMAL_UUID        "abcd6789-ab12-ab12-ab12-abcdef123456"
#define CLIMATE_UUID        "abcdba98-ab12-ab12-ab12-abcdef123456"

const int NEAR_MM  = 200;
const int FAR_MM   = 2000;   // reserved: graded-proximity HUD threshold, not yet
                             // wired in-loop — see specs/zokyo/kehai-hikari.md
const int UPDATE_MS = 1500;  // update interval

VL53L4CX sensor(&Wire, -1);
Adafruit_LSM6DSOX imu;
Adafruit_LIS3MDL  mag;
Adafruit_GPS GPS(&Wire);
Adafruit_MLX90640 mlx;
SensirionI2cScd4x scd4x;   // SCD-40: CO2 + ambient air temp + humidity (I2C 0x62)

float thermalFrame[32 * 24];

BLECharacteristic *distanceChar;
BLECharacteristic *alertChar;
BLECharacteristic *headingChar;
BLECharacteristic *accelChar;
BLECharacteristic *gpsChar;
BLECharacteristic *thermalChar;
BLECharacteristic *climateChar;

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
  "hotspot_delta,co2_ppm,air_temp_c,humidity_pct";

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
  delay(100);

  // ToF — non-fatal: warn and continue if absent (mirrors the other sensors).
  // InitSensor() boots the device + DataInit over I2C, so its status is a
  // reliable presence probe; gate reads on it so an absent ToF can't spin.
  sensor.begin();
  sensor.VL53L4CX_Off();
  tofPresent = (sensor.InitSensor(0x29) == VL53L4CX_ERROR_NONE);
  if (tofPresent) {
    sensor.VL53L4CX_StartMeasurement();
    Serial.println("[OK] VL53L4CX ToF");
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
    mlx.setRefreshRate(MLX90640_2_HZ);
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

  // BLE
  BLEDevice::init("ShintaiOS");
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

  service->start();
  server->getAdvertising()->start();
  Serial.println("[OK] BLE advertising as 'ShintaiOS'");
  Serial.println("Output: 'h'=human  'c'=CSV  'b'=both (current: both)");
  Serial.println("=============================\n");
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
  }

  if (millis() - lastUpdate < UPDATE_MS) return;
  lastUpdate = millis();

  bool human = (outputMode == HUMAN || outputMode == BOTH);
  bool csv   = (outputMode == CSV   || outputMode == BOTH);

  // ── ToF ── (optional; gated so an absent sensor doesn't poll I2C every loop)
  int16_t mm = -1;
  if (tofPresent) {
    uint8_t dataReady = 0;
    sensor.VL53L4CX_GetMeasurementDataReady(&dataReady);
    if (dataReady) {
      VL53L4CX_MultiRangingData_t data;
      sensor.VL53L4CX_GetMultiRangingData(&data);
      for (int i = 0; i < data.NumberOfObjectsFound; i++) {
        if (data.RangeData[i].RangeStatus == 0) {
          mm = data.RangeData[i].RangeMilliMeter;
          break;
        }
      }
      sensor.VL53L4CX_ClearInterruptAndStartMeasurement();
    }
  }
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

  // ── Thermal camera (MLX90640): surface temps across the scene ──
  bool  thermalOk  = thermalPresent && (mlx.getFrame(thermalFrame) == 0);
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
      }
    }
  }

  // Hot-spot: how much hotter the warmest point is than the ambient air.
  // Baseline is the SCD-40 air temp when available, else the coldest scene pixel.
  float hotspotBase  = scdHasData ? scdTemp : thermalMin;
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
    Serial.println();
  }

  // ── CSV row: built once, persisted to flash (untethered) and/or streamed ──
  {
    String row;
    row.reserve(220);            // one alloc up front (rows run ~130–150 chars);
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
    row += ',';  row += (scdHasData ? String(scdTemp, 1)    : String(""));
    row += ',';  row += (scdHasData ? String(scdHum, 1)     : String(""));

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
  }
}