-- Progress database created automatically at /sdcard/fsrs_progress.db
-- The word database remains read-only at /sdcard/database/oxford.db.

PRAGMA journal_mode=PERSIST;
PRAGMA synchronous=FULL;
PRAGMA temp_store=MEMORY;
PRAGMA user_version=1;

CREATE TABLE IF NOT EXISTS cards (
    id INTEGER PRIMARY KEY,
    word_id INTEGER NOT NULL,
    mode INTEGER NOT NULL,              -- 1 EN->CN, 2 CN->EN, 3 dictation
    state INTEGER NOT NULL DEFAULT 0,    -- 0 New, 1 Learning, 2 Review, 3 Relearning
    due INTEGER NOT NULL DEFAULT 0,
    stability REAL NOT NULL DEFAULT 0,
    difficulty REAL NOT NULL DEFAULT 0,
    elapsed_days REAL NOT NULL DEFAULT 0,
    scheduled_days REAL NOT NULL DEFAULT 0,
    reps INTEGER NOT NULL DEFAULT 0,
    lapses INTEGER NOT NULL DEFAULT 0,
    last_review INTEGER NOT NULL DEFAULT 0,
    enabled INTEGER NOT NULL DEFAULT 1,
    created_at INTEGER NOT NULL,
    UNIQUE(word_id, mode)
);
CREATE INDEX IF NOT EXISTS idx_cards_due ON cards(enabled, state, due);
CREATE INDEX IF NOT EXISTS idx_cards_word ON cards(word_id);

CREATE TABLE IF NOT EXISTS started_words (
    word_id INTEGER PRIMARY KEY,
    cefr TEXT NOT NULL,
    introduced_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_started_day ON started_words(introduced_at);

CREATE TABLE IF NOT EXISTS word_status (
    word_id INTEGER PRIMARY KEY,
    cefr TEXT NOT NULL,
    recognition_ready INTEGER NOT NULL DEFAULT 0,
    dictation_unlocked INTEGER NOT NULL DEFAULT 0,
    mastered INTEGER NOT NULL DEFAULT 0,
    updated_at INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_word_status_level ON word_status(cefr, mastered);

CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS daily_stats (
    day_start INTEGER PRIMARY KEY,
    reviews INTEGER NOT NULL DEFAULT 0,
    new_words INTEGER NOT NULL DEFAULT 0,
    correct INTEGER NOT NULL DEFAULT 0,
    again INTEGER NOT NULL DEFAULT 0
);

-- Created unconditionally from the first schema version. Rows are only
-- inserted when settings.write_review_log is enabled.
CREATE TABLE IF NOT EXISTS review_log (
    id INTEGER PRIMARY KEY,
    card_id INTEGER NOT NULL,
    word_id INTEGER NOT NULL,
    mode INTEGER NOT NULL,
    rating INTEGER NOT NULL,
    ts INTEGER NOT NULL,
    state_before INTEGER,
    state_after INTEGER,
    stability_before REAL,
    stability_after REAL,
    difficulty_before REAL,
    difficulty_after REAL,
    elapsed_days REAL,
    scheduled_days REAL
);
CREATE INDEX IF NOT EXISTS idx_review_log_card ON review_log(card_id, ts);

