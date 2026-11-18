idf_component_register(
    SRCS
        "main.cpp"
        "fsrs.cpp"
        "database.cpp"
        "clock_service.cpp"
        "audio_service.cpp"
        "keyboard.cpp"
        "ui.cpp"
        "fonts/font_zh_28.c"
    INCLUDE_DIRS "."
    REQUIRES
        m5stack_tab5
        esp_lcd_st7121
        driver
        esp_lcd
        esp_lvgl_port
        lvgl
        chmorgan__esp-audio-player
        esp_sqlite3
        nvs_flash
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

