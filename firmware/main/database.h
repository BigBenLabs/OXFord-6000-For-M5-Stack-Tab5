#pragma once

#include "app_types.h"

#include "esp_err.h"
#include "esp_sqlite3.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

struct ReviewEffects {
    bool dictation_unlocked = false;
    bool level_unlocked = false;
    bool word_mastered = false;
    std::string new_level;
};

class Database {
public:
    bool begin();
    bool ready() const
    {
        return ready_;
    }
    const std::string &last_error() const
    {
        return error_;
    }

    AppSettings settings();
    bool save_settings(const AppSettings &value);

    Word word(int64_t word_id);
    std::vector<Card> prepare_session(int capacity);
    bool review_card(Card &card, int rating, ReviewEffects &effects);
    HomeStats home_stats();
    DailyStats daily_stats();

    int total_words_in_level(const std::string &level) const;

private:
    bool exec(sqlite3 *db, const std::string &sql);
    bool create_schema();
    void load_catalog();
    void load_settings();
    int introduced_today(int64_t day_start);
    int introduce_new_words(int max_words, int remaining_card_slots);
    std::vector<Card> load_due_cards(int limit);
    std::vector<Card> load_new_cards(int limit);
    Card read_card_column(sqlite3_stmt *stmt);
    void update_progress_for_word(int64_t word_id, int64_t now, ReviewEffects &effects);
    void maybe_unlock_level(ReviewEffects &effects);
    bool get_setting_int(const char *key, int fallback, int &out);
    bool get_setting_float(const char *key, float fallback, float &out);
    std::string get_setting_string(const char *key, const std::string &fallback);
    bool set_setting(const char *key, const std::string &value);
    void seed_defaults();

    esp_sqlite3_t *words_db_ = nullptr;
    esp_sqlite3_t *progress_db_ = nullptr;
    sqlite3 *words_ = nullptr;
    sqlite3 *progress_ = nullptr;
    bool ready_ = false;
    std::string error_;
    std::mutex mutex_;
    AppSettings settings_;
    std::map<std::string, std::vector<int64_t>> level_words_;
    std::map<std::string, size_t> level_cursor_;
    std::set<int64_t> started_words_;
};

