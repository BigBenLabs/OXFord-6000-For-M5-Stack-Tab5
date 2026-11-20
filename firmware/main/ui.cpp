#include "ui.h"

#include "bsp/esp-bsp.h"
#include "clock_service.h"
#include "fsrs.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <vector>

LV_FONT_DECLARE(font_zh_28);

namespace {

constexpr uint32_t COLOR_BG = 0x06110C;
constexpr uint32_t COLOR_PANEL = 0x0D1D15;
constexpr uint32_t COLOR_PANEL_2 = 0x12261B;
constexpr uint32_t COLOR_BORDER = 0x244936;
constexpr uint32_t COLOR_TEXT = 0xE7F4EC;
constexpr uint32_t COLOR_MUTED = 0x8AA99A;
constexpr uint32_t COLOR_GREEN = 0x65D59A;
constexpr uint32_t COLOR_GREEN_DARK = 0x173D2B;
constexpr uint32_t COLOR_RED = 0xC75454;
constexpr uint32_t COLOR_AMBER = 0xB98538;
constexpr uint32_t COLOR_TEAL = 0x3D9F8C;
constexpr uint32_t COLOR_BLUE = 0x4B82B8;
constexpr const char *TAG = "ui";

std::string upper_level(std::string level)
{
    for (char &ch : level) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return level;
}

std::string display_level(const std::string &level)
{
    return level.empty() ? "-" : upper_level(level);
}

bool is_blank_text(const KeyEvent &event)
{
    return event.type == KeyEvent::Type::Text && event.text[0] == ' ' &&
           event.text[1] == '\0';
}

const char *signal_name(int rssi)
{
    if (rssi >= -55) {
        return "强";
    }
    if (rssi >= -70) {
        return "中";
    }
    return "弱";
}

void put_le16(uint8_t *out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void put_le32(uint8_t *out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

const char *screen_name(AppUi::Screen screen)
{
    switch (screen) {
    case AppUi::Screen::Home:
        return "home";
    case AppUi::Screen::Study:
        return "study";
    case AppUi::Screen::Summary:
        return "summary";
    case AppUi::Screen::Settings:
        return "settings";
    case AppUi::Screen::Wifi:
        return "wifi";
    case AppUi::Screen::Error:
        return "error";
    }
    return "screen";
}

} // namespace

AppUi::AppUi(Database &database, AudioService &audio, KeyboardService &keyboard,
             PowerService &power, WifiService &wifi)
    : db_(database), audio_(audio), keyboard_(keyboard), power_(power), wifi_(wifi)
{
}

void AppUi::init(lv_display_t *display)
{
    display_ = display;
    screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen_, 0, 0);
    lv_obj_set_style_pad_all(screen_, 0, 0);
    lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);
    lv_screen_load(screen_);
    power_status_ = power_.status();
    last_usb_present_ = power_status_.usb_present;
    last_power_update_ms_ =
        static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
    last_wifi_update_ms_ = last_power_update_ms_;
    last_wifi_revision_ = wifi_.network_revision();
    last_wifi_state_ = wifi_.state();
    timer_ = lv_timer_create(timer_event, 40, this);
}

void AppUi::clear_screen()
{
    if (toast_timer_ != nullptr) {
        lv_timer_delete(toast_timer_);
        toast_timer_ = nullptr;
    }
    toast_obj_ = nullptr;
    keyboard_status_label_ = nullptr;
    battery_label_ = nullptr;
    battery_bar_ = nullptr;
    battery_terminal_ = nullptr;
    wifi_status_label_ = nullptr;
    sleep_overlay_ = nullptr;
    if (screen_ != nullptr) {
        lv_obj_clean(screen_);
    }
}

lv_obj_t *AppUi::make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color, lv_coord_t width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    if (font != nullptr) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(label, 7, 0);
    if (width != LV_SIZE_CONTENT) {
        lv_obj_set_width(label, width);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    }
    return label;
}

lv_obj_t *AppUi::make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                            lv_coord_t h, uint32_t color, int radius)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    return panel;
}

lv_obj_t *AppUi::make_button(lv_obj_t *parent, const char *text, uint32_t color,
                             int action, lv_coord_t width, lv_coord_t height,
                             const lv_font_t *font)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_user_data(button, reinterpret_cast<void *>(static_cast<intptr_t>(action)));
    lv_obj_add_event_cb(button, action_event, LV_EVENT_CLICKED, this);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font != nullptr ? font : &font_zh_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t *AppUi::make_top_bar(const char *left, const char *center, bool home_button)
{
    lv_obj_t *bar = make_panel(screen_, 0, 0, 1280, 68, 0x09170F, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(COLOR_BORDER), 0);

    lv_obj_t *left_label = make_label(bar, left, &font_zh_28, COLOR_TEXT);
    lv_obj_align(left_label, LV_ALIGN_LEFT_MID, 28, 0);

    if (center != nullptr && center[0] != '\0') {
        lv_obj_t *center_label = make_label(bar, center, &font_zh_28, COLOR_MUTED);
        lv_obj_align(center_label, LV_ALIGN_CENTER, 0, 0);
    }
    make_battery_indicator(bar, home_button ? -166 : -28);
    if (home_button) {
        lv_obj_t *home = make_button(bar, "主页", COLOR_GREEN_DARK, ACTION_HOME, 108, 44);
        lv_obj_align(home, LV_ALIGN_RIGHT_MID, -24, 0);
    }
    return bar;
}

