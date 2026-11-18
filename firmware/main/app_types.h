#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class CardMode : int {
    Recognition = 1, // word -> Chinese
    Recall = 2,      // Chinese -> word
    Dictation = 3,   // type the word
};

enum class CardState : int {
    New = 0,
    Learning = 1,
    Review = 2,
    Relearning = 3,
};

struct Word {
    int64_t id = 0;
    std::string word;
    std::string type;
    std::string cefr;
    std::string phon_br;
    std::string phon_n_am;
    std::string definition;
    std::string chinese_definition;
    std::string example;
    std::string uk_audio;
    std::string us_audio;
};

struct Card {
    int64_t id = 0;
    int64_t word_id = 0;
    CardMode mode = CardMode::Recognition;
    CardState state = CardState::New;
    int64_t due = 0;
    float stability = 0;
    float difficulty = 0;
    float elapsed_days = 0;
    float scheduled_days = 0;
    int reps = 0;
    int lapses = 0;
    int64_t last_review = 0;
    bool enabled = true;
    int64_t created_at = 0;
};

struct AppSettings {
    int new_words_per_day = 15;
    int session_card_limit = 60;
    int audio_volume = 70;
    float recognition_gate_days = 21.0f;
    float dictation_gate_days = 21.0f;
    float level_unlock_ratio = 0.90f;
    bool write_review_log = false;
    std::string unlocked_level = "a1";
};

struct HomeStats {
    int due_cards = 0;
    int new_words_done = 0;
    int new_words_limit = 15;
    int introduced_words = 0;
    int mastered_current_level = 0;
    int total_current_level = 0;
    float current_level_ratio = 0.0f;
    std::string current_level = "a1";
    std::string next_level = "a2";
};

struct DailyStats {
    int reviews = 0;
    int new_words = 0;
    int correct = 0;
    int again = 0;
};

inline const char *card_mode_name(CardMode mode)
{
    switch (mode) {
    case CardMode::Recognition:
        return "英 → 中";
    case CardMode::Recall:
        return "中 → 英";
    case CardMode::Dictation:
        return "默写";
    }
    return "";
}

