# Word database

The repository does **not** include the Oxford word database or the Oxford
audio files. They may be subject to third-party rights and are intentionally
kept out of the source tree.

The application expects a SQLite database at:

```text
/database/oxford.db
```

## Required word table

The compatibility schema is:

```sql
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
);
CREATE INDEX idx_words_word ON words(word);
```

`cefr` should be one of `a1`, `a2`, `b1`, `b2`, `c1`. `uk` and `us` are
MP3 filenames, normally resolved under:

```text
/audio/uk_audio_split_24m/
/audio/us_audio_split_24m/
```

## Public sample

For development and screenshots, generate or copy [sample.db](sample.db) as
`oxford.db` on the TF card. The sample contains only a few synthetic entries
and is not connected to Oxford University Press.

```bash
python3 tools/make_sample_db.py
```

