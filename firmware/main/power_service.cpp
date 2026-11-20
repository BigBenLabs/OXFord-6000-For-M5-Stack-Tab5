#include "power_service.h"

#include "bsp/m5stack_tab5.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr const char *TAG = "power";
constexpr uint8_t INA226_ADDR = 0x41;
constexpr uint8_t REG_CONFIG = 0x00;
constexpr uint8_t REG_BUS_VOLTAGE = 0x02;
constexpr uint8_t REG_CURRENT = 0x04;
constexpr uint8_t REG_CALIBRATION = 0x05;

constexpr uint16_t CONFIG =
    (2 << 9) |  // 16 averages
    (4 << 6) |  // 1.1 ms bus conversion
    (4 << 3) |  // 1.1 ms shunt conversion
    0x07;       // continuous bus and shunt sampling

// Tab5 uses a 5 mOhm shunt and expects at most 8.192 A. This LSB matches
// the calibration used by the official M5Stack Tab5 firmware.
constexpr float CURRENT_LSB = 0.0003f;
constexpr uint16_t CALIBRATION = 3413;
constexpr float BATTERY_INTERNAL_RESISTANCE = 0.06f;

struct VoltagePoint {
    float voltage;
    int percent;
};

constexpr std::array<VoltagePoint, 14> VOLTAGE_CURVE = {{
    {8.40f, 100},
    {8.20f, 95},
    {8.10f, 90},
    {8.00f, 85},
    {7.90f, 75},
    {7.80f, 65},
    {7.70f, 55},
    {7.60f, 45},
    {7.50f, 35},
    {7.40f, 25},
    {7.20f, 15},
    {7.00f, 10},
    {6.80f, 5},
    {6.50f, 0},
}};

} // namespace

bool PowerService::read_register(uint8_t reg, uint16_t &value)
{
    if (device_ == nullptr) {
        return false;
    }
    uint8_t raw[2] = {};
    if (i2c_master_transmit_receive(device_, &reg, 1, raw, sizeof(raw), 50) != ESP_OK) {
        return false;
    }
    value = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
    return true;
}

bool PowerService::write_register(uint8_t reg, uint16_t value)
{
    if (device_ == nullptr) {
        return false;
    }
    const uint8_t raw[3] = {
        reg,
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF),
    };
    return i2c_master_transmit(device_, raw, sizeof(raw), 50) == ESP_OK;
}

bool PowerService::init()
{
    initialized_ = false;
    status_ = PowerStatus{};

    vTaskDelay(pdMS_TO_TICKS(100));
    for (uint8_t address : {INA226_ADDR, static_cast<uint8_t>(0x40)}) {
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = address;
        config.scl_speed_hz = 400000;
        esp_err_t err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &device_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "INA226 add at 0x%02X failed: %s", address,
                     esp_err_to_name(err));
            device_ = nullptr;
            continue;
        }

        const bool wrote =
            write_register(REG_CONFIG, CONFIG) &&
            write_register(REG_CALIBRATION, CALIBRATION);
        uint16_t readback = 0;
        uint16_t raw_bus = 0;
        bool config_read = false;
        for (int attempt = 0; attempt < 3 && !config_read; ++attempt) {
            vTaskDelay(pdMS_TO_TICKS(20));
            config_read = read_register(REG_CONFIG, readback);
        }
        const bool bus_read = read_register(REG_BUS_VOLTAGE, raw_bus);
        const float bus_voltage = static_cast<float>(raw_bus) * 0.00125f;
        ESP_LOGI(TAG, "INA226 probe 0x%02X wrote=%d config=0x%04X bus=%.2fV",
                 address, wrote, readback, bus_voltage);
        if (bus_read && bus_voltage >= 4.5f && bus_voltage <= 9.0f) {
            initialized_ = true;
            status_.available = true;
            ESP_LOGI(TAG, "INA226 ready at 0x%02X, shunt=0.005 ohm", address);
            break;
        }
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
    }

    if (!initialized_) {
        ESP_LOGW(TAG, "INA226 not found at 0x41 or 0x40");
        return false;
    }
    update();
    return status_.available;
}

bool PowerService::ensure_charging_enabled()
{
    bsp_set_usb_5v_en(true);
    bsp_set_charge_qc_en(true);
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_set_charge_en(true);
    charging_enabled_ = true;
    ESP_LOGI(TAG, "IP2326 charging enabled (CHG_EN high, QC input enabled)");
    return true;
}

int PowerService::voltage_to_percent(float voltage) const
{
    if (voltage >= VOLTAGE_CURVE.front().voltage) {
        return VOLTAGE_CURVE.front().percent;
    }
    if (voltage <= VOLTAGE_CURVE.back().voltage) {
        return VOLTAGE_CURVE.back().percent;
    }
    for (size_t i = 1; i < VOLTAGE_CURVE.size(); ++i) {
        const VoltagePoint high = VOLTAGE_CURVE[i - 1];
        const VoltagePoint low = VOLTAGE_CURVE[i];
        if (voltage <= high.voltage && voltage >= low.voltage) {
            const float span = high.voltage - low.voltage;
            const float ratio = span > 0.0f ? (voltage - low.voltage) / span : 0.0f;
            return static_cast<int>(std::lround(low.percent +
                                                ratio * (high.percent - low.percent)));
        }
    }
    return 0;
}

PowerStatus PowerService::update()
{
    status_.usb_present = bsp_usb_c_detect();
    if (!initialized_) {
        status_.charging = status_.usb_present;
        status_.percent = -1;
        return status_;
    }

    uint16_t raw_voltage = 0;
    uint16_t raw_current = 0;
    if (!read_register(REG_BUS_VOLTAGE, raw_voltage) ||
        !read_register(REG_CURRENT, raw_current)) {
        status_.available = false;
        status_.percent = -1;
        return status_;
    }

    const float raw_voltage_v = static_cast<float>(raw_voltage) * 0.00125f;
    const float current_a =
        static_cast<float>(static_cast<int16_t>(raw_current)) * CURRENT_LSB;
    if (raw_voltage_v < 4.5f || raw_voltage_v > 9.0f) {
        status_.available = false;
        status_.percent = -1;
        return status_;
    }

    if (!voltage_filtered_) {
        filtered_voltage_ = raw_voltage_v;
        voltage_filtered_ = true;
    } else {
        filtered_voltage_ = filtered_voltage_ * 0.75f + raw_voltage_v * 0.25f;
    }

    // Tab5's shunt is oriented so charging current is negative. While
    // charging, terminal voltage is above the open-circuit voltage; while
    // discharging, it is below it.
    const float open_circuit_voltage =
        filtered_voltage_ + current_a * BATTERY_INTERNAL_RESISTANCE;
    status_.available = true;
    status_.voltage = filtered_voltage_;
    status_.current = current_a;
    status_.charging = current_a < -0.02f;
    status_.percent =
        std::clamp(voltage_to_percent(open_circuit_voltage), 0, 100);

    const int64_t now_us = esp_timer_get_time();
    if (status_.percent != last_logged_percent_ || now_us - last_log_us_ > 10000000) {
        ESP_LOGI(TAG, "battery %.2fV %.3fA %d%% usb=%d charging=%d",
                 status_.voltage, status_.current, status_.percent,
                 status_.usb_present, status_.charging);
        last_logged_percent_ = status_.percent;
        last_log_us_ = now_us;
    }
    return status_;
}
