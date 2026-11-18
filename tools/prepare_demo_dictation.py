#!/usr/bin/env python3
"""Prepare one demo word with an unlocked dictation card for screenshots.

Use a copy of the TF card or back up fsrs_progress.db first. This tool is a
development helper and must not be used on a real study profile.
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import sqlite3
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("card_root", help="mounted TF card root, e.g. /Volumes/TFCARD")
    parser.add_argument("--word", default="music", help="word to use for the demo")
    parser.add_argument("--no-backup", action="store_true", help="do not create fsrs_progress.db.bak")
    args = parser.parse_args()

    root = pathlib.Path(args.card_root)
    words_db = root / "database" / "oxford.db"
    progress_db = root / "fsrs_progress.db"
    if not words_db.exists():
        raise SystemExit(f"missing word database: {words_db}")
    if not progress_db.exists():
        raise SystemExit(
            f"missing progress database: {progress_db}\n"
            "Launch the app once first so it creates the schema."
        )
    if not args.no_backup:
        shutil.copy2(progress_db, progress_db.with_suffix(".db.bak"))

    with sqlite3.connect(words_db) as words:
        row = words.execute(
            "SELECT id, cefr FROM words WHERE lower(word)=lower(?) LIMIT 1",
            (args.word,),
        ).fetchone()
        if row is None:
            row = words.execute(
                "SELECT id, cefr FROM words WHERE lower(cefr)='a1' ORDER BY id LIMIT 1"
            ).fetchone()
    if row is None:
        raise SystemExit("no word found")
    word_id, cefr = row

    now = int(time.time())
    future = now + 7 * 86400
    with sqlite3.connect(progress_db) as db:
        for mode in (1, 2):
            db.execute(
                """
                INSERT INTO cards
                    (word_id,mode,state,due,stability,difficulty,elapsed_days,
                     scheduled_days,reps,lapses,last_review,enabled,created_at)
                VALUES (?,?,2,?,30,5,1,30,3,0,?,1,?)
                ON CONFLICT(word_id,mode) DO UPDATE SET
                    state=2,due=excluded.due,stability=30,difficulty=5,
                    elapsed_days=1,scheduled_days=30,reps=3,lapses=0,
                    last_review=excluded.last_review,enabled=1
                """,
                (word_id, mode, future, now, now),
            )
        db.execute(
            """
            INSERT INTO cards
                (word_id,mode,state,due,stability,difficulty,elapsed_days,
                 scheduled_days,reps,lapses,last_review,enabled,created_at)
            VALUES (?,3,0,0,0,0,0,0,0,0,0,1,?)
            ON CONFLICT(word_id,mode) DO UPDATE SET
                state=0,due=0,stability=0,difficulty=0,elapsed_days=0,
                scheduled_days=0,reps=0,lapses=0,last_review=0,enabled=1
            """,
            (word_id, now),
        )
        db.execute(
            "INSERT OR REPLACE INTO started_words(word_id,cefr,introduced_at) VALUES(?,?,?)",
            (word_id, cefr, now),
        )
        db.execute(
            """
            INSERT INTO word_status
                (word_id,cefr,recognition_ready,dictation_unlocked,mastered,updated_at)
            VALUES (?,?,1,1,0,?)
            ON CONFLICT(word_id) DO UPDATE SET
                recognition_ready=1,dictation_unlocked=1,mastered=0,updated_at=excluded.updated_at
            """,
            (word_id, cefr, now),
        )

    print(f"prepared {args.word!r} (word_id={word_id}) in {progress_db}")
    print("restore fsrs_progress.db.bak after taking screenshots")


if __name__ == "__main__":
    main()

