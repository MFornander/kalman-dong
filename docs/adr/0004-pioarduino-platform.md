# PlatformIO with the pioarduino platform, not the official one

We build with PlatformIO Core and the community pioarduino `platform-espressif32` (pinned to release `55.03.312-1`, Arduino core 3.3.12), not the official `platformio/platform-espressif32`. As of 2026-10 the official platform had only just reached Arduino core 3.x (v8.0.0, days old) and has no board definition for the XIAO ESP32-S3 Plus, which Seeed supports only on core 3.x; pioarduino ships `seeed_xiao_esp32_s3_plus`. PlatformIO was chosen over arduino-cli because one `platformio.ini` gives both the device build and a `native` environment for host unit tests.

Revisit when the official platform has matured and gained the Plus board; switching should only touch `platformio.ini`.

## Considered Options

- **arduino-cli + Makefile host tests** — workable, but two build systems and manual library pinning.
- **ESP-IDF with Arduino as a component** — heavier, no testing advantage.
- **Wokwi simulation in CI** — skipped for v1: needs an account token and has no APA102, LSM6DS3 or ST7789 parts.
