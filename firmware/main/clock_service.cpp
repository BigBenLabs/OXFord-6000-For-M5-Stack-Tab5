#include "clock_service.h"

#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include <sys/time.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace clock_service {
namespace {

constexpr const char *TAG = "rtc";
constexpr uint8_t RTC_ADDR = 0x32;
constexpr uint8_t REG_SEC = 0x10;
constexpr uint8_t REG_CTRL0 = 0x1E;
constexpr uint8_t REG_CTRL1 = 0x1F;
constexpr uint8_t CTRL_STOP = 1 << 6;

i2c_master_dev_handle_t rtc_dev = nullptr;

uint8_t bcd_to_dec(uint8_t value)
{
    return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0F));
}

uint8_t dec_to_bcd(uint8_t value)
{
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    if (rtc_dev == nullptr) {
        return false;
    }
    return i2c_master_transmit_receive(rtc_dev, &reg, 1, data, len, 100) == ESP_OK;
}

bool write_reg(uint8_t reg, const uint8_t *data, size_t len)
{
    if (rtc_dev == nullptr || len > 16) {
        return false;
    }
    uint8_t buffer[17];
    buffer[0] = reg;
    std::memcpy(buffer + 1, data, len);
    return i2c_master_transmit(rtc_dev, buffer, len + 1, 100) == ESP_OK;
}

bool read_time(std::tm &value)
{
    uint8_t raw[7];
    if (!read_reg(REG_SEC, raw, sizeof(raw))) {
        return false;
    }
    std::memset(&value, 0, sizeof(value));
    value.tm_sec = bcd_to_dec(raw[0] & 0x7F);
    value.tm_min = bcd_to_dec(raw[1] & 0x7F);
    value.tm_hour = bcd_to_dec(raw[2] & 0x3F);
    value.tm_mday = bcd_to_dec(raw[4] & 0x3F);
    value.tm_mon = bcd_to_dec(raw[5] & 0x1F) - 1;
    value.tm_year = bcd_to_dec(raw[6]) + 100;
    return true;
}

bool write_time(const std::tm &value)
{
    uint8_t ctrl = 0;
    if (!read_reg(REG_CTRL0, &ctrl, 1)) {
        return false;
    }
    const uint8_t stopped = ctrl | CTRL_STOP;
    write_reg(REG_CTRL0, &stopped, 1);

    std::tm copy = value;
    copy.tm_year = std::clamp(copy.tm_year, 100, 199);
    uint8_t raw[7] = {
        dec_to_bcd(static_cast<uint8_t>(copy.tm_sec)),
        dec_to_bcd(static_cast<uint8_t>(copy.tm_min)),
        dec_to_bcd(static_cast<uint8_t>(copy.tm_hour)),
        dec_to_bcd(static_cast<uint8_t>(copy.tm_wday)),
        dec_to_bcd(static_cast<uint8_t>(copy.tm_mday)),
        dec_to_bcd(static_cast<uint8_t>(copy.tm_mon + 1)),
        dec_to_bcd(static_cast<uint8_t>(copy.tm_year % 100)),
    };
    const bool ok = write_reg(REG_SEC, raw, sizeof(raw));
    write_reg(REG_CTRL0, &ctrl, 1);
    return ok;
}

std::tm build_time()
{
    std::tm value{};
    const char *text = __DATE__ " " __TIME__;
    strptime(text, "%b %d %Y %H:%M:%S", &value);
    value.tm_isdst = -1;
    return value;
}

bool apply_system_time(const std::tm &value)
{
    std::tm copy = value;
    copy.tm_isdst = -1;
    const std::time_t seconds = std::mktime(&copy);
    if (seconds <= 0) {
        return false;
    }
    timeval tv{};
    tv.tv_sec = seconds;
    return settimeofday(&tv, nullptr) == 0;
}

} // namespace

bool init()
{
    setenv("TZ", "CST-8", 1);
    tzset();

    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = RTC_ADDR;
    config.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &rtc_dev) != ESP_OK) {
        ESP_LOGW(TAG, "RX8130 not available");
        const std::tm fallback = build_time();
        apply_system_time(fallback);
        return false;
    }

    uint8_t battery = 0;
    if (read_reg(REG_CTRL1, &battery, 1)) {
        battery |= (1 << 4) | (1 << 5);
        write_reg(REG_CTRL1, &battery, 1);
    }
    uint8_t flag = 0;
    write_reg(0x1D, &flag, 1);

    std::tm rtc_time{};
    const bool rtc_ok = read_time(rtc_time) &&
                        rtc_time.tm_year >= 124 && rtc_time.tm_year <= 220 &&
                        rtc_time.tm_mday >= 1 && rtc_time.tm_mday <= 31;
    if (!rtc_ok) {
        std::tm fallback = build_time();
        write_time(fallback);
        apply_system_time(fallback);
        ESP_LOGW(TAG, "RTC time was invalid; initialized from build time");
        return false;
    }
    apply_system_time(rtc_time);
    ESP_LOGI(TAG, "system time set from RX8130: %04d-%02d-%02d %02d:%02d:%02d",
             rtc_time.tm_year + 1900, rtc_time.tm_mon + 1, rtc_time.tm_mday,
             rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec);
    return true;
}

int64_t now()
{
    return static_cast<int64_t>(std::time(nullptr));
}

int64_t start_of_day(int64_t timestamp)
{
    std::time_t raw = static_cast<std::time_t>(timestamp);
    std::tm local{};
    localtime_r(&raw, &local);
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&local));
}

std::string format_time(int64_t timestamp, const char *format)
{
    std::time_t raw = static_cast<std::time_t>(timestamp);
    std::tm local{};
    localtime_r(&raw, &local);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), format, &local);
    return buffer;
}

} // namespace clock_service
