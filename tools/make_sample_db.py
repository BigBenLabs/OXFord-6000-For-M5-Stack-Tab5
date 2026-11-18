#!/usr/bin/env python3
"""Create a small, publicly redistributable words DB for UI development."""

from __future__ import annotations

import pathlib
import sqlite3


ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "database" / "sample.db"

WORDS = [
    (0, "apple", "noun", "a1", "/ˈæp.əl/", "/ˈæp.əl/",
     "a round fruit with red or green skin and firm white flesh",
     "苹果", "She ate an apple after lunch.", "", ""),
    (1, "book", "noun", "a1", "/bʊk/", "/bʊk/",
     "a set of printed pages held together inside a cover",
     "书；书籍", "This book is easy to read.", "", ""),
    (2, "music", "noun", "a1", "/ˈmjuː.zɪk/", "/ˈmjuː.zɪk/",
     "sounds arranged in a pleasing or expressive way",
     "音乐", "We listened to music in the garden.", "", ""),
    (3, "river", "noun", "a2", "/ˈrɪv.ər/", "/ˈrɪv.ɚ/",
     "a natural wide stream of water flowing to the sea",
     "河流", "The river runs through the old town.", "", ""),
    (4, "bright", "adjective", "a2", "/braɪt/", "/braɪt/",
     "full of light or shining strongly",
     "明亮的", "The room was bright and quiet.", "", ""),
    (5, "abandon", "verb", "b2", "/əˈbæn.dən/", "/əˈbæn.dən/",
     "to leave somebody or something with no intention of returning",
     "抛弃；遗弃", "They had to abandon the plan.", "", ""),
    (6, "curious", "adjective", "b2", "/ˈkjʊə.ri.əs/", "/ˈkjʊr.i.əs/",
     "wanting to know or learn about something",
     "好奇的", "The child was curious about the machine.", "", ""),
    (7, "journey", "noun", "b1", "/ˈdʒɜː.ni/", "/ˈdʒɝː.ni/",
     "an act of travelling from one place to another",
     "旅程；旅行", "The journey took three hours.", "", ""),
]


def main() -> None:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists():
        OUTPUT.unlink()
    with sqlite3.connect(OUTPUT) as db:
        db.execute(
            """
            CREATE TABLE words (
                id INTEGER PRIMARY KEY,
                word TEXT NOT NULL,
                type TEXT,
                cefr TEXT,
                phon_br TEXT,
                phon_n_am TEXT,
                definition TEXT,
                chinese_definition TEXT,
                example TEXT,
                uk TEXT,
                us TEXT
            )
            """
        )
        db.execute("CREATE INDEX idx_words_word ON words(word)")
        db.executemany("INSERT INTO words VALUES(?,?,?,?,?,?,?,?,?,?,?)", WORDS)
    print(f"created {OUTPUT} with {len(WORDS)} sample words")


if __name__ == "__main__":
    main()

