# Publishing this project on GitHub

## Recommended repository name

`tab5-fsrs`

Suggested description:

> Offline FSRS-6 vocabulary trainer for M5Stack Tab5 (ESP32-P4), with
> recognition, recall, dictation, pronunciation, and a physical keyboard.

Suggested topics:

`m5stack`, `tab5`, `esp32-p4`, `esp-idf`, `fsrs`, `flashcards`,
`vocabulary`, `lvgl`, `sqlite`, `embedded`

## 1. Choose the license

The repository includes an MIT `LICENSE` as the default for the original
application code. If you prefer an explicit patent grant, use Apache-2.0
instead. Keep `THIRD_PARTY_LICENSES.md` and the `NOTICE`/license files inside
vendored components.

The MIT license does **not** grant rights to the Oxford database, Oxford
definitions/examples, or their audio recordings. Do not publish those files.

## 2. Check what will be committed

The `.gitignore` excludes:

- `audio/`, `audio_副本/`
- `database/oxford.db`, `database_副本/`
- `data_processing/`
- `firmware/build/`, `firmware/managed_components/`
- `firmware/sdkconfig`
- `dist/`
- raw BMP screenshots

Initialize Git and inspect:

```bash
git init -b main
git add .
git status --short
git status --ignored --short | head -50
```

`database/sample.db` is intentionally included. `firmware/dependencies.lock`
and the vendored `firmware/components/esp_sqlite3` are intentionally included.

## 3. Add screenshots

Capture the screens on the device with `Ctrl+S`, copy the BMPs from
`/sdcard/screenshots/` to `docs/images/raw/`, then run:

```bash
python3 tools/prepare_screenshots.py
```

Replace the `SCREENSHOTS_START` block in `README.md` with the PNG files you
want on GitHub. Keep text out of the image crop if the screenshots contain
private study data.

The initial public gallery contains the home screen, the recognition-card
front, and the session summary. These views do not expose Oxford definitions,
examples, or audio. Before publishing a screenshot with revealed meanings,
run the app against `database/sample.db` renamed to `oxford.db` on a separate
TF card; if that is not possible, omit the screenshot. Cropping out only part
of a definition does not remove the redistribution concern.

## 4. Create the GitHub repository

Using GitHub CLI:

```bash
gh auth login
gh repo create tab5-fsrs --public --source . --remote origin --push
```

Or create an empty repository in the GitHub web UI and add it:

```bash
git remote add origin git@github.com:<YOUR_ACCOUNT>/tab5-fsrs.git
git push -u origin main
```

Do not initialize the GitHub repository with a generated README or `.gitignore`;
this local repository already has both.

## 5. Create the first release

The binary package is intentionally excluded from Git. Upload it as release
assets instead:

```text
dist/tab5-fsrs/bootloader.bin
dist/tab5-fsrs/partition-table.bin
dist/tab5-fsrs/tab5_fsrs.bin
dist/tab5-fsrs/tab5_fsrs_full.bin
dist/tab5-fsrs/flash.sh
dist/tab5-fsrs/README_烧录与TF卡.md
dist/tab5-fsrs/SHA256SUMS.txt
```

Create a tag:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Then create a GitHub Release named `v0.1.0` and attach those files. A release
is better than committing generated binaries because Git history stays small
and users can download a known build.

## 6. Repository settings

Enable:

- Issues
- Discussions (optional)
- Dependabot alerts
- Branch protection for `main`
- GitHub Actions

Add a short security note in the repository description if you later distribute
prebuilt firmware.

## 7. What not to claim

This project is not affiliated with or endorsed by Oxford University Press.
The sample DB uses synthetic entries. Do not describe the repository as
shipping the Oxford 6000 dataset unless you have explicit redistribution
permission.

