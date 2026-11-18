#!/usr/bin/env python3
"""Regenerate the LVGL Chinese subset from the word DB and UI sources."""

from __future__ import annotations

import os
import pathlib
import sqlite3
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[2]
MAIN = ROOT / "firmware" / "main"
DB = ROOT / "database" / "oxford.db"
FONT = pathlib.Path(
    os.environ.get(
        "NOTO_CJK_FONT",
        "/private/tmp/NotoSansCJKsc-Regular.otf",
    )
)
IPA_FONT = pathlib.Path(
    os.environ.get(
        "NOTO_IPA_FONT",
        "/private/tmp/NotoSans-Regular.ttf",
    )
)
CONVERTER = os.environ.get("LV_FONT_CONV", "lv_font_conv")


def main() -> None:
    chars: set[str] = set()
    with sqlite3.connect(DB) as connection:
        for row in connection.execute(
            "SELECT word,phon_br,phon_n_am,definition,chinese_definition,example FROM words"
        ):
            for value in row:
                if value:
                    chars.update(value)

    for path in [*MAIN.glob("*.cpp"), *MAIN.glob("*.h")]:
        chars.update(path.read_text(encoding="utf-8"))

    symbols = "".join(sorted(ch for ch in chars if ord(ch) >= 0x20))
    symbols_path = MAIN / "fonts" / "symbols.txt"
    symbols_path.write_text(symbols, encoding="utf-8")
    output = MAIN / "fonts" / "font_zh_28.c"

    subprocess.run(
        [
            CONVERTER,
            "--size",
            "28",
            "--bpp",
            "4",
            "--format",
            "lvgl",
            "--font",
            str(FONT),
            "-r",
            "0x20-0x7f",
            "--symbols",
            symbols,
            "--font",
            str(IPA_FONT),
            "-r",
            "0x014b",
            "-r",
            "0x0250-0x02af",
            "-r",
            "0x02b0-0x02ff",
            "-r",
            "0x0300-0x036f",
            "-r",
            "0x2009",
            "--lv-font-name",
            "font_zh_28",
            "--no-kerning",
            "-o",
            str(output),
        ],
        check=True,
    )
    print(f"generated {output} with {len(symbols)} symbols")


if __name__ == "__main__":
    main()

