#pragma once

#include "app_types.h"
#include "audio_service.h"
#include "database.h"
#include "keyboard.h"

#include "lvgl.h"

#include <deque>
#include <string>

class AppUi {
public:
    AppUi(Database &database, AudioService &audio, KeyboardService &keyboard);

    void init(lv_display_t *display);
    void show_fatal_error(const std::string &message);
    void show_home();

public:
    enum class Screen {
        Home,
        Study,
        Summary,
        Settings,
        Error,
    };

private:
    enum Action : int {
        ACTION_NONE = 0,
        ACTION_START = 1,
        ACTION_SETTINGS = 2,
        ACTION_HOME = 3,
        ACTION_REVEAL = 4,
        ACTION_CHECK = 5,
        ACTION_RATE_AGAIN = 11,
        ACTION_RATE_HARD = 12,
        ACTION_RATE_GOOD = 13,
        ACTION_RATE_EASY = 14,
        ACTION_AUDIO_UK = 21,
        ACTION_AUDIO_US = 22,
        ACTION_NEW_MINUS = 31,
        ACTION_NEW_PLUS = 32,
        ACTION_SESSION_MINUS = 33,
        ACTION_SESSION_PLUS = 34,
        ACTION_VOLUME_MINUS = 35,
        ACTION_VOLUME_PLUS = 36,
        ACTION_LOG_TOGGLE = 37,
        ACTION_SAVE_SETTINGS = 38,
    };

    static void action_event(lv_event_t *event);
    static void timer_event(lv_timer_t *timer);
    static void toast_timer_event(lv_timer_t *timer);

    void handle_action(int action);
    void handle_key(const KeyEvent &event);
    void poll_keyboard();
    void start_session();
    void next_card();
    void render_study();
    void render_summary();
    void render_settings();
    void rate_current(int rating);
    void check_dictation();
    void toggle_reveal();
    void play_audio(bool british);
    void capture_screenshot();
    void show_toast(const std::string &text);
    void clear_screen();
    void update_keyboard_indicator();

    lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                         uint32_t color, lv_coord_t width = LV_SIZE_CONTENT);
    lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                         lv_coord_t h, uint32_t color, int radius = 18);
    lv_obj_t *make_button(lv_obj_t *parent, const char *text, uint32_t color,
                          int action, lv_coord_t width, lv_coord_t height,
                          const lv_font_t *font = nullptr);
    lv_obj_t *make_top_bar(const char *left, const char *center, bool home_button = true);
    lv_obj_t *make_rating_row(lv_obj_t *parent, bool dictation);
    lv_obj_t *make_word_audio_row(lv_obj_t *parent, const Word &word, int y,
                                  const lv_font_t *font);
    lv_obj_t *make_stepper(lv_obj_t *parent, int y, const char *label, int value,
                           int minus_action, int plus_action);
    std::string normalized_word(const std::string &value) const;
    int dictation_letter_count() const;

    Database &db_;
    AudioService &audio_;
    KeyboardService &keyboard_;
    lv_display_t *display_ = nullptr;
    lv_obj_t *screen_ = nullptr;
    lv_timer_t *timer_ = nullptr;
    lv_timer_t *toast_timer_ = nullptr;
    lv_obj_t *toast_obj_ = nullptr;
    lv_obj_t *keyboard_status_label_ = nullptr;

    Screen screen_type_ = Screen::Home;
    bool study_active_ = false;
    bool reveal_ = false;
    bool dictation_checked_ = false;
    bool dictation_correct_ = false;
    std::string typed_;
    std::deque<Card> queue_;
    Card current_;
    Word current_word_;
    int session_total_ = 0;
    int session_done_ = 0;
    int screenshot_sequence_ = 1;
    int64_t last_screenshot_at_ = 0;
    AppSettings edit_settings_;
};