lv_obj_t *AppUi::make_battery_indicator(lv_obj_t *parent, lv_coord_t right_offset)
{
    lv_obj_t *indicator = lv_obj_create(parent);
    lv_obj_set_size(indicator, 150, 38);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(indicator, 0, 0);
    lv_obj_set_style_pad_all(indicator, 0, 0);
    lv_obj_set_scrollbar_mode(indicator, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(indicator, LV_ALIGN_RIGHT_MID, right_offset, 0);

    battery_bar_ = lv_bar_create(indicator);
    lv_obj_set_pos(battery_bar_, 0, 7);
    lv_obj_set_size(battery_bar_, 50, 24);
    lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(0x101D16), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(battery_bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(battery_bar_, lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
    lv_obj_set_style_border_width(battery_bar_, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(battery_bar_, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(battery_bar_, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(COLOR_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(battery_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(battery_bar_, 2, LV_PART_INDICATOR);
    lv_bar_set_range(battery_bar_, 0, 100);
    lv_bar_set_value(battery_bar_, 0, LV_ANIM_OFF);

    battery_terminal_ = lv_obj_create(indicator);
    lv_obj_set_pos(battery_terminal_, 50, 14);
    lv_obj_set_size(battery_terminal_, 5, 10);
    lv_obj_set_style_bg_color(battery_terminal_, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_bg_opa(battery_terminal_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(battery_terminal_, 0, 0);
    lv_obj_set_style_radius(battery_terminal_, 1, 0);

    battery_label_ = make_label(indicator, "--%", &font_zh_28, COLOR_TEXT);
    lv_obj_set_pos(battery_label_, 62, 1);
    lv_obj_set_width(battery_label_, 86);
    lv_obj_set_style_text_align(battery_label_, LV_TEXT_ALIGN_RIGHT, 0);
    update_power_widget();
    return indicator;
}

void AppUi::show_fatal_error(const std::string &message)
{
    screen_type_ = Screen::Error;
    clear_screen();
    make_top_bar("Tab5 · FSRS", "启动检查");

    lv_obj_t *panel = make_panel(screen_, 70, 120, 1140, 500, COLOR_PANEL, 22);
    lv_obj_t *title = make_label(panel, "无法开始学习", &font_zh_28, COLOR_RED);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *text = make_label(panel, message.c_str(), &font_zh_28, COLOR_TEXT, 1000);
    lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 120);

    lv_obj_t *path = make_label(
        panel,
        "请确认 TF 卡根目录包含：\n"
        "/database/oxford.db\n"
        "/audio/uk_audio_split_24m/\n"
        "/audio/us_audio_split_24m/\n\n"
        "进度会自动写入 /fsrs_progress.db。",
        &font_zh_28, COLOR_MUTED, 980);
    lv_obj_align(path, LV_ALIGN_TOP_LEFT, 70, 220);
}

void AppUi::show_home()
{
    screen_type_ = Screen::Home;
    study_active_ = false;
    clear_screen();
    make_top_bar("OXFORD 6000", "FSRS 离线闪卡", false);

    if (!db_.ready()) {
        show_fatal_error(db_.last_error());
        return;
    }

    const HomeStats stats = db_.home_stats();
    const AppSettings settings = db_.settings();

    lv_obj_t *title = make_label(screen_, "今天学一点，记忆会自己长大。", &font_zh_28,
                                 COLOR_MUTED);
    lv_obj_set_pos(title, 50, 92);

    struct StatCard {
        const char *title;
        char value[48];
        uint32_t accent;
    };
    StatCard cards[4]{};
    cards[0] = {"今日待复习", {}, COLOR_AMBER};
    std::snprintf(cards[0].value, sizeof(cards[0].value), "%d 张", stats.due_cards);
    cards[1] = {"今日新词", {}, COLOR_GREEN};
    std::snprintf(cards[1].value, sizeof(cards[1].value), "%d / %d",
                  stats.new_words_done, stats.new_words_limit);
    std::string level_text = "当前等级 " + display_level(stats.current_level);
    std::snprintf(cards[2].value, sizeof(cards[2].value), "%s", level_text.c_str());
    cards[2].title = "学习阶段";
    cards[2].accent = COLOR_BLUE;
    cards[3] = {"本级已熟练", {}, COLOR_TEAL};
    std::snprintf(cards[3].value, sizeof(cards[3].value), "%d / %d",
                  stats.mastered_current_level, stats.total_current_level);

    for (int i = 0; i < 4; ++i) {
        lv_obj_t *card = make_panel(screen_, 50 + i * 300, 145, 270, 118, COLOR_PANEL, 18);
        lv_obj_t *accent = lv_obj_create(card);
        lv_obj_set_pos(accent, 22, 20);
        lv_obj_set_size(accent, 6, 72);
        lv_obj_set_style_bg_color(accent, lv_color_hex(cards[i].accent), 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        lv_obj_set_style_radius(accent, 3, 0);
        lv_obj_t *name = make_label(card, cards[i].title, &font_zh_28, COLOR_MUTED);
        lv_obj_set_pos(name, 42, 20);
        lv_obj_t *value = make_label(card, cards[i].value, &font_zh_28, COLOR_TEXT);
        lv_obj_set_pos(value, 42, 58);
    }

    lv_obj_t *progress_panel = make_panel(screen_, 50, 288, 1180, 155, COLOR_PANEL, 20);
    char progress_text[192];
    const std::string next_level_text =
        stats.next_level.empty() ? "全部等级" : display_level(stats.next_level);
    std::snprintf(progress_text, sizeof(progress_text),
                  "%s → %s 升级进度 · 默写熟练词占本级 %.0f%%（门槛 %.0f%%）",
                  display_level(stats.current_level).c_str(), next_level_text.c_str(),
                  stats.current_level_ratio * 100.0f,
                  settings.level_unlock_ratio * 100.0f);
    lv_obj_t *progress_label = make_label(progress_panel, progress_text, &font_zh_28,
                                          COLOR_TEXT);
    lv_obj_set_pos(progress_label, 28, 24);

    lv_obj_t *bar = lv_bar_create(progress_panel);
    lv_obj_set_pos(bar, 28, 82);
    lv_obj_set_size(bar, 1124, 18);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_GREEN_DARK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 9, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, static_cast<int>(std::clamp(stats.current_level_ratio, 0.0f, 1.0f) * 1000),
                     LV_ANIM_OFF);

    char mode_text[160];
    std::snprintf(mode_text, sizeof(mode_text),
                  "认读双卡 stability ≥ %.0f 天 → 解锁默写；默写 stability ≥ %.0f 天 → 计为熟练",
                  settings.recognition_gate_days, settings.dictation_gate_days);
    lv_obj_t *mode_label = make_label(progress_panel, mode_text, &font_zh_28, COLOR_MUTED);
    lv_obj_set_pos(mode_label, 28, 116);

    lv_obj_t *start = make_button(screen_, "开始学习", COLOR_GREEN, ACTION_START, 380, 82);
    lv_obj_set_pos(start, 450, 480);
    lv_obj_set_style_text_color(lv_obj_get_child(start, 0), lv_color_hex(0x052013), 0);

    lv_obj_t *settings_button =
        make_button(screen_, "设置", COLOR_GREEN_DARK, ACTION_SETTINGS, 260, 56);
    lv_obj_set_pos(settings_button, 510, 580);

    char footer[220];
    std::snprintf(footer, sizeof(footer), "键盘：%s   ·   发音：ES8388   ·   日志：%s",
                  keyboard_.connected() ? "已连接" : "未连接",
                  settings.write_review_log ? "写入中" : "关闭（表已建）");
    lv_obj_t *footer_label = make_label(screen_, footer, &font_zh_28, COLOR_MUTED);
    lv_obj_set_pos(footer_label, 50, 665);
}

void AppUi::start_session()
{
    edit_settings_ = db_.settings();
    std::vector<Card> cards = db_.prepare_session(edit_settings_.session_card_limit);
    queue_.clear();
    for (Card &card : cards) {
        queue_.push_back(card);
    }
    session_total_ = static_cast<int>(queue_.size());
    session_done_ = 0;
    study_active_ = true;
    if (queue_.empty()) {
        render_summary();
        return;
    }
    next_card();
}

void AppUi::next_card()
{
    if (!queue_.empty() && queue_.front().due > clock_service::now() + 5) {
        study_active_ = false;
        render_summary();
        return;
    }
    while (!queue_.empty()) {
        current_ = queue_.front();
        queue_.pop_front();
        current_word_ = db_.word(current_.word_id);
        if (!current_word_.word.empty()) {
            reveal_ = current_.mode == CardMode::Dictation ? false : false;
            dictation_checked_ = false;
            dictation_correct_ = false;
            typed_.clear();
            render_study();
            if (current_.mode == CardMode::Dictation && !keyboard_.connected()) {
                show_toast("默写需要连接 Tab5 Keyboard");
            }
            return;
        }
    }
    study_active_ = false;
    render_summary();
}

lv_obj_t *AppUi::make_word_audio_row(lv_obj_t *parent, const Word &word, int y,
                                     const lv_font_t *font)
{
    if (word.word.size() > 24) {
        font = &font_zh_28;
    } else if (word.word.size() > 16) {
        font = &lv_font_montserrat_36;
    }
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_pos(row, 35, y);
    lv_obj_set_size(row, 1110, 90);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 18, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *label = make_label(row, word.word.c_str(), font, COLOR_GREEN);
    lv_obj_t *uk = make_button(row, "UK", COLOR_GREEN_DARK, ACTION_AUDIO_UK, 64, 44);
    lv_obj_t *us = make_button(row, "US", COLOR_GREEN_DARK, ACTION_AUDIO_US, 64, 44);
    (void)label;
    (void)uk;
    (void)us;
    return row;
}

lv_obj_t *AppUi::make_rating_row(lv_obj_t *parent, bool dictation)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_pos(row, 90, 606);
    lv_obj_set_size(row, 1100, 78);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    if (dictation) {
        make_button(row, "Again\n没记住", COLOR_RED, ACTION_RATE_AGAIN, 360, 72);
        make_button(row, "Good\n记住了", COLOR_GREEN, ACTION_RATE_GOOD, 360, 72);
    } else {
        make_button(row, "Again\n忘记", COLOR_RED, ACTION_RATE_AGAIN, 250, 72);
        make_button(row, "Hard\n困难", COLOR_AMBER, ACTION_RATE_HARD, 250, 72);
        make_button(row, "Good\n记得", COLOR_GREEN, ACTION_RATE_GOOD, 250, 72);
        make_button(row, "Easy\n简单", COLOR_TEAL, ACTION_RATE_EASY, 250, 72);
    }
    return row;
}

void AppUi::render_study()
{
    screen_type_ = Screen::Study;
    clear_screen();

    char left[96];
    std::snprintf(left, sizeof(left), "%s · %s", display_level(current_word_.cefr).c_str(),
                  card_mode_name(current_.mode));
    char progress[96];
    const int shown = std::min(session_done_ + 1, std::max(session_total_, 1));
    std::snprintf(progress, sizeof(progress), "%d / %d", shown,
                  std::max(session_total_, 1));
    make_top_bar(left, progress);

    lv_obj_t *card = make_panel(screen_, 50, 88, 1180, 500, COLOR_PANEL, 24);
    lv_obj_t *tag = make_panel(card, 24, 20, 138, 38, COLOR_GREEN_DARK, 19);
    lv_obj_t *tag_label = make_label(tag, card_mode_name(current_.mode), &font_zh_28,
                                     COLOR_GREEN);
    lv_obj_center(tag_label);
    if (!current_word_.type.empty()) {
        const std::string type_text = "词性 " + current_word_.type;
        lv_obj_t *type_tag = make_panel(card, 176, 20, 280, 38, COLOR_PANEL_2, 19);
        lv_obj_t *type_label =
            make_label(type_tag, type_text.c_str(), &font_zh_28, COLOR_MUTED);
        lv_obj_center(type_label);
    }

    if (current_.mode == CardMode::Recognition) {
        if (!reveal_) {
            make_word_audio_row(card, current_word_, 65, &lv_font_montserrat_48);
            char phonetics[160];
            std::snprintf(phonetics, sizeof(phonetics), "BR  %s\nAM  %s",
                          current_word_.phon_br.c_str(), current_word_.phon_n_am.c_str());
            lv_obj_t *phon = make_label(card, phonetics, &font_zh_28, COLOR_MUTED);
            lv_obj_align(phon, LV_ALIGN_TOP_MID, 0, 155);
            lv_obj_t *line = lv_obj_create(card);
            lv_obj_set_pos(line, 70, 252);
            lv_obj_set_size(line, 1040, 2);
            lv_obj_set_style_bg_color(line, lv_color_hex(COLOR_BORDER), 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(line, 0, 0);
            lv_obj_t *prompt = make_label(card, "点击卡片显示中文释义", &font_zh_28,
                                          COLOR_MUTED);
            lv_obj_align(prompt, LV_ALIGN_TOP_MID, 0, 300);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(card, reinterpret_cast<void *>(ACTION_REVEAL));
            lv_obj_add_event_cb(card, action_event, LV_EVENT_CLICKED, this);
        } else {
            make_word_audio_row(card, current_word_, 24, &lv_font_montserrat_36);
            char phonetics[160];
            std::snprintf(phonetics, sizeof(phonetics), "BR %s    AM %s",
                          current_word_.phon_br.c_str(), current_word_.phon_n_am.c_str());
            lv_obj_t *phon = make_label(card, phonetics, &font_zh_28, COLOR_MUTED);
            lv_obj_align(phon, LV_ALIGN_TOP_MID, 0, 112);
            lv_obj_t *meaning =
                make_label(card, current_word_.chinese_definition.c_str(), &font_zh_28,
                           COLOR_TEXT, 1040);
            lv_obj_align(meaning, LV_ALIGN_TOP_MID, 0, 165);
            lv_obj_t *definition =
                make_label(card, current_word_.definition.c_str(), &font_zh_28,
                           COLOR_MUTED, 1040);
            lv_obj_align(definition, LV_ALIGN_TOP_LEFT, 70, 278);
            lv_obj_update_layout(card);
            const lv_coord_t example_y = 278 + lv_obj_get_height(definition) + 10;
            lv_obj_t *example =
                make_label(card, current_word_.example.c_str(), &font_zh_28,
                           0x6F897C, 1040);
            lv_obj_align(example, LV_ALIGN_TOP_LEFT, 70, example_y);
            make_rating_row(screen_, false);
        }
    } else if (current_.mode == CardMode::Recall) {
        if (!reveal_) {
            lv_obj_t *meaning =
                make_label(card, current_word_.chinese_definition.c_str(), &font_zh_28,
                           COLOR_TEXT, 1000);
            lv_obj_align(meaning, LV_ALIGN_TOP_MID, 0, 115);
            lv_obj_t *type = make_label(card, current_word_.type.c_str(), &font_zh_28,
                                        COLOR_MUTED);
            lv_obj_align(type, LV_ALIGN_TOP_MID, 0, 240);
            lv_obj_t *prompt = make_label(card, "点击卡片显示单词", &font_zh_28,
                                          COLOR_MUTED);
            lv_obj_align(prompt, LV_ALIGN_TOP_MID, 0, 330);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(card, reinterpret_cast<void *>(ACTION_REVEAL));
            lv_obj_add_event_cb(card, action_event, LV_EVENT_CLICKED, this);
        } else {
            make_word_audio_row(card, current_word_, 24, &lv_font_montserrat_36);
            char phonetics[160];
            std::snprintf(phonetics, sizeof(phonetics), "BR %s    AM %s",
                          current_word_.phon_br.c_str(), current_word_.phon_n_am.c_str());
            lv_obj_t *phon = make_label(card, phonetics, &font_zh_28, COLOR_MUTED);
            lv_obj_align(phon, LV_ALIGN_TOP_MID, 0, 112);
            lv_obj_t *meaning =
                make_label(card, current_word_.chinese_definition.c_str(), &font_zh_28,
                           COLOR_TEXT, 1040);
            lv_obj_align(meaning, LV_ALIGN_TOP_MID, 0, 170);
            lv_obj_t *definition =
                make_label(card, current_word_.definition.c_str(), &font_zh_28,
                           COLOR_MUTED, 1040);
            lv_obj_align(definition, LV_ALIGN_TOP_LEFT, 70, 278);
            lv_obj_update_layout(card);
            const lv_coord_t example_y = 278 + lv_obj_get_height(definition) + 10;
            lv_obj_t *example =
                make_label(card, current_word_.example.c_str(), &font_zh_28,
                           0x6F897C, 1040);
            lv_obj_align(example, LV_ALIGN_TOP_LEFT, 70, example_y);
            make_rating_row(screen_, false);
        }
    } else {
        lv_obj_t *meaning =
            make_label(card, current_word_.chinese_definition.c_str(), &font_zh_28,
                       COLOR_TEXT, 1030);
        lv_obj_align(meaning, LV_ALIGN_TOP_LEFT, 70, 28);
        lv_obj_t *definition =
            make_label(card, current_word_.definition.c_str(), &font_zh_28,
                       COLOR_MUTED, 1030);
        lv_obj_align(definition, LV_ALIGN_TOP_LEFT, 70, 98);

        const std::string normalized = normalized_word(current_word_.word);
        const int letters = dictation_letter_count();
        int cell_width = 50;
        if (letters > 0) {
            cell_width = std::clamp(1080 / std::max(1, static_cast<int>(current_word_.word.size())),
                                    28, 54);
        }
        lv_obj_t *cells = lv_obj_create(card);
        lv_obj_set_pos(cells, 35, 205);
        lv_obj_set_size(cells, 1110, 78);
        lv_obj_set_style_bg_opa(cells, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cells, 0, 0);
        lv_obj_set_style_pad_all(cells, 0, 0);
        lv_obj_set_flex_flow(cells, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(cells, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(cells, 8, 0);
        lv_obj_set_scrollbar_mode(cells, LV_SCROLLBAR_MODE_OFF);

        int letter_index = 0;
        for (char ch : current_word_.word) {
            const bool input_cell = std::isalnum(static_cast<unsigned char>(ch)) != 0;
            lv_obj_t *cell = make_panel(cells, 0, 0, cell_width, 70,
                                        input_cell ? COLOR_PANEL_2 : COLOR_GREEN_DARK, 10);
            char display[2] = {' ', '\0'};
            if (input_cell) {
                if (letter_index < static_cast<int>(typed_.size())) {
                    display[0] = typed_[letter_index];
                }
                ++letter_index;
            } else {
                display[0] = ch;
            }
            lv_obj_t *char_label = make_label(cell, display, &font_zh_28,
                                              input_cell ? COLOR_GREEN : COLOR_MUTED);
            lv_obj_align(char_label, LV_ALIGN_CENTER, 0, -6);
            if (input_cell) {
                lv_obj_t *underline = lv_obj_create(cell);
                lv_obj_set_size(underline, cell_width - 14, 2);
                lv_obj_align(underline, LV_ALIGN_BOTTOM_MID, 0, -12);
                lv_obj_set_style_bg_color(underline, lv_color_hex(COLOR_GREEN), 0);
                lv_obj_set_style_bg_opa(underline, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(underline, 0, 0);
            }
        }

        (void)normalized;
        if (!dictation_checked_) {
            lv_obj_t *hint =
                make_label(card, "键盘输入，Enter 检查；横线数量就是字母数量",
                           &font_zh_28, COLOR_MUTED);
            lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 298);
            lv_obj_t *check = make_button(card, "检查", COLOR_GREEN, ACTION_CHECK, 220, 62);
            lv_obj_set_pos(check, 480, 350);
        } else {
            lv_obj_t *result = make_label(card, dictation_correct_ ? "正确" : "再看一眼",
                                          &font_zh_28,
                                          dictation_correct_ ? COLOR_GREEN : COLOR_RED);
            lv_obj_align(result, LV_ALIGN_TOP_MID, 0, 285);
            make_word_audio_row(card, current_word_, 325, &lv_font_montserrat_36);
            lv_obj_t *phon = make_label(card, current_word_.phon_br.c_str(), &font_zh_28,
                                        COLOR_MUTED);
            lv_obj_align(phon, LV_ALIGN_TOP_MID, 0, 418);
            make_rating_row(screen_, true);
        }
    }
}

void AppUi::render_summary()
{
    screen_type_ = Screen::Summary;
    study_active_ = false;
    clear_screen();
    make_top_bar("本轮总结", "FSRS 调度", true);

    const DailyStats stats = db_.daily_stats();
    const HomeStats home = db_.home_stats();
    lv_obj_t *title = make_label(screen_, session_total_ == 0 ? "今天暂时没有任务" : "本轮完成",
                                 &font_zh_28, COLOR_GREEN);
    lv_obj_set_pos(title, 70, 115);

    lv_obj_t *panel = make_panel(screen_, 70, 175, 1140, 270, COLOR_PANEL, 24);
    char text[512];
    std::snprintf(text, sizeof(text),
                  "今日复习 %d 张    今日新词 %d 个\n"
                  "今日正确 %d 次    今日 Again %d 次\n"
                  "当前等级 %s    本级熟练 %d / %d\n"
                  "今日待复习还剩 %d 张",
                  stats.reviews, stats.new_words, stats.correct, stats.again,
                  display_level(home.current_level).c_str(),
                  home.mastered_current_level, home.total_current_level,
                  home.due_cards);
    lv_obj_t *summary = make_label(panel, text, &font_zh_28, COLOR_TEXT, 1060);
    lv_obj_set_pos(summary, 40, 42);

    lv_obj_t *again = make_button(screen_, "继续学习", COLOR_GREEN, ACTION_START, 320, 72);
    lv_obj_set_pos(again, 300, 500);
    lv_obj_t *home_button =
        make_button(screen_, "返回首页", COLOR_GREEN_DARK, ACTION_HOME, 280, 72);
    lv_obj_set_pos(home_button, 700, 500);
}

lv_obj_t *AppUi::make_stepper(lv_obj_t *parent, int y, const char *label, int value,
                              int minus_action, int plus_action)
{
    lv_obj_t *panel = make_panel(parent, 140, y, 1000, 76, COLOR_PANEL, 16);
    lv_obj_t *name = make_label(panel, label, &font_zh_28, COLOR_TEXT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 28, 0);
    lv_obj_t *minus = make_button(panel, "−", COLOR_GREEN_DARK, minus_action, 58, 50);
    lv_obj_align(minus, LV_ALIGN_RIGHT_MID, -210, 0);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    lv_obj_t *number = make_label(panel, buffer, &font_zh_28, COLOR_GREEN);
    lv_obj_set_width(number, 100);
    lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(number, LV_ALIGN_RIGHT_MID, -110, 0);
    lv_obj_t *plus = make_button(panel, "+", COLOR_GREEN_DARK, plus_action, 58, 50);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, -24, 0);
    return panel;
}

void AppUi::render_settings()
{
    screen_type_ = Screen::Settings;
    clear_screen();
    make_top_bar("设置", "设备与 FSRS 参数", true);

    make_stepper(screen_, 90, "每天最多学习的新单词", edit_settings_.new_words_per_day,
                 ACTION_NEW_MINUS, ACTION_NEW_PLUS);
    make_stepper(screen_, 175, "单次会话最多卡片", edit_settings_.session_card_limit,
                 ACTION_SESSION_MINUS, ACTION_SESSION_PLUS);
    make_stepper(screen_, 260, "发音音量", edit_settings_.audio_volume,
                 ACTION_VOLUME_MINUS, ACTION_VOLUME_PLUS);

    lv_obj_t *log_panel = make_panel(screen_, 140, 345, 490, 76, COLOR_PANEL, 16);
    lv_obj_t *log_label = make_label(log_panel, "审查日志", &font_zh_28, COLOR_TEXT);
    lv_obj_align(log_label, LV_ALIGN_LEFT_MID, 24, 0);
    make_button(log_panel, edit_settings_.write_review_log ? "开启" : "关闭",
                edit_settings_.write_review_log ? COLOR_GREEN : COLOR_GREEN_DARK,
                ACTION_LOG_TOGGLE, 120, 50);
    lv_obj_t *log_button = lv_obj_get_child(log_panel, lv_obj_get_child_count(log_panel) - 1);
    lv_obj_align(log_button, LV_ALIGN_RIGHT_MID, -20, 0);

    lv_obj_t *wifi_panel = make_panel(screen_, 650, 345, 490, 76, COLOR_PANEL, 16);
    lv_obj_t *wifi_label = make_label(wifi_panel, "WiFi", &font_zh_28, COLOR_TEXT);
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 22, 0);
    wifi_status_label_ = make_label(wifi_panel, "…", &font_zh_28, COLOR_MUTED);
    lv_obj_set_width(wifi_status_label_, 150);
    lv_obj_align(wifi_status_label_, LV_ALIGN_LEFT_MID, 94, 0);
    make_button(wifi_panel, "配置", COLOR_GREEN_DARK, ACTION_WIFI_CONFIG, 88, 50);
    lv_obj_t *wifi_config =
        lv_obj_get_child(wifi_panel, lv_obj_get_child_count(wifi_panel) - 1);
    lv_obj_align(wifi_config, LV_ALIGN_RIGHT_MID, -120, 0);
    make_button(wifi_panel, wifi_.enabled() ? "关闭" : "开启",
                wifi_.enabled() ? COLOR_GREEN_DARK : COLOR_AMBER,
                ACTION_WIFI_TOGGLE, 88, 50);
    lv_obj_t *wifi_toggle =
        lv_obj_get_child(wifi_panel, lv_obj_get_child_count(wifi_panel) - 1);
    lv_obj_align(wifi_toggle, LV_ALIGN_RIGHT_MID, -20, 0);
    update_wifi_status();

    const PowerStatus power = power_.status();
    char battery[96];
    if (power.available && power.percent >= 0) {
        std::snprintf(battery, sizeof(battery), "%d%% · %.2fV%s",
                      power.percent, power.voltage,
                      power.usb_present ? " · USB" : "");
    } else if (power.usb_present) {
        std::snprintf(battery, sizeof(battery), "检测不可用 · USB 已接入");
    } else {
        std::snprintf(battery, sizeof(battery), "检测不可用");
    }

    std::string wifi_short;
    switch (wifi_.state()) {
    case WifiService::State::Connected:
        wifi_short = "已连接";
        break;
    case WifiService::State::Scanning:
        wifi_short = "扫描中";
        break;
    case WifiService::State::Connecting:
        wifi_short = "连接中";
        break;
    case WifiService::State::Error:
        wifi_short = "异常";
        break;
    case WifiService::State::Disabled:
        wifi_short = "关闭";
        break;
    default:
        wifi_short = wifi_.enabled() ? "已开启" : "关闭";
        break;
    }

    char detail[640];
    std::snprintf(detail, sizeof(detail),
                  "FSRS-6 默认权重 · 目标保持率 90%%\n"
                  "认读 stability≥%.0f天解锁默写，默写 stability≥%.0f天记为熟练\n"
                  "本级熟练占比 %.0f%% 解锁下一等级\n"
                  "RTC %s · WiFi %s · 电池 %s",
                  edit_settings_.recognition_gate_days,
                  edit_settings_.dictation_gate_days,
                  edit_settings_.level_unlock_ratio * 100.0f,
                  clock_service::format_time(clock_service::now(), "%Y-%m-%d %H:%M").c_str(),
                  wifi_short.c_str(),
                  battery);
    lv_obj_t *detail_label = make_label(screen_, detail, &font_zh_28, COLOR_MUTED, 1000);
    lv_obj_set_pos(detail_label, 140, 438);

    lv_obj_t *save = make_button(screen_, "保存并返回", COLOR_GREEN, ACTION_SAVE_SETTINGS, 320, 56);
    lv_obj_set_pos(save, 340, 650);
    lv_obj_t *back = make_button(screen_, "取消", COLOR_GREEN_DARK, ACTION_HOME, 220, 56);
    lv_obj_set_pos(back, 700, 650);
}

void AppUi::show_wifi_setup()
{
    wifi_return_to_settings_ = screen_type_ == Screen::Settings;
    wifi_page_ = WifiPage::List;
    selected_wifi_index_ = -1;
    selected_wifi_valid_ = false;
    wifi_password_.clear();
    wifi_connected_notified_ = false;
    if (!wifi_.enabled()) {
        wifi_.set_enabled(true);
    }
    render_wifi();
    wifi_.start_scan();
}

void AppUi::render_wifi()
{
    screen_type_ = Screen::Wifi;
    clear_screen();
    make_top_bar("WiFi 设置", "网络配置", false);

    wifi_status_label_ =
        make_label(screen_, wifi_.status_text().c_str(), &font_zh_28,
                   wifi_.state() == WifiService::State::Error ? COLOR_RED : COLOR_MUTED,
                   1120);
    lv_obj_set_pos(wifi_status_label_, 70, 86);

    if (wifi_page_ == WifiPage::List) {
        const std::vector<WifiService::Network> networks = wifi_.networks();
        if (networks.empty()) {
            lv_obj_t *empty = make_panel(screen_, 170, 180, 940, 300, COLOR_PANEL, 22);
            lv_obj_t *text = make_label(
                empty,
                wifi_.state() == WifiService::State::Scanning
                    ? "正在搜索附近的 WiFi 网络...\n请稍候"
                    : "没有发现可用网络。\n请确认 WiFi 已开启，然后点击“重新扫描”。",
                &font_zh_28, COLOR_TEXT, 860);
            lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(text);
        } else {
            lv_obj_t *list = lv_obj_create(screen_);
            lv_obj_set_pos(list, 70, 142);
            lv_obj_set_size(list, 1140, 452);
            lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(list, 0, 0);
            lv_obj_set_style_pad_all(list, 0, 0);
            lv_obj_set_scroll_dir(list, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
            for (size_t i = 0; i < networks.size(); ++i) {
                const WifiService::Network &network = networks[i];
                char label[128];
                std::snprintf(label, sizeof(label), "%s    %s · 信号%s",
                              network.ssid.c_str(),
                              WifiService::security_name(network.auth).c_str(),
                              signal_name(network.rssi));
                lv_obj_t *button =
                    make_button(list, label, COLOR_PANEL_2,
                                ACTION_WIFI_SELECT_BASE + i, 1112, 76);
                lv_obj_set_pos(button, 0, static_cast<lv_coord_t>(i * 88));
                lv_obj_t *button_label = lv_obj_get_child(button, 0);
                lv_obj_set_width(button_label, 1070);
                lv_label_set_long_mode(button_label, LV_LABEL_LONG_DOT);
                lv_obj_set_style_text_align(button_label,
                                            LV_TEXT_ALIGN_CENTER, 0);
            }
        }

        lv_obj_t *refresh =
            make_button(screen_, "重新扫描", COLOR_GREEN_DARK, ACTION_WIFI_REFRESH,
                        210, 58);
        lv_obj_set_pos(refresh, 70, 620);
        lv_obj_t *skip =
            make_button(screen_, "跳过并关闭 WiFi", COLOR_AMBER, ACTION_WIFI_SKIP,
                        360, 58);
        lv_obj_set_pos(skip, 300, 620);
        lv_obj_t *back =
            make_button(screen_, "返回", COLOR_GREEN_DARK, ACTION_HOME, 220, 58);
        lv_obj_set_pos(back, 990, 620);
    } else {
        if (!selected_wifi_valid_) {
            wifi_page_ = WifiPage::List;
            render_wifi();
            return;
        }
        const WifiService::Network &network = selected_wifi_network_;

        lv_obj_t *panel = make_panel(screen_, 70, 145, 1140, 410, COLOR_PANEL, 22);
        char title[128];
        std::snprintf(title, sizeof(title), "%s  ·  %s",
                      network.ssid.c_str(),
                      WifiService::security_name(network.auth).c_str());
        lv_obj_t *ssid_label =
            make_label(panel, title, &font_zh_28, COLOR_TEXT, 1060);
        lv_obj_set_pos(ssid_label, 40, 28);

        lv_obj_t *password_box =
            make_panel(panel, 40, 105, 1060, 82, COLOR_PANEL_2, 14);
        std::string masked = wifi_password_;
        std::fill(masked.begin(), masked.end(), '*');
        char password_text[160];
        std::snprintf(password_text, sizeof(password_text), "%s%s",
                      wifi_password_.empty() ? "" : "密码  ",
                      masked.c_str());
        lv_obj_t *input = make_label(
            password_box,
            wifi_password_.empty() ? "请输入 WPA 密码" : password_text,
            &font_zh_28, wifi_password_.empty() ? COLOR_MUTED : COLOR_GREEN, 1010);
        lv_obj_align(input, LV_ALIGN_LEFT_MID, 24, 0);

        char hint[320];
        std::snprintf(hint, sizeof(hint),
                      "使用 Tab5 键盘输入密码，Enter 连接，Esc 返回网络列表。\n"
                      "已输入 %zu / 63 位；保存后会记住此网络，下次开机自动连接。",
                      wifi_password_.size());
        lv_obj_t *hint_label =
            make_label(panel, hint, &font_zh_28, COLOR_MUTED, 1060);
        lv_obj_set_pos(hint_label, 40, 225);

        lv_obj_t *back =
            make_button(screen_, "返回网络列表", COLOR_GREEN_DARK,
                        ACTION_WIFI_BACK_LIST, 270, 58);
        lv_obj_set_pos(back, 70, 620);
        lv_obj_t *erase =
            make_button(screen_, "删除一位", COLOR_AMBER,
                        ACTION_WIFI_BACKSPACE, 180, 58);
        lv_obj_set_pos(erase, 830, 620);
        lv_obj_t *connect =
            make_button(screen_, "连接", COLOR_GREEN, ACTION_WIFI_CONNECT,
                        200, 58);
        lv_obj_set_pos(connect, 1030, 620);
    }

    last_wifi_revision_ = wifi_.network_revision();
    last_wifi_state_ = wifi_.state();
    update_wifi_status();
    update_power_widget();
}

void AppUi::select_wifi_network(int index)
{
    const std::vector<WifiService::Network> networks = wifi_.networks();
    if (index < 0 || index >= static_cast<int>(networks.size())) {
        return;
    }
    selected_wifi_index_ = index;
    selected_wifi_network_ = networks[index];
    selected_wifi_valid_ = true;
    wifi_password_.clear();
    if (WifiService::is_open(selected_wifi_network_.auth)) {
        wifi_page_ = WifiPage::Password;
        connect_selected_wifi();
        return;
    }
    wifi_page_ = WifiPage::Password;
    render_wifi();
}

void AppUi::connect_selected_wifi()
{
    if (!selected_wifi_valid_) {
        return;
    }
    const WifiService::Network &network = selected_wifi_network_;
    if (!WifiService::is_open(network.auth) && wifi_password_.size() < 8) {
        show_toast("加密 WiFi 密码至少需要 8 位");
        return;
    }
    if (!wifi_.save_and_connect(network, wifi_password_)) {
        update_wifi_status();
        return;
    }
    if (wifi_status_label_ != nullptr) {
        lv_label_set_text(wifi_status_label_, wifi_.status_text().c_str());
        lv_obj_set_style_text_color(wifi_status_label_,
                                    lv_color_hex(COLOR_GREEN), 0);
    }
}

void AppUi::toggle_reveal()
{
    if (screen_type_ != Screen::Study || current_.mode == CardMode::Dictation) {
        return;
    }
    reveal_ = true;
    render_study();
}

void AppUi::play_audio(bool british)
{
    if (!audio_.play(current_word_, british)) {
        show_toast("发音文件不存在");
    }
}

void AppUi::rate_current(int rating)
{
    if (screen_type_ != Screen::Study || (!reveal_ && current_.mode != CardMode::Dictation)) {
        return;
    }
    if (current_.mode == CardMode::Dictation && !dictation_checked_) {
        return;
    }
    ReviewEffects effects;
    if (!db_.review_card(current_, rating, effects)) {
        show_toast("保存学习进度失败");
        return;
    }
    ++session_done_;

    const int64_t now = clock_service::now();
    if ((current_.state == CardState::Learning || current_.state == CardState::Relearning) &&
        current_.due <= now + 30 * 60) {
        auto position = std::lower_bound(queue_.begin(), queue_.end(), current_,
                                         [](const Card &a, const Card &b) {
                                             return a.due < b.due;
                                         });
        queue_.insert(position, current_);
    }

    next_card();
    if (effects.dictation_unlocked) {
        show_toast("这两种认读都熟练了，已解锁默写卡");
    } else if (effects.level_unlocked) {
        show_toast("等级已解锁：" + upper_level(effects.new_level));
    } else if (effects.word_mastered) {
        show_toast("这个单词已经达到熟练");
    }
}

void AppUi::check_dictation()
{
    if (screen_type_ != Screen::Study || current_.mode != CardMode::Dictation ||
        dictation_checked_) {
        return;
    }
    dictation_correct_ = normalized_word(typed_) == normalized_word(current_word_.word);
    dictation_checked_ = true;
    reveal_ = true;
    render_study();
}

std::string AppUi::normalized_word(const std::string &value) const
{
    std::string result;
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            result.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return result;
}

int AppUi::dictation_letter_count() const
{
    int count = 0;
    for (unsigned char ch : current_word_.word) {
        if (std::isalnum(ch)) {
            ++count;
        }
    }
    return count;
}

void AppUi::handle_key(const KeyEvent &event)
{
    ESP_LOGD(TAG, "key event type=%d text='%s'", static_cast<int>(event.type), event.text);
    if (display_sleeping_) {
        wake_display();
        return;
    }
    lv_display_trigger_activity(display_);
    if (event.type == KeyEvent::Type::Screenshot) {
        capture_screenshot();
        return;
    }
    if (screen_type_ == Screen::Wifi) {
        if (wifi_page_ == WifiPage::Password) {
            if (event.type == KeyEvent::Type::Escape) {
                wifi_page_ = WifiPage::List;
                wifi_password_.clear();
                render_wifi();
            } else if (event.type == KeyEvent::Type::Backspace) {
                if (!wifi_password_.empty()) {
                    wifi_password_.pop_back();
                    render_wifi();
                }
            } else if (event.type == KeyEvent::Type::Enter) {
                connect_selected_wifi();
            } else if (event.type == KeyEvent::Type::Text) {
                const std::string text(event.text);
                for (unsigned char ch : text) {
                    if (ch >= 0x20 && ch <= 0x7E && wifi_password_.size() < 63) {
                        wifi_password_.push_back(static_cast<char>(ch));
                    }
                }
                render_wifi();
            }
        } else if (event.type == KeyEvent::Type::Escape) {
            if (wifi_return_to_settings_) {
                render_settings();
            } else {
                show_home();
            }
        }
        return;
    }
    if (screen_type_ != Screen::Study) {
        if (screen_type_ == Screen::Settings &&
            event.type == KeyEvent::Type::Escape) {
            show_home();
        }
        return;
    }
    if (event.type == KeyEvent::Type::Escape) {
        show_home();
        return;
    }
    if (current_.mode == CardMode::Dictation && !dictation_checked_) {
        if (event.type == KeyEvent::Type::Backspace) {
            if (!typed_.empty()) {
                typed_.pop_back();
                render_study();
            }
            return;
        }
        if (event.type == KeyEvent::Type::Enter) {
            check_dictation();
            return;
        }
        if (event.type == KeyEvent::Type::Text) {
            const int limit = dictation_letter_count();
            bool changed = false;
            for (const unsigned char ch : std::string(event.text)) {
                if (std::isalnum(ch) && static_cast<int>(typed_.size()) < limit) {
                    typed_.push_back(static_cast<char>(std::tolower(ch)));
                    changed = true;
                }
            }
            if (changed) {
                render_study();
            }
        }
        return;
    }
    if (!reveal_ && (event.type == KeyEvent::Type::Enter || is_blank_text(event))) {
        toggle_reveal();
    }
}

void AppUi::capture_screenshot()
{
    const int64_t requested_at = clock_service::now();
    if (last_screenshot_at_ > 0 && requested_at - last_screenshot_at_ < 2) {
        return;
    }
    ESP_LOGI(TAG, "screenshot requested");
    mkdir("/sdcard/screenshots", 0777);
    lv_draw_buf_t *snapshot = lv_snapshot_take(screen_, LV_COLOR_FORMAT_RGB565);
    if (snapshot == nullptr || snapshot->data == nullptr) {
        ESP_LOGE(TAG, "snapshot failed");
        show_toast("截图失败");
        if (snapshot != nullptr) {
            lv_draw_buf_destroy(snapshot);
        }
        return;
    }

    const uint32_t width = snapshot->header.w;
    const uint32_t height = snapshot->header.h;
    const uint32_t stride = snapshot->header.stride;
    const uint32_t row_bytes = (width * 3 + 3) & ~3U;
    const uint32_t image_size = row_bytes * height;
    const uint32_t file_size = 54 + image_size;
    const std::string stamp =
        clock_service::format_time(clock_service::now(), "%Y%m%d_%H%M%S");
    char path[160];
    std::snprintf(path, sizeof(path), "/sdcard/screenshots/%s_%s_%02d.bmp",
                  screen_name(screen_type_), stamp.c_str(), screenshot_sequence_++);

    FILE *file = std::fopen(path, "wb");
    if (file == nullptr) {
        lv_draw_buf_destroy(snapshot);
        show_toast("无法写入截图文件");
        return;
    }

    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    put_le32(header + 2, file_size);
    put_le32(header + 10, 54);
    put_le32(header + 14, 40);
    put_le32(header + 18, width);
    put_le32(header + 22, height);
    put_le16(header + 26, 1);
    put_le16(header + 28, 24);
    put_le32(header + 34, image_size);
    put_le32(header + 38, 2835);
    put_le32(header + 42, 2835);
    std::fwrite(header, 1, sizeof(header), file);

    std::vector<uint8_t> row(row_bytes, 0);
    for (int y = static_cast<int>(height) - 1; y >= 0; --y) {
        const auto *src = reinterpret_cast<const uint16_t *>(
            snapshot->data + static_cast<size_t>(y) * stride);
        for (uint32_t x = 0; x < width; ++x) {
            const uint16_t pixel = src[x];
            const uint8_t r = static_cast<uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
            const uint8_t g = static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
            const uint8_t b = static_cast<uint8_t>((pixel & 0x1F) * 255 / 31);
            row[x * 3] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
        std::fwrite(row.data(), 1, row_bytes, file);
    }
    std::fclose(file);
    lv_draw_buf_destroy(snapshot);
    last_screenshot_at_ = clock_service::now();
    ESP_LOGI(TAG, "screenshot saved: %s (%ux%u)", path, width, height);
    show_toast(std::string("截图已保存：") + path);
}

void AppUi::poll_keyboard()
{
    KeyEvent event;
    while (keyboard_.poll(event, 0)) {
        handle_key(event);
    }
}

void AppUi::handle_action(int action)
{
    if (action >= ACTION_WIFI_SELECT_BASE) {
        select_wifi_network(action - ACTION_WIFI_SELECT_BASE);
        return;
    }
    switch (action) {
    case ACTION_WAKE:
        wake_display();
        break;
    case ACTION_START:
        start_session();
        break;
    case ACTION_SETTINGS:
        edit_settings_ = db_.settings();
        render_settings();
        break;
    case ACTION_HOME:
        if (screen_type_ == Screen::Wifi && wifi_return_to_settings_) {
            render_settings();
        } else {
            show_home();
        }
        break;
    case ACTION_REVEAL:
        toggle_reveal();
        break;
    case ACTION_CHECK:
        check_dictation();
        break;
    case ACTION_RATE_AGAIN:
        rate_current(1);
        break;
    case ACTION_RATE_HARD:
        rate_current(2);
        break;
    case ACTION_RATE_GOOD:
        rate_current(3);
        break;
    case ACTION_RATE_EASY:
        rate_current(4);
        break;
    case ACTION_AUDIO_UK:
        play_audio(true);
        break;
    case ACTION_AUDIO_US:
        play_audio(false);
        break;
    case ACTION_NEW_MINUS:
        edit_settings_.new_words_per_day = std::max(5, edit_settings_.new_words_per_day - 5);
        render_settings();
        break;
    case ACTION_NEW_PLUS:
        edit_settings_.new_words_per_day = std::min(30, edit_settings_.new_words_per_day + 5);
        render_settings();
        break;
    case ACTION_SESSION_MINUS:
        edit_settings_.session_card_limit = std::max(20, edit_settings_.session_card_limit - 20);
        render_settings();
        break;
    case ACTION_SESSION_PLUS:
        edit_settings_.session_card_limit = std::min(200, edit_settings_.session_card_limit + 20);
        render_settings();
        break;
    case ACTION_VOLUME_MINUS:
        edit_settings_.audio_volume = std::max(0, edit_settings_.audio_volume - 10);
        audio_.set_volume(edit_settings_.audio_volume);
        render_settings();
        break;
    case ACTION_VOLUME_PLUS:
        edit_settings_.audio_volume = std::min(100, edit_settings_.audio_volume + 10);
        audio_.set_volume(edit_settings_.audio_volume);
        render_settings();
        break;
    case ACTION_LOG_TOGGLE:
        edit_settings_.write_review_log = !edit_settings_.write_review_log;
        render_settings();
        break;
    case ACTION_SAVE_SETTINGS:
        if (db_.save_settings(edit_settings_)) {
            audio_.set_volume(edit_settings_.audio_volume);
            show_home();
            show_toast("设置已保存");
        } else {
            show_toast("设置保存失败");
        }
        break;
    case ACTION_WIFI_TOGGLE: {
        const bool enable = !wifi_.enabled();
        wifi_.set_enabled(enable);
        if (enable && !wifi_.configured()) {
            show_wifi_setup();
        } else {
            render_settings();
            show_toast(enable ? "WiFi 已开启" : "WiFi 已关闭，进入飞机模式");
        }
        break;
    }
    case ACTION_WIFI_CONFIG:
        show_wifi_setup();
        break;
    case ACTION_WIFI_REFRESH:
        wifi_page_ = WifiPage::List;
        selected_wifi_index_ = -1;
        selected_wifi_valid_ = false;
        wifi_password_.clear();
        wifi_.start_scan();
        render_wifi();
        break;
    case ACTION_WIFI_SKIP:
        wifi_.set_enabled(false);
        wifi_.mark_setup_dismissed();
        show_home();
        show_toast("已跳过 WiFi 配置，设备保持离线");
        break;
    case ACTION_WIFI_BACK_LIST:
        wifi_page_ = WifiPage::List;
        selected_wifi_index_ = -1;
        selected_wifi_valid_ = false;
        wifi_password_.clear();
        render_wifi();
        break;
    case ACTION_WIFI_CONNECT:
        connect_selected_wifi();
        break;
    case ACTION_WIFI_BACKSPACE:
        if (!wifi_password_.empty()) {
            wifi_password_.pop_back();
            render_wifi();
        }
        break;
    default:
        break;
    }
}

void AppUi::action_event(lv_event_t *event)
{
    auto self = static_cast<AppUi *>(lv_event_get_user_data(event));
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (self == nullptr || target == nullptr) {
        return;
    }
    const int action = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(target)));
    self->handle_action(action);
}

void AppUi::update_power_widget()
{
    if (battery_bar_ == nullptr || battery_label_ == nullptr) {
        return;
    }

    char text[24];
    if (!power_status_.available || power_status_.percent < 0) {
        std::snprintf(text, sizeof(text), "%s",
                      power_status_.usb_present ? "USB" : "--%");
        lv_bar_set_value(battery_bar_, 0, LV_ANIM_OFF);
    } else {
        std::snprintf(text, sizeof(text), "%d%%%s", power_status_.percent,
                      power_status_.usb_present ? "+" : "");
        lv_bar_set_value(battery_bar_,
                         std::clamp(power_status_.percent, 0, 100), LV_ANIM_OFF);
    }
    lv_label_set_text(battery_label_, text);

    uint32_t color = COLOR_GREEN;
    if (power_status_.available && power_status_.percent <= 15) {
        color = COLOR_RED;
    } else if (power_status_.usb_present) {
        color = COLOR_TEAL;
    }
    lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(color), LV_PART_INDICATOR);
    if (battery_terminal_ != nullptr) {
        lv_obj_set_style_bg_color(battery_terminal_, lv_color_hex(color), 0);
    }
    lv_obj_set_style_text_color(
        battery_label_,
        lv_color_hex(power_status_.available && power_status_.percent <= 15
                         ? COLOR_RED
                         : COLOR_TEXT),
        0);
}

void AppUi::update_power_status()
{
    const int previous_percent = power_status_.percent;
    power_status_ = power_.update();

    if (power_status_.usb_present && !last_usb_present_) {
        show_toast("USB 已接入，正在使用外部电源");
    }
    last_usb_present_ = power_status_.usb_present;

    if (power_status_.charging && power_status_.percent > 30) {
        low_battery_warning_percent_ = 100;
    }
    if (power_status_.available && power_status_.percent >= 0) {
        const int thresholds[] = {20, 10, 5};
        for (int threshold : thresholds) {
            if (previous_percent > threshold &&
                power_status_.percent <= threshold &&
                low_battery_warning_percent_ > threshold) {
                char message[96];
                std::snprintf(message, sizeof(message),
                              "电量仅剩 %d%%，请尽快连接充电器",
                              power_status_.percent);
                show_toast(message);
                low_battery_warning_percent_ = threshold;
                break;
            }
        }
    }
    update_power_widget();
}

void AppUi::update_wifi_status()
{
    if (wifi_status_label_ == nullptr) {
        return;
    }

    std::string text;
    if (screen_type_ == Screen::Wifi) {
        text = wifi_.status_text();
    } else {
        switch (wifi_.state()) {
        case WifiService::State::Disabled:
            text = "关闭";
            break;
        case WifiService::State::Connected:
            text = "已连接";
            break;
        case WifiService::State::Scanning:
            text = "扫描中";
            break;
        case WifiService::State::Connecting:
            text = "连接中";
            break;
        case WifiService::State::Error:
            text = "异常";
            break;
        default:
            text = wifi_.enabled() ? "已开启" : "关闭";
            break;
        }
    }
    lv_label_set_text(wifi_status_label_, text.c_str());
    lv_obj_set_style_text_color(
        wifi_status_label_,
        lv_color_hex(wifi_.state() == WifiService::State::Error
                         ? COLOR_RED
                         : wifi_.state() == WifiService::State::Connected
                               ? COLOR_GREEN
                               : COLOR_MUTED),
        0);
}

void AppUi::enter_sleep()
{
    if (display_sleeping_ || screen_ == nullptr) {
        return;
    }
    sleep_overlay_ = lv_obj_create(screen_);
    lv_obj_set_pos(sleep_overlay_, 0, 0);
    lv_obj_set_size(sleep_overlay_, 1280, 720);
    lv_obj_set_style_bg_color(sleep_overlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(sleep_overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sleep_overlay_, 0, 0);
    lv_obj_set_style_radius(sleep_overlay_, 0, 0);
    lv_obj_set_style_pad_all(sleep_overlay_, 0, 0);
    lv_obj_add_flag(sleep_overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(
        sleep_overlay_,
        reinterpret_cast<void *>(static_cast<intptr_t>(ACTION_WAKE)));
    lv_obj_add_event_cb(sleep_overlay_, action_event, LV_EVENT_CLICKED, this);
    lv_obj_move_foreground(sleep_overlay_);
    display_sleeping_ = true;
    bsp_display_backlight_off();
    ESP_LOGI(TAG, "display sleep after 5 minutes of inactivity");
}

void AppUi::wake_display()
{
    if (!display_sleeping_) {
        return;
    }
    if (sleep_overlay_ != nullptr) {
        lv_obj_delete(sleep_overlay_);
        sleep_overlay_ = nullptr;
    }
    bsp_display_backlight_on();
    display_sleeping_ = false;
    lv_display_trigger_activity(display_);
    ESP_LOGI(TAG, "display awake");
}

void AppUi::timer_event(lv_timer_t *timer)
{
    auto self = static_cast<AppUi *>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->poll_keyboard();
        const uint32_t now_ms =
            static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now_ms - self->last_power_update_ms_ >= 1000) {
            self->last_power_update_ms_ = now_ms;
            self->update_power_status();
        }
        if (now_ms - self->last_wifi_update_ms_ >= 500) {
            self->last_wifi_update_ms_ = now_ms;
            self->update_wifi_status();
            if (self->screen_type_ == Screen::Wifi) {
                const WifiService::State wifi_state = self->wifi_.state();
                const uint32_t revision = self->wifi_.network_revision();
                if (self->wifi_page_ == WifiPage::List &&
                    revision != self->last_wifi_revision_) {
                    self->render_wifi();
                } else if (self->wifi_page_ == WifiPage::Password &&
                           wifi_state == WifiService::State::Connected &&
                           !self->wifi_connected_notified_) {
                    self->wifi_connected_notified_ = true;
                    const std::string ssid = self->wifi_.connected_ssid();
                    self->show_home();
                    self->show_toast("WiFi 已连接：" + ssid);
                } else {
                    self->update_wifi_status();
                }
                self->last_wifi_state_ = wifi_state;
            } else if (self->screen_type_ == Screen::Settings) {
                self->update_wifi_status();
            }
        }
        if (!self->display_sleeping_ &&
            lv_display_get_inactive_time(self->display_) >= 300000) {
            self->enter_sleep();
        }
    }
}

void AppUi::show_toast(const std::string &text)
{
    if (screen_ == nullptr) {
        return;
    }
    if (toast_timer_ != nullptr) {
        lv_timer_delete(toast_timer_);
        toast_timer_ = nullptr;
    }
    if (toast_obj_ != nullptr) {
        lv_obj_delete(toast_obj_);
        toast_obj_ = nullptr;
    }
    lv_obj_t *toast = make_panel(screen_, 340, 80, 600, 58, COLOR_GREEN_DARK, 16);
    lv_obj_set_style_border_color(toast, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_t *label = make_label(toast, text.c_str(), &font_zh_28, COLOR_TEXT, 550);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    toast_obj_ = toast;
    toast_timer_ = lv_timer_create(toast_timer_event, 2200, this);
    lv_timer_set_repeat_count(toast_timer_, 1);
}

void AppUi::toast_timer_event(lv_timer_t *timer)
{
    auto self = static_cast<AppUi *>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    if (self->toast_obj_ != nullptr) {
        lv_obj_delete(self->toast_obj_);
        self->toast_obj_ = nullptr;
    }
    self->toast_timer_ = nullptr;
}

void AppUi::update_keyboard_indicator()
{
    if (keyboard_status_label_ != nullptr) {
        lv_label_set_text(keyboard_status_label_,
                          keyboard_.connected() ? "键盘已连接" : "键盘未连接");
    }
}
