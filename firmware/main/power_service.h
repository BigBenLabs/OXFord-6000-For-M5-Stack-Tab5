#pragma once

#include "driver/i2c_master.h"

struct PowerStatus {
    bool available = false;
    bool usb_present = false;
    bool charging = false;
    int percent = -1;
    float voltage = 0.0f;
    float current = 0.0f;
};

class PowerService {
public:
    bool init();
    bool ensure_charging_enabled();
    PowerStatus update();
    PowerStatus status() const
    {
        return status_;
    }

private:
    bool read_register(uint8_t reg, uint16_t &value);
    bool write_register(uint8_t reg, uint16_t value);
    int voltage_to_percent(float voltage) const;

    i2c_master_dev_handle_t device_ = nullptr;
    bool initialized_ = false;
    bool charging_enabled_ = false;
    bool voltage_filtered_ = false;
    float filtered_voltage_ = 0.0f;
    int last_logged_percent_ = -1;
    int64_t last_log_us_ = 0;
    PowerStatus status_;
};
