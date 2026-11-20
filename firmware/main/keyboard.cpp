#include "keyboard.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <strings.h>

namespace {

constexpr const char *TAG = "keyboard";
constexpr uint8_t KEYBOARD_ADDR = 0x6D;
constexpr gpio_num_t KEYBOARD_INT = GPIO_NUM_50;
constexpr i2c_port_num_t KEYBOARD_I2C_PORT = I2C_NUM_1;
constexpr gpio_num_t KEYBOARD_SDA = GPIO_NUM_0;
constexpr gpio_num_t KEYBOARD_SCL = GPIO_NUM_1;

constexpr uint8_t REG_INT_CFG = 0x00;
constexpr uint8_t REG_INT_STAT = 0x01;
constexpr uint8_t REG_EVENT_NUM = 0x02;
constexpr uint8_t REG_BRIGHTNESS = 0x03;
constexpr uint8_t REG_MODE = 0x10;
constexpr uint8_t REG_CHAR_LEN = 0x40;
constexpr uint8_t REG_CHAR_EVENT = 0x50;

void copy_text(KeyEvent &event, const char *text, size_t length)
{
    event.type = KeyEvent::Type::Text;
    length = std::min(length, sizeof(event.text) - 1);
    std::memcpy(event.text, text, length);
    event.text[length] = '\0';
}

bool is_key_name(const char *text, size_t length, const char *name)
{
    return length == std::strlen(name) && strncasecmp(text, name, length) == 0;
}

} // namespace

bool KeyboardService::read_register(uint8_t reg, uint8_t *data, size_t len)
{
    auto device = static_cast<i2c_master_dev_handle_t>(device_);
    return device != nullptr &&
           i2c_master_transmit_receive(device, &reg, 1, data, len, 50) == ESP_OK;
}

bool KeyboardService::write_register(uint8_t reg, const uint8_t *data, size_t len)
{
    auto device = static_cast<i2c_master_dev_handle_t>(device_);
    if (device == nullptr || len > 8) {
        return false;
    }
    uint8_t buffer[9];
    buffer[0] = reg;
    std::memcpy(buffer + 1, data, len);
    return i2c_master_transmit(device, buffer, len + 1, 50) == ESP_OK;
}

bool KeyboardService::init()
{
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = KEYBOARD_I2C_PORT;
    bus_config.sda_io_num = KEYBOARD_SDA;
    bus_config.scl_io_num = KEYBOARD_SCL;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = 1;

    i2c_master_bus_handle_t bus = nullptr;
    if (i2c_new_master_bus(&bus_config, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "keyboard I2C bus init failed");
        return false;
    }
    bus_ = bus;

    if (i2c_master_probe(bus, KEYBOARD_ADDR, 100) != ESP_OK) {
        ESP_LOGE(TAG, "keyboard not found at 0x%02X on I2C1 (SDA=0, SCL=1)",
                 KEYBOARD_ADDR);
        return false;
    }

    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = KEYBOARD_ADDR;
    device_config.scl_speed_hz = 400000;
    i2c_master_dev_handle_t device = nullptr;
    if (i2c_master_bus_add_device(bus, &device_config, &device) != ESP_OK) {
        ESP_LOGE(TAG, "keyboard device add failed");
        return false;
    }
    device_ = device;
    connected_ = true;

    // Write the two mode bytes separately so this does not depend on the
    // device's register auto-increment behavior.
    const uint8_t character_mode = 2;
    const uint8_t binding_rgb = 0;
    const bool mode_ok = write_register(REG_MODE, &character_mode, 1);
    const bool rgb_ok = write_register(REG_MODE + 1, &binding_rgb, 1);
    const uint8_t brightness = 35;
    const bool brightness_ok = write_register(REG_BRIGHTNESS, &brightness, 1);
    const uint8_t int_config = 0x04; // character-mode interrupt
    const bool int_ok = write_register(REG_INT_CFG, &int_config, 1);
    const uint8_t clear = 0;
    write_register(REG_INT_STAT, &clear, 1);
    write_register(REG_EVENT_NUM, &clear, 1);
    uint8_t readback_mode = 0;
    uint8_t readback_int = 0;
    read_register(REG_MODE, &readback_mode, 1);
    read_register(REG_INT_CFG, &readback_int, 1);
    ESP_LOGI(TAG, "config writes: mode=%d rgb=%d brightness=%d int=%d; readback mode=0x%02X int=0x%02X",
             mode_ok, rgb_ok, brightness_ok, int_ok, readback_mode, readback_int);

    gpio_config_t gpio{};
    gpio.pin_bit_mask = 1ULL << KEYBOARD_INT;
    gpio.mode = GPIO_MODE_INPUT;
    gpio.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&gpio);

    if (gpio_install_isr_service(0) != ESP_OK) {
        ESP_LOGW(TAG, "GPIO ISR service already installed");
    }
    gpio_isr_handler_add(KEYBOARD_INT, interrupt_handler, this);

    QueueHandle_t queue = xQueueCreate(32, sizeof(KeyEvent));
    if (queue == nullptr) {
        return false;
    }
    queue_ = queue;
    if (xTaskCreate(task_entry, "tab5_keyboard", 4096, this, 8, nullptr) != pdPASS) {
        return false;
    }
    enabled_ = true;
    ESP_LOGI(TAG, "keyboard ready: I2C1 SDA=0 SCL=1 INT=50 addr=0x6D");
    return true;
}

