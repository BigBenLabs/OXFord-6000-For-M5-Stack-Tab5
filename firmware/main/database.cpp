#include "database.h"

#include "clock_service.h"
#include "fsrs.h"

#include "esp_log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

constexpr const char *TAG = "database";
constexpr const char *WORDS_PATH = "/sdcard/database/oxford.db";
constexpr const char *PROGRESS_PATH = "/sdcard/fsrs_progress.db";
constexpr const char *LEVELS[] = {"a1", "a2", "b1", "b2", "c1"};

const char *text_column(sqlite3_stmt *stmt, int index)
{
    const unsigned char *text = sqlite3_column_text(stmt, index);
    return text ? reinterpret_cast<const char *>(text) : "";
}

std::string string_column(sqlite3_stmt *stmt, int index)
{
    return text_column(stmt, index);
}

int level_index(const std::string &level)
{
    for (int i = 0; i < 5; ++i) {
        if (level == LEVELS[i]) {
            return i;
        }
    }
    return 0;
}

std::string next_level(const std::string &level)
{
    int index = level_index(level) + 1;
    return index < 5 ? LEVELS[index] : "";
}

} // namespace

bool Database::exec(sqlite3 *db, const std::string &sql)
{
    char *message = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error_ = message ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        ESP_LOGE(TAG, "SQL failed: %s\n%s", error_.c_str(), sql.c_str());
        return false;
    }
    return true;
}

bool Database::begin()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = false;
    FILE *probe = std::fopen(WORDS_PATH, "rb");
    if (probe == nullptr) {
        error_ = "找不到 /sdcard/database/oxford.db，请检查 TF 卡目录";
        return false;
    }
    std::fclose(probe);

    esp_err_t err = esp_sqlite3_init();
    if (err != ESP_OK) {
        error_ = std::string("SQLite 初始化失败: ") + esp_err_to_name(err);
        return false;
    }
    esp_sqlite3_set_log_level(ESP_LOG_DEBUG);

    esp_sqlite3_config_t word_cfg;
    esp_sqlite3_config_init(&word_cfg);
    word_cfg.path = WORDS_PATH;
    word_cfg.read_only = true;
    word_cfg.create = false;
    word_cfg.cache_size_kib = 64;
    word_cfg.temp_store_memory = true;
    err = esp_sqlite3_open(&word_cfg, &words_db_);
    if (err != ESP_OK) {
        error_ = std::string("词库打开失败: ") + esp_err_to_name(err);
        return false;
    }
    words_ = esp_sqlite3_handle(words_db_);

    esp_sqlite3_config_t progress_cfg;
    esp_sqlite3_config_init(&progress_cfg);
    progress_cfg.path = PROGRESS_PATH;
    progress_cfg.read_only = false;
    progress_cfg.create = true;
    progress_cfg.cache_size_kib = 64;
    progress_cfg.temp_store_memory = true;
    progress_cfg.journal_mode = ESP_SQLITE3_JOURNAL_PERSIST;
    progress_cfg.synchronous = ESP_SQLITE3_SYNC_FULL;
    progress_cfg.busy_timeout_ms = 5000;
    err = esp_sqlite3_open(&progress_cfg, &progress_db_);
    if (err != ESP_OK) {
        error_ = std::string("进度库打开失败: ") + esp_err_to_name(err);
        return false;
    }
    progress_ = esp_sqlite3_handle(progress_db_);

    if (!create_schema()) {
        return false;
    }
    load_catalog();
    load_settings();
    ready_ = true;
    ESP_LOGI(TAG, "database ready: %zu words, %zu started",
             level_words_.empty() ? 0 : level_words_.at("a1").size() + level_words_.at("a2").size() +
                                            level_words_.at("b1").size() + level_words_.at("b2").size() +
                                            level_words_.at("c1").size(),
             started_words_.size());
    return true;
}

bool Database::create_schema()
{
    const char *schema = R"SQL(
PRAGMA journal_mode=PERSIST;
PRAGMA synchronous=FULL;
PRAGMA temp_store=MEMORY;
PRAGMA user_version=1;

CREATE TABLE IF NOT EXISTS cards (
    id INTEGER PRIMARY KEY,
    word_id INTEGER NOT NULL,
    mode INTEGER NOT NULL,
    state INTEGER NOT NULL DEFAULT 0,
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

-- The table exists from day one for future exports/migrations. Actual rows are
-- only written when write_review_log is enabled in settings.
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
)SQL";
    if (!exec(progress_, schema)) {
        return false;
    }
    seed_defaults();
    return true;
}

