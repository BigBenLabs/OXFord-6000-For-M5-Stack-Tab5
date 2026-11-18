#!/usr/bin/env python3
"""Convert raw device BMP screenshots to README-ready PNG files."""

from __future__ import annotations

import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]
RAW = ROOT / "docs" / "images" / "raw"
OUT = ROOT / "docs" / "images"


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    files = sorted(RAW.glob("*.bmp"))
    if not files:
        raise SystemExit(f"no BMP files found in {RAW}")

    for index, source in enumerate(files, start=1):
        destination = OUT / f"{index:02d}-{source.stem.replace('_', '-')}.png"
        subprocess.run(
            [
                "sips",
                "-s",
                "format",
                "png",
                str(source),
                "--out",
                str(destination),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        print(f"{source.name} -> {destination.relative_to(ROOT)}")


if __name__ == "__main__":
    main()