void KeyboardService::interrupt_handler(void *arg)
{
    auto self = static_cast<KeyboardService *>(arg);
    if (self != nullptr && self->task_ != nullptr) {
        BaseType_t higher_priority_woken = pdFALSE;
        vTaskNotifyGiveFromISR(static_cast<TaskHandle_t>(self->task_),
                               &higher_priority_woken);
        portYIELD_FROM_ISR(higher_priority_woken);
    }
}

void KeyboardService::task_entry(void *arg)
{
    static_cast<KeyboardService *>(arg)->task_loop();
}

void KeyboardService::task_loop()
{
    task_ = xTaskGetCurrentTaskHandle();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        if (enabled_) {
            read_and_parse_events();
        }
    }
}

bool KeyboardService::read_and_parse_events()
{
    if (device_ == nullptr) {
        return false;
    }

    uint8_t status = 0;
    uint8_t count = 0;
    if (!read_register(REG_INT_STAT, &status, 1) ||
        !read_register(REG_EVENT_NUM, &count, 1)) {
        ESP_LOGW(TAG, "keyboard read failed");
        connected_ = false;
        return false;
    }
    connected_ = true;
    if ((status & 0x04) == 0 && count == 0) {
        return true;
    }
    ESP_LOGD(TAG, "keyboard status=0x%02X events=%u", status, count);

    QueueHandle_t queue = static_cast<QueueHandle_t>(queue_);
    const int iterations = std::min<int>(count, 32);
    for (int i = 0; i < iterations; ++i) {
        uint8_t length = 0;
        // The device reports the full event length, including the modifier
        // byte. A one-character event therefore has length == 2.
        if (!read_register(REG_CHAR_LEN, &length, 1) || length < 2 || length > 10) {
            break;
        }
        uint8_t event_data[10] = {};
        if (!read_register(REG_CHAR_EVENT, event_data, length)) {
            break;
        }
        const uint8_t modifier = event_data[0];
        const char *text = reinterpret_cast<const char *>(event_data + 1);
        const size_t text_length = length - 1;
        ESP_LOGD(TAG, "char event modifier=0x%02X len=%u text='%.*s'",
                 modifier, static_cast<unsigned>(text_length),
                 static_cast<int>(text_length), text);

        KeyEvent event;
        if ((modifier & 0x01) != 0 && text_length == 1 &&
            (text[0] == 's' || text[0] == 'S')) {
            event.type = KeyEvent::Type::Screenshot;
        } else if (is_key_name(text, text_length, "BACKSPACE") ||
            is_key_name(text, text_length, "BACK")) {
            event.type = KeyEvent::Type::Backspace;
        } else if (is_key_name(text, text_length, "ENTER") ||
                   is_key_name(text, text_length, "RETURN")) {
            event.type = KeyEvent::Type::Enter;
        } else if (is_key_name(text, text_length, "TAB")) {
            event.type = KeyEvent::Type::Tab;
        } else if (is_key_name(text, text_length, "ESC") ||
                   is_key_name(text, text_length, "ESCAPE")) {
            event.type = KeyEvent::Type::Escape;
        } else {
            for (size_t j = 0; j < text_length; ++j) {
                const unsigned char ch = static_cast<unsigned char>(text[j]);
                if (ch == 8 || ch == 127) {
                    event.type = KeyEvent::Type::Backspace;
                    break;
                }
                if (ch == 10 || ch == 13) {
                    event.type = KeyEvent::Type::Enter;
                    break;
                }
                if (ch == 9) {
                    event.type = KeyEvent::Type::Tab;
                    break;
                }
                if (ch == 27) {
                    event.type = KeyEvent::Type::Escape;
                    break;
                }
            }
            if (event.type == KeyEvent::Type::None) {
                // Ctrl/Alt are modifiers for host-style shortcuts; dictation only
                // consumes ordinary printable characters.
                if ((modifier & 0x05) == 0) {
                    copy_text(event, text, text_length);
                } else {
                    event.type = KeyEvent::Type::Other;
                }
            }
        }

        if (event.type != KeyEvent::Type::None) {
            ESP_LOGD(TAG, "queued key type=%d", static_cast<int>(event.type));
            xQueueSend(queue, &event, 0);
        }
    }

    const uint8_t clear = 0;
    write_register(REG_INT_STAT, &clear, 1);
    return true;
}

bool KeyboardService::poll(KeyEvent &event, unsigned timeout_ms)
{
    QueueHandle_t queue = static_cast<QueueHandle_t>(queue_);
    if (queue == nullptr) {
        return false;
    }
    return xQueueReceive(queue, &event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void KeyboardService::set_enabled(bool enabled)
{
    enabled_ = enabled;
}