void Database::seed_defaults()
{
    const std::pair<const char *, const char *> defaults[] = {
        {"new_words_per_day", "15"},
        {"session_card_limit", "60"},
        {"audio_volume", "70"},
        {"recognition_gate_days", "21"},
        {"dictation_gate_days", "21"},
        {"level_unlock_ratio", "0.90"},
        {"write_review_log", "0"},
        {"unlocked_level", "a1"},
    };
    for (const auto &item : defaults) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(progress_, "INSERT OR IGNORE INTO settings(key,value) VALUES(?,?)",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, item.first, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, item.second, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }
}

void Database::load_catalog()
{
    level_words_.clear();
    level_cursor_.clear();
    for (const char *level : LEVELS) {
        level_words_[level] = {};
        level_cursor_[level] = 0;
    }

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(words_, "SELECT id, lower(cefr) FROM words ORDER BY id", -1,
                           &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const std::string level = string_column(stmt, 1);
            auto it = level_words_.find(level);
            if (it != level_words_.end()) {
                it->second.push_back(sqlite3_column_int64(stmt, 0));
            }
        }
    }
    sqlite3_finalize(stmt);

    started_words_.clear();
    stmt = nullptr;
    if (sqlite3_prepare_v2(progress_, "SELECT word_id FROM started_words", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            started_words_.insert(sqlite3_column_int64(stmt, 0));
        }
    }
    sqlite3_finalize(stmt);
}

void Database::load_settings()
{
    settings_ = AppSettings{};
    int value = 0;
    float fvalue = 0;
    if (get_setting_int("new_words_per_day", 15, value)) {
        settings_.new_words_per_day = std::clamp(value, 5, 30);
    }
    if (get_setting_int("session_card_limit", 60, value)) {
        settings_.session_card_limit = std::clamp(value, 20, 200);
    }
    if (get_setting_int("audio_volume", 70, value)) {
        settings_.audio_volume = std::clamp(value, 0, 100);
    }
    if (get_setting_float("recognition_gate_days", 21.0f, fvalue)) {
        settings_.recognition_gate_days = std::clamp(fvalue, 1.0f, 365.0f);
    }
    if (get_setting_float("dictation_gate_days", 21.0f, fvalue)) {
        settings_.dictation_gate_days = std::clamp(fvalue, 1.0f, 365.0f);
    }
    if (get_setting_float("level_unlock_ratio", 0.90f, fvalue)) {
        settings_.level_unlock_ratio = std::clamp(fvalue, 0.80f, 1.0f);
    }
    settings_.write_review_log = get_setting_int("write_review_log", 0, value) && value != 0;
    settings_.unlocked_level = get_setting_string("unlocked_level", "a1");
    if (level_words_.find(settings_.unlocked_level) == level_words_.end()) {
        settings_.unlocked_level = "a1";
    }
}

bool Database::get_setting_int(const char *key, int fallback, int &out)
{
    const std::string value = get_setting_string(key, std::to_string(fallback));
    char *end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

bool Database::get_setting_float(const char *key, float fallback, float &out)
{
    const std::string value = get_setting_string(key, std::to_string(fallback));
    char *end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str()) {
        return false;
    }
    out = parsed;
    return true;
}

