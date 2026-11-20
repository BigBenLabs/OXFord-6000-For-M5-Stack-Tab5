#include "audio_service.h"
#include "bsp/esp-bsp.h"
#include "bsp/m5stack_tab5.h"
#include "clock_service.h"
#include "database.h"
#include "keyboard.h"
#include "power_service.h"
#include "ui.h"
#include "wifi_service.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

LV_FONT_DECLARE(font_zh_28);

static const char *TAG = "tab5_fsrs";

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(bsp_i2c_init());
    bsp_io_expander_pi4ioe_init(bsp_i2c_get_handle());
    static PowerService power;
    power.ensure_charging_enabled();
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!power.init()) {
        ESP_LOGW(TAG, "battery monitor unavailable; UI will still start");
    }
    bsp_reset_tp();
    clock_service::init();
    bsp_codec_init();

    bsp_display_cfg_t display_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size   = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = true,
        .flags         = {
            .buff_dma    = true,
            .buff_spiram = true,
            .sw_rotate   = true,
        },
    };
    lv_display_t *display = bsp_display_start_with_config(&display_cfg);
    if (display == nullptr) {
        ESP_LOGE(TAG, "display initialization failed");
        return;
    }
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    char mount_point[] = "/sdcard";
    const esp_err_t sd_result = bsp_sdcard_init(mount_point, 25);
    if (sd_result != ESP_OK) {
        ESP_LOGE(TAG, "TF card mount failed: %s", esp_err_to_name(sd_result));
    }

    static Database database;
    if (sd_result == ESP_OK && !database.begin()) {
        ESP_LOGE(TAG, "database init failed: %s", database.last_error().c_str());
    }

    static AudioService audio;
    if (!audio.init(70)) {
        ESP_LOGW(TAG, "audio unavailable; flashcards will still work");
    }

    static KeyboardService keyboard;
    if (!keyboard.init()) {
        ESP_LOGW(TAG, "keyboard unavailable; recognition cards still work");
    }

    static WifiService wifi;
    if (!wifi.begin()) {
        ESP_LOGW(TAG, "WiFi service unavailable; offline mode remains usable");
    }

    static AppUi ui(database, audio, keyboard, power, wifi);
    if (bsp_display_lock(1000)) {
        ui.init(display);
        if (database.ready()) {
            ui.show_home();
            if (wifi.needs_setup()) {
                ui.show_wifi_setup();
            }
        } else {
            const std::string message =
                sd_result != ESP_OK
                    ? "TF 卡挂载失败。请重新插卡后重启。"
                    : database.last_error();
            ui.show_fatal_error(message);
        }
        bsp_display_unlock();
    }

    ESP_LOGI(TAG, "Tab5 FSRS started, panel=%s, main stack free=%u bytes",
             bsp_display_get_panel_ic(),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) *
                                   sizeof(StackType_t)));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

