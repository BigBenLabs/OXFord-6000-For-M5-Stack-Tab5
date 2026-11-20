# Tab5 FSRS firmware

ESP-IDF 5.5.4 / ESP32-P4 C++ firmware for the M5Stack Tab5.

The build intentionally selects ESP32-P4 revisions below v3.0
(`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`, minimum v1.0), because the Tab5
currently reports revision v1.3.

It also enables FAT long filenames and UTF-8 API paths
(`CONFIG_FATFS_LFN_HEAP=y`), uses one DPI frame buffer on ST7121 (the official
Tab5 configuration), and gives the main task a 24 KB stack for SQLite plus
board services.

## Build

```bash
source ~/.espressif/tools/activate_idf_v5.5.4.sh
cd firmware
idf.py set-target esp32p4   # only needed after deleting sdkconfig/build
export ESP_IDF_VERSION=5.5
export IDF_COMPONENT_STORAGE_URL=https://components-file.espressif.com.cn/
export IDF_COMPONENT_CHECK_NEW_VERSION=0
idf.py build
```

The explicit `ESP_IDF_VERSION` selects ESP-WiFi-Remote's 5.5 Kconfig set.
`IDF_COMPONENT_STORAGE_URL` selects the China component CDN, and disabling
the version check avoids an upstream registry metadata incompatibility in
ESP-Hosted 1.4.0. The first build downloads managed components. The SQLite
component is vendored under `components/esp_sqlite3` because ESP-IDF 5.5.4
with GCC 14 requires a release-mode assert fix for the SQLite amalgamation.

## Main components

- `components/m5stack_tab5`: official M5Stack Tab5 BSP, with an ES7210 constant typo corrected.
- `components/esp_lcd_st7121`: official ST7121 display driver.
- `components/esp_sqlite3`: SQLite 3.54.1 + ESP32 VFS, pinned locally.
- `main/fsrs.cpp`: FSRS-6 fixed default parameters and scheduler.
- `main/database.cpp`: words/progress DB, card dispatch, proficiency gates.
- `main/keyboard.cpp`: Tab5 Keyboard character mode over I2C1.
- `main/audio_service.cpp`: ES8388 playback of UK/US MP3 files from SD.
- `main/power_service.cpp`: IP2326 charge enable and INA226 battery monitor.
- `main/wifi_service.cpp`: first-run WiFi scan, NVS credentials, and airplane mode.
- `main/ui.cpp`: 1280x720 dark green LVGL interface.
- `main/fonts/font_zh_28.c`: generated Noto Sans CJK SC + Noto Sans Latin/IPA subset covering the word DB, phonetics, and UI.
- `main/fonts/LICENSE_NotoSansCJK.txt`: SIL OFL 1.1 license for the embedded font subset.

## Regenerate Chinese font

See `tools/generate_font.py`. It requires `lv_font_conv` and the OFL-licensed Noto Sans CJK SC font.