std::string Database::get_setting_string(const char *key, const std::string &fallback)
{
    sqlite3_stmt *stmt = nullptr;
    std::string result = fallback;
    if (sqlite3_prepare_v2(progress_, "SELECT value FROM settings WHERE key=?", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = string_column(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

bool Database::set_setting(const char *key, const std::string &value)
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(progress_,
                           "INSERT INTO settings(key,value) VALUES(?,?) "
                           "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

AppSettings Database::settings()
{
    std::lock_guard<std::mutex> lock(mutex_);
    load_settings();
    return settings_;
}

bool Database::save_settings(const AppSettings &value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!exec(progress_, "BEGIN IMMEDIATE")) {
        return false;
    }
    const bool ok =
        set_setting("new_words_per_day", std::to_string(std::clamp(value.new_words_per_day, 5, 30))) &&
        set_setting("session_card_limit", std::to_string(std::clamp(value.session_card_limit, 20, 200))) &&
        set_setting("audio_volume", std::to_string(std::clamp(value.audio_volume, 0, 100))) &&
        set_setting("recognition_gate_days", std::to_string(value.recognition_gate_days)) &&
        set_setting("dictation_gate_days", std::to_string(value.dictation_gate_days)) &&
        set_setting("level_unlock_ratio", std::to_string(value.level_unlock_ratio)) &&
        set_setting("write_review_log", value.write_review_log ? "1" : "0");
    if (!ok) {
        exec(progress_, "ROLLBACK");
        return false;
    }
    if (!exec(progress_, "COMMIT")) {
        return false;
    }
    load_settings();
    return true;
}

Word Database::word(int64_t word_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Word result;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id,word,type,cefr,phon_br,phon_n_am,definition,chinese_definition,"
        "example,uk,us FROM words WHERE id=?";
    if (sqlite3_prepare_v2(words_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, word_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result.id = sqlite3_column_int64(stmt, 0);
            result.word = string_column(stmt, 1);
            result.type = string_column(stmt, 2);
            result.cefr = string_column(stmt, 3);
            result.phon_br = string_column(stmt, 4);
            result.phon_n_am = string_column(stmt, 5);
            result.definition = string_column(stmt, 6);
            result.chinese_definition = string_column(stmt, 7);
            result.example = string_column(stmt, 8);
            result.uk_audio = string_column(stmt, 9);
            result.us_audio = string_column(stmt, 10);
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

Card Database::read_card_column(sqlite3_stmt *stmt)
{
    Card card;
    card.id = sqlite3_column_int64(stmt, 0);
    card.word_id = sqlite3_column_int64(stmt, 1);
    card.mode = static_cast<CardMode>(sqlite3_column_int(stmt, 2));
    card.state = static_cast<CardState>(sqlite3_column_int(stmt, 3));
    card.due = sqlite3_column_int64(stmt, 4);
    card.stability = static_cast<float>(sqlite3_column_double(stmt, 5));
    card.difficulty = static_cast<float>(sqlite3_column_double(stmt, 6));
    card.elapsed_days = static_cast<float>(sqlite3_column_double(stmt, 7));
    card.scheduled_days = static_cast<float>(sqlite3_column_double(stmt, 8));
    card.reps = sqlite3_column_int(stmt, 9);
    card.lapses = sqlite3_column_int(stmt, 10);
    card.last_review = sqlite3_column_int64(stmt, 11);
    card.enabled = sqlite3_column_int(stmt, 12) != 0;
    card.created_at = sqlite3_column_int64(stmt, 13);
    return card;
}

std::vector<Card> Database::load_due_cards(int limit)
{
    std::vector<Card> cards;
    if (limit <= 0) {
        return cards;
    }
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id,word_id,mode,state,due,stability,difficulty,elapsed_days,"
        "scheduled_days,reps,lapses,last_review,enabled,created_at FROM cards "
        "WHERE enabled=1 AND state<>0 AND due<=? "
        "ORDER BY CASE state WHEN 3 THEN 0 WHEN 1 THEN 1 ELSE 2 END, due, id LIMIT ?";
    if (sqlite3_prepare_v2(progress_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, clock_service::now());
        sqlite3_bind_int(stmt, 2, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            cards.push_back(read_card_column(stmt));
        }
    }
    sqlite3_finalize(stmt);
    return cards;
}

std::vector<Card> Database::load_new_cards(int limit)
{
    std::vector<Card> cards;
    if (limit <= 0) {
        return cards;
    }
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id,word_id,mode,state,due,stability,difficulty,elapsed_days,"
        "scheduled_days,reps,lapses,last_review,enabled,created_at FROM cards "
        "WHERE enabled=1 AND state=0 ORDER BY created_at, word_id, mode LIMIT ?";
    if (sqlite3_prepare_v2(progress_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            cards.push_back(read_card_column(stmt));
        }
    }
    sqlite3_finalize(stmt);
    return cards;
}

int Database::introduced_today(int64_t day_start)
{
    sqlite3_stmt *stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(progress_,
                           "SELECT COUNT(*) FROM started_words WHERE introduced_at>=?",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, day_start);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

int Database::introduce_new_words(int max_words, int remaining_card_slots)
{
    if (max_words <= 0 || remaining_card_slots < 2) {
        return 0;
    }
    const int max_by_slots = remaining_card_slots / 2;
    int remaining = std::min(max_words, max_by_slots);
    int introduced = 0;
    const int64_t now = clock_service::now();
    const int last_level = level_index(settings_.unlocked_level);

    if (!exec(progress_, "BEGIN IMMEDIATE")) {
        return 0;
    }
    for (int level = 0; level <= last_level && remaining > 0; ++level) {
        const std::string level_name = LEVELS[level];
        auto &words = level_words_[level_name];
        size_t &cursor = level_cursor_[level_name];
        while (cursor < words.size() && remaining > 0) {
            const int64_t word_id = words[cursor++];
            if (started_words_.count(word_id) != 0) {
                continue;
            }

            sqlite3_stmt *card_stmt = nullptr;
            const char *card_sql =
                "INSERT INTO cards(word_id,mode,state,due,stability,difficulty,"
                "elapsed_days,scheduled_days,reps,lapses,last_review,enabled,created_at) "
                "VALUES(?,?,0,0,0,0,0,0,0,0,0,?,?)";
            if (sqlite3_prepare_v2(progress_, card_sql, -1, &card_stmt, nullptr) != SQLITE_OK) {
                continue;
            }
            for (int mode = 1; mode <= 3; ++mode) {
                sqlite3_reset(card_stmt);
                sqlite3_bind_int64(card_stmt, 1, word_id);
                sqlite3_bind_int(card_stmt, 2, mode);
                sqlite3_bind_int(card_stmt, 3, mode == 3 ? 0 : 1);
                sqlite3_bind_int64(card_stmt, 4, now);
                sqlite3_step(card_stmt);
            }
            sqlite3_finalize(card_stmt);

            sqlite3_stmt *start_stmt = nullptr;
            if (sqlite3_prepare_v2(progress_,
                                   "INSERT OR IGNORE INTO started_words(word_id,cefr,introduced_at) "
                                   "VALUES(?,?,?)",
                                   -1, &start_stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(start_stmt, 1, word_id);
                sqlite3_bind_text(start_stmt, 2, level_name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(start_stmt, 3, now);
                sqlite3_step(start_stmt);
            }
            sqlite3_finalize(start_stmt);

            sqlite3_stmt *status_stmt = nullptr;
            if (sqlite3_prepare_v2(progress_,
                                   "INSERT OR IGNORE INTO word_status"
                                   "(word_id,cefr,recognition_ready,dictation_unlocked,mastered,updated_at) "
                                   "VALUES(?,?,0,0,0,?)",
                                   -1, &status_stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(status_stmt, 1, word_id);
                sqlite3_bind_text(status_stmt, 2, level_name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(status_stmt, 3, now);
                sqlite3_step(status_stmt);
            }
            sqlite3_finalize(status_stmt);

            started_words_.insert(word_id);
            --remaining;
            ++introduced;
        }
    }

    if (introduced > 0) {
        sqlite3_stmt *daily = nullptr;
        if (sqlite3_prepare_v2(progress_,
                               "INSERT INTO daily_stats(day_start,reviews,new_words,correct,again) "
                               "VALUES(?,0,?,0,0) ON CONFLICT(day_start) DO UPDATE SET "
                               "new_words=new_words+excluded.new_words",
                               -1, &daily, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(daily, 1, clock_service::start_of_day(now));
            sqlite3_bind_int(daily, 2, introduced);
            sqlite3_step(daily);
        }
        sqlite3_finalize(daily);
    }
    exec(progress_, "COMMIT");
    return introduced;
}

std::vector<Card> Database::prepare_session(int capacity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Card> result;
    if (!ready_) {
        return result;
    }
    capacity = std::max(1, capacity);
    auto due = load_due_cards(capacity);
    result.insert(result.end(), due.begin(), due.end());

    int slots = capacity - static_cast<int>(result.size());
    if (slots >= 2) {
        const int today = introduced_today(clock_service::start_of_day(clock_service::now()));
        const int allowed = std::max(0, settings_.new_words_per_day - today);
        if (allowed > 0) {
            introduce_new_words(allowed, slots);
        }
    }
    if (slots > 0) {
        auto fresh = load_new_cards(capacity - static_cast<int>(result.size()));
        result.insert(result.end(), fresh.begin(), fresh.end());
    }

    if (static_cast<int>(result.size()) > capacity) {
        result.resize(capacity);
    }
    return result;
}

void Database::update_progress_for_word(int64_t word_id, int64_t now, ReviewEffects &effects)
{
    float s1 = 0;
    float s2 = 0;
    float s3 = 0;
    int mode3_enabled = 0;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(progress_, "SELECT mode,stability,enabled FROM cards WHERE word_id=?",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, word_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const int mode = sqlite3_column_int(stmt, 0);
            const float stability = static_cast<float>(sqlite3_column_double(stmt, 1));
            if (mode == 1) {
                s1 = stability;
            } else if (mode == 2) {
                s2 = stability;
            } else if (mode == 3) {
                s3 = stability;
                mode3_enabled = sqlite3_column_int(stmt, 2);
            }
        }
    }
    sqlite3_finalize(stmt);

    const bool recognition_ready =
        s1 >= settings_.recognition_gate_days && s2 >= settings_.recognition_gate_days;
    const bool mastered = mode3_enabled != 0 && s3 >= settings_.dictation_gate_days;

    if (recognition_ready && mode3_enabled == 0) {
        sqlite3_stmt *unlock = nullptr;
        if (sqlite3_prepare_v2(progress_,
                               "UPDATE cards SET enabled=1,state=0,due=0,stability=0,"
                               "difficulty=0,reps=0,lapses=0,last_review=0,created_at=? "
                               "WHERE word_id=? AND mode=3",
                               -1, &unlock, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(unlock, 1, now);
            sqlite3_bind_int64(unlock, 2, word_id);
            sqlite3_step(unlock);
        }
        sqlite3_finalize(unlock);
        effects.dictation_unlocked = true;
    }

    sqlite3_stmt *update = nullptr;
    if (sqlite3_prepare_v2(progress_,
                           "UPDATE word_status SET recognition_ready=?,dictation_unlocked=?,"
                           "mastered=?,updated_at=? WHERE word_id=?",
                           -1, &update, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(update, 1, recognition_ready ? 1 : 0);
        sqlite3_bind_int(update, 2, (recognition_ready || mode3_enabled) ? 1 : 0);
        sqlite3_bind_int(update, 3, mastered ? 1 : 0);
        sqlite3_bind_int64(update, 4, now);
        sqlite3_bind_int64(update, 5, word_id);
        sqlite3_step(update);
    }
    sqlite3_finalize(update);
    if (mastered) {
        effects.word_mastered = true;
    }
}

void Database::maybe_unlock_level(ReviewEffects &effects)
{
    const std::string current = settings_.unlocked_level;
    const std::string next = next_level(current);
    if (next.empty()) {
        return;
    }
    const int total = total_words_in_level(current);
    if (total <= 0) {
        return;
    }

    sqlite3_stmt *stmt = nullptr;
    int mastered = 0;
    if (sqlite3_prepare_v2(progress_,
                           "SELECT COUNT(*) FROM word_status WHERE cefr=? AND mastered=1",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, current.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            mastered = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    if (static_cast<float>(mastered) / static_cast<float>(total) >=
        settings_.level_unlock_ratio) {
        if (set_setting("unlocked_level", next)) {
            settings_.unlocked_level = next;
            effects.level_unlocked = true;
            effects.new_level = next;
            ESP_LOGI(TAG, "level unlocked: %s", next.c_str());
        }
    }
}

bool Database::review_card(Card &card, int rating, ReviewEffects &effects)
{
    std::lock_guard<std::mutex> lock(mutex_);
    effects = ReviewEffects{};
    const int64_t now = clock_service::now();

    fsrs::ReviewInput input;
    input.state = card.state;
    input.stability = card.stability;
    input.difficulty = card.difficulty;
    input.reps = card.reps;
    input.lapses = card.lapses;
    input.last_review = card.last_review;
    input.now = now;
    const fsrs::ReviewResult result = fsrs::review(input, rating);

    if (!exec(progress_, "BEGIN IMMEDIATE")) {
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "UPDATE cards SET state=?,due=?,stability=?,difficulty=?,elapsed_days=?,"
        "scheduled_days=?,reps=?,lapses=?,last_review=? WHERE id=?";
    bool ok = false;
    if (sqlite3_prepare_v2(progress_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, static_cast<int>(result.state));
        sqlite3_bind_int64(stmt, 2, result.due);
        sqlite3_bind_double(stmt, 3, result.stability);
        sqlite3_bind_double(stmt, 4, result.difficulty);
        sqlite3_bind_double(stmt, 5, result.elapsed_days);
        sqlite3_bind_double(stmt, 6, result.scheduled_days);
        sqlite3_bind_int(stmt, 7, result.reps);
        sqlite3_bind_int(stmt, 8, result.lapses);
        sqlite3_bind_int64(stmt, 9, now);
        sqlite3_bind_int64(stmt, 10, card.id);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);
    if (!ok) {
        exec(progress_, "ROLLBACK");
        return false;
    }

    sqlite3_stmt *daily = nullptr;
    if (sqlite3_prepare_v2(progress_,
                           "INSERT INTO daily_stats(day_start,reviews,new_words,correct,again) "
                           "VALUES(?,1,0,?,?) ON CONFLICT(day_start) DO UPDATE SET "
                           "reviews=reviews+1,correct=correct+excluded.correct,"
                           "again=again+excluded.again",
                           -1, &daily, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(daily, 1, clock_service::start_of_day(now));
        sqlite3_bind_int(daily, 2, rating >= 3 ? 1 : 0);
        sqlite3_bind_int(daily, 3, rating == 1 ? 1 : 0);
        sqlite3_step(daily);
    }
    sqlite3_finalize(daily);

    if (settings_.write_review_log) {
        sqlite3_stmt *log = nullptr;
        const char *log_sql =
            "INSERT INTO review_log(card_id,word_id,mode,rating,ts,state_before,state_after,"
            "stability_before,stability_after,difficulty_before,difficulty_after,"
            "elapsed_days,scheduled_days) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)";
        if (sqlite3_prepare_v2(progress_, log_sql, -1, &log, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(log, 1, card.id);
            sqlite3_bind_int64(log, 2, card.word_id);
            sqlite3_bind_int(log, 3, static_cast<int>(card.mode));
            sqlite3_bind_int(log, 4, rating);
            sqlite3_bind_int64(log, 5, now);
            sqlite3_bind_int(log, 6, static_cast<int>(card.state));
            sqlite3_bind_int(log, 7, static_cast<int>(result.state));
            sqlite3_bind_double(log, 8, card.stability);
            sqlite3_bind_double(log, 9, result.stability);
            sqlite3_bind_double(log, 10, card.difficulty);
            sqlite3_bind_double(log, 11, result.difficulty);
            sqlite3_bind_double(log, 12, result.elapsed_days);
            sqlite3_bind_double(log, 13, result.scheduled_days);
            sqlite3_step(log);
        }
        sqlite3_finalize(log);
    }

    update_progress_for_word(card.word_id, now, effects);
    maybe_unlock_level(effects);
    exec(progress_, "COMMIT");

    card.state = result.state;
    card.due = result.due;
    card.stability = result.stability;
    card.difficulty = result.difficulty;
    card.elapsed_days = result.elapsed_days;
    card.scheduled_days = result.scheduled_days;
    card.reps = result.reps;
    card.lapses = result.lapses;
    card.last_review = now;
    return true;
}

HomeStats Database::home_stats()
{
    std::lock_guard<std::mutex> lock(mutex_);
    HomeStats stats;
    stats.current_level = settings_.unlocked_level;
    stats.next_level = next_level(settings_.unlocked_level);
    stats.new_words_limit = settings_.new_words_per_day;
    stats.new_words_done = introduced_today(clock_service::start_of_day(clock_service::now()));
    stats.total_current_level = total_words_in_level(settings_.unlocked_level);

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(progress_,
                           "SELECT COUNT(*) FROM cards WHERE enabled=1 AND state<>0 AND due<=?",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, clock_service::now());
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.due_cards = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);

    stmt = nullptr;
    if (sqlite3_prepare_v2(progress_, "SELECT COUNT(*) FROM started_words", -1, &stmt,
                           nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        stats.introduced_words = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    stmt = nullptr;
    if (sqlite3_prepare_v2(progress_,
                           "SELECT COUNT(*) FROM word_status WHERE cefr=? AND mastered=1",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, settings_.unlocked_level.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.mastered_current_level = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    stats.current_level_ratio =
        stats.total_current_level > 0
            ? static_cast<float>(stats.mastered_current_level) /
                  static_cast<float>(stats.total_current_level)
            : 0.0f;
    return stats;
}

DailyStats Database::daily_stats()
{
    std::lock_guard<std::mutex> lock(mutex_);
    DailyStats stats;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(progress_,
                           "SELECT reviews,new_words,correct,again FROM daily_stats "
                           "WHERE day_start=?",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, clock_service::start_of_day(clock_service::now()));
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.reviews = sqlite3_column_int(stmt, 0);
            stats.new_words = sqlite3_column_int(stmt, 1);
            stats.correct = sqlite3_column_int(stmt, 2);
            stats.again = sqlite3_column_int(stmt, 3);
        }
    }
    sqlite3_finalize(stmt);
    return stats;
}

int Database::total_words_in_level(const std::string &level) const
{
    auto it = level_words_.find(level);
    return it == level_words_.end() ? 0 : static_cast<int>(it->second.size());
}
