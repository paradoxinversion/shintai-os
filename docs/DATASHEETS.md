# Datasheets & reference

Canonical documentation for every component in `firmware/shintai-os/shintai-os.ino`.

We **do not** commit the PDF datasheets themselves — they're copyrighted by their
manufacturers and not licensed for redistribution. Keep local copies (for offline /
bench reference) under `docs/datasheets/`, which is gitignored. This file is the
version-controlled pointer to the authoritative sources.

For firmware work, the **library API** is usually the surface that matters more than
the raw datasheet — the "Arduino library" column is what the sketch actually calls.

## Board

| Part | MCU | Library / header | Product page | Reference |
|------|-----|------------------|--------------|-----------|
| Adafruit QT Py ESP32-S3 (no PSRAM) | ESP32-S3 | Arduino-ESP32 core | https://www.adafruit.com/product/5426 | https://learn.adafruit.com/adafruit-qt-py-esp32-s3 · [ESP32-S3 datasheet (Espressif)](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) |

## Sensors (I²C)

| Part | Measures | Arduino library / header | Product page | Manufacturer datasheet |
|------|----------|--------------------------|--------------|------------------------|
| ST **VL53L4CX** | Time-of-flight distance | STM32duino `VL53L4CX` (`vl53l4cx_class.h`) — https://github.com/stm32duino/VL53L4CX | https://www.adafruit.com/product/5425 | https://www.st.com/en/imaging-and-photonics-solutions/vl53l4cx.html |
| ST **LSM6DSOX** | 6-DoF accel + gyro | `Adafruit_LSM6DSOX` — https://github.com/adafruit/Adafruit_LSM6DS | https://www.adafruit.com/product/4438 | https://www.st.com/en/mems-and-sensors/lsm6dsox.html |
| ST **LIS3MDL** | 3-axis magnetometer (heading) | `Adafruit_LIS3MDL` — https://github.com/adafruit/Adafruit_LIS3MDL | https://www.adafruit.com/product/4438 | https://www.st.com/en/mems-and-sensors/lis3mdl.html |
| **Adafruit Mini GPS PA1010D** (MT3333, I²C 0x10) | GPS fix / NMEA | `Adafruit_GPS` — https://github.com/adafruit/Adafruit_GPS | https://www.adafruit.com/product/4415 | https://learn.adafruit.com/adafruit-mini-gps-pa1010d-module · [PA1010D datasheet](https://cdn-shop.adafruit.com/product-files/4415/4415_PA1010D-Datasheet-v.05.pdf) |
| Melexis **MLX90640** | 32×24 thermal camera (768 px) | `Adafruit_MLX90640` — https://github.com/adafruit/Adafruit_MLX90640 | https://www.adafruit.com/product/4407 | https://www.melexis.com/en/product/MLX90640/Far-Infrared-Thermal-Sensor-Array |
| Sensirion **SCD-40** | CO₂ / temp / humidity | `Sensirion I2C SCD4x` (`SensirionI2cScd4x.h`) — https://github.com/Sensirion/arduino-i2c-scd4x | https://www.adafruit.com/product/5187 | https://sensirion.com/products/catalog/SCD40 |

## Notes

- **VL53L4CX** and **SCD-40** are driven by the *manufacturer's* libraries (ST
  STM32duino, Sensirion), **not** Adafruit's — check those repos for API and examples.
- I²C addresses, timing budgets, and per-sensor quirks live in the manufacturer
  datasheets above; the `*Present` presence-gating in the sketch mirrors the wiring,
  not the datasheet.
- To keep local PDFs: drop them in `docs/datasheets/` (gitignored). Suggested naming:
  `<part>.pdf`, e.g. `docs/datasheets/vl53l4cx.pdf`.
