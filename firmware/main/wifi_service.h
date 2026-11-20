#pragma once

#include "esp_wifi_types.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class WifiService {
public:
    enum class State {
        Disabled,
        Starting,
        Idle,
        Scanning,
        Connecting,
        Connected,
        Error,
    };

    struct Network {
        std::string ssid;
        int rssi = -127;
        wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
    };

    bool begin();
    void set_enabled(bool enabled);
    bool enabled() const;
    bool configured() const;
    bool needs_setup() const;
    void mark_setup_dismissed();

    bool start_scan();
    bool save_and_connect(const Network &network, const std::string &password);

    State state() const;
    std::string status_text() const;
    std::string saved_ssid() const;
    std::string connected_ssid() const;
    std::string ip_address() const;
    std::vector<Network> networks() const;
    uint32_t network_revision() const;

    static bool is_open(wifi_auth_mode_t auth);
    static std::string security_name(wifi_auth_mode_t auth);

private:
    enum class CommandType : uint8_t {
        Enable,
        Disable,
        Scan,
        Connect,
    };

    struct Command {
        CommandType type = CommandType::Scan;
        char ssid[33] = {};
        char password[64] = {};
        uint8_t auth = 0;
    };

    static void task_entry(void *arg);
    static void wifi_event_handler(void *arg, const char *base, int32_t id, void *data);
    static void ip_event_handler(void *arg, const char *base, int32_t id, void *data);

    void task_loop();
    bool initialize_radio();
    bool start_radio();
    bool connect_internal(const std::string &ssid, const std::string &password,
                          wifi_auth_mode_t auth);
    bool scan_internal();
    void collect_scan_results();
    void load_preferences();
    bool store_preferences(const std::string &ssid, const std::string &password,
                           bool configured, bool enabled);
    void set_state(State state, const std::string &message = {});

    mutable std::mutex mutex_;
    void *queue_ = nullptr;
    void *task_ = nullptr;
    void *netif_ = nullptr;
    bool radio_initialized_ = false;
    bool radio_started_ = false;
    bool enabled_ = true;
    bool configured_ = false;
    bool setup_dismissed_ = false;
    State state_ = State::Disabled;
    std::string saved_ssid_;
    std::string saved_password_;
    wifi_auth_mode_t saved_auth_ = WIFI_AUTH_WPA2_PSK;
    std::string connected_ssid_;
    std::string ip_address_;
    std::string error_;
    std::vector<Network> networks_;
    uint32_t network_revision_ = 0;
};
