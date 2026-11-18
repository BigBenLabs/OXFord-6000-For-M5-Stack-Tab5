# Contributing

Contributions are welcome.

## Development setup

- ESP-IDF 5.5.4
- ESP32-P4 target
- M5Stack Tab5 hardware for display, touch, keyboard, and audio validation
- Python 3 for the sample database and screenshot tools

```bash
source ~/.espressif/tools/activate_idf_v5.5.4.sh
cd firmware
idf.py set-target esp32p4
idf.py build
```

## Data policy

Do not submit:

- `database/oxford.db`
- Oxford definitions, examples, or word-list exports
- UK/US MP3 audio copied from the source dataset
- `data_processing/`, `audio_副本/`, or `database_副本/`

Keep `database/sample.db` synthetic and redistributable.

## Pull requests

Please describe:

- the behavior changed
- the hardware used for testing
- serial boot logs for hardware-related fixes
- whether the change affects FSRS state or existing progress databases

For UI changes, attach a screenshot generated with `Ctrl+S` and converted to
PNG.

