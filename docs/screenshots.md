# Screenshots

The firmware includes a developer screenshot shortcut. On any main screen,
press `Ctrl+S` on the Tab5 Keyboard. A 24-bit BMP is written to:

```text
/sdcard/screenshots/
```

Capture these views:

1. Home dashboard
2. English-to-Chinese card before reveal
3. English-to-Chinese card after reveal with rating buttons
4. Chinese-to-English card after reveal
5. Dictation screen with letter-count blanks
6. Settings screen

If dictation is not unlocked yet on a fresh profile, use a copy/backup of the
progress database and run:

```bash
python3 tools/prepare_demo_dictation.py /Volumes/TFCARD
```

That seeds one demo word with an unlocked dictation card. Restore
`fsrs_progress.db.bak` after the screenshot. Do not use this helper on a real
study profile.

Copy the raw BMP files into `docs/images/raw/`, then convert them:

```bash
python3 tools/prepare_screenshots.py
```

The converter writes README-ready PNG files under `docs/images/`. Raw BMP
files are ignored by Git. Review every converted image before committing it.
In particular, do not publish revealed definitions or examples copied from a
third-party dictionary unless you have redistribution rights. Use the
synthetic `database/sample.db` for public demo cards.

