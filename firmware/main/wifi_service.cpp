#include "wifi_service.h"

#include "bsp/m5stack_tab5.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>
#include <map>

namespace {

constexpr const char *TAG = "wifi";
constexpr const char *NVS_NAMESPACE = "tab5_wifi";

} // namespace

bool WifiService::is_open(wifi_auth_mode_t auth)
{
    return auth == WIFI_AUTH_OPEN || auth == WIFI_AUTH_OWE;
}

std::string WifiService::security_name(wifi_auth_mode_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN:
        return "开放";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    default:
        return "加密";
    }
}

void WifiService::load_preferences()
{
    nvs_handle_t handle = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        enabled_ = true;
        configured_ = false;
        setup_dismissed_ = false;
        return;
    }

    uint8_t value = 1;
    enabled_ = nvs_get_u8(handle, "enabled", &value) == ESP_OK ? value != 0 : true;
    value = 0;
    configured_ = nvs_get_u8(handle, "configured", &value) == ESP_OK && value != 0;
    value = 0;
    setup_dismissed_ =
        nvs_get_u8(handle, "setup_dismissed", &value) == ESP_OK && value != 0;

    char ssid[33] = {};
    size_t ssid_size = sizeof(ssid);
    if (nvs_get_str(handle, "ssid", ssid, &ssid_size) == ESP_OK) {
        saved_ssid_ = ssid;
    }
    char password[64] = {};
    size_t password_size = sizeof(password);
    if (nvs_get_str(handle, "password", password, &password_size) == ESP_OK) {
        saved_password_ = password;
    }
    value = static_cast<uint8_t>(WIFI_AUTH_WPA2_PSK);
    if (nvs_get_u8(handle, "auth", &value) == ESP_OK) {
        saved_auth_ = static_cast<wifi_auth_mode_t>(value);
    }
    configured_ = configured_ && !saved_ssid_.empty();
    nvs_close(handle);
}

bool WifiService::store_preferences(const std::string &ssid,
                                    const std::string &password,
                                    bool configured, bool enabled)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    if (configured) {
        err = nvs_set_str(handle, "ssid", ssid.c_str());
        if (err == ESP_OK) {
            err = nvs_set_str(handle, "password", password.c_str());
        }
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, "auth", static_cast<uint8_t>(saved_auth_));
        }
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "configured", configured ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "enabled", enabled ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "setup_dismissed", 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool WifiService::begin()
{
    load_preferences();
    state_ = enabled_ ? State::Starting : State::Disabled;

    QueueHandle_t queue = xQueueCreate(8, sizeof(Command));
    if (queue == nullptr) {
        set_state(State::Error, "无法创建 WiFi 任务队列");
        return false;
    }
    queue_ = queue;
    if (xTaskCreate(task_entry, "tab5_wifi", 6144, this, 5, nullptr) != pdPASS) {
        set_state(State::Error, "无法启动 WiFi 后台任务");
        return false;
    }
    ESP_LOGI(TAG, "WiFi service started, enabled=%d configured=%d saved_ssid=%s",
             enabled_, configured_, saved_ssid_.c_str());
    return true;
}

void WifiService::set_state(State state, const std::string &message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    if (!message.empty()) {
        error_ = message;
    } else if (state != State::Error) {
        error_.clear();
    }
}

WifiService::State WifiService::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool WifiService::enabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

bool WifiService::configured() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return configured_;
}

bool WifiService::needs_setup() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_ && !configured_ && !setup_dismissed_;
}

std::string WifiService::saved_ssid() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return saved_ssid_;
}

std::string WifiService::connected_ssid() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ssid_;
}

std::string WifiService::ip_address() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ip_address_;
}

std::vector<WifiService::Network> WifiService::networks() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return networks_;
}

uint32_t WifiService::network_revision() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return network_revision_;
}

std::string WifiService::status_text() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state_) {
    case State::Disabled:
        return "WiFi 已关闭（飞机模式）";
    case State::Starting:
        return "正在启动 WiFi...";
    case State::Scanning:
        return "正在扫描附近 WiFi...";
    case State::Connecting:
        return "正在连接 " + saved_ssid_;
    case State::Connected:
        return "已连接 " + connected_ssid_;
    case State::Error:
        return error_.empty() ? "WiFi 连接失败" : error_;
    case State::Idle:
        return networks_.empty() ? "未发现可用 WiFi" : "请选择 WiFi 网络";
    }
    return "WiFi";
}

void WifiService::mark_setup_dismissed()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        setup_dismissed_ = true;
    }
    nvs_handle_t handle = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "setup_dismissed", 1);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

void WifiService::set_enabled(bool enabled)
{
    std::string ssid;
    std::string password;
    bool configured = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = enabled;
        ssid = saved_ssid_;
        password = saved_password_;
        configured = configured_;
        if (!enabled) {
            state_ = State::Disabled;
            connected_ssid_.clear();
            ip_address_.clear();
        } else if (!configured_) {
            state_ = State::Starting;
        } else {
            state_ = State::Connecting;
        }
    }
    store_preferences(ssid, password, configured, enabled);

    Command command;
    command.type = enabled ? CommandType::Enable : CommandType::Disable;
    QueueHandle_t queue = static_cast<QueueHandle_t>(queue_);
    if (queue != nullptr) {
        xQueueSend(queue, &command, pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "WiFi enabled=%d", enabled);
}

bool WifiService::start_scan()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == State::Scanning) {
            return true;
        }
        // The initial worker scan will start as soon as the radio is ready.
        if (state_ == State::Starting && !configured_) {
            return true;
        }
    }
    Command command;
    command.type = CommandType::Scan;
    QueueHandle_t queue = static_cast<QueueHandle_t>(queue_);
    if (queue == nullptr || !enabled()) {
        return false;
    }
    return xQueueSend(queue, &command, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool WifiService::save_and_connect(const Network &network,
                                   const std::string &password)
{
    if (network.ssid.empty() || network.ssid.size() > 32 || password.size() > 63) {
        set_state(State::Error, "WiFi 名称或密码长度无效");
        return false;
    }
    if (!is_open(network.auth) && password.size() < 8) {
        set_state(State::Error, "加密 WiFi 密码至少需要 8 位");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = true;
        configured_ = true;
        saved_ssid_ = network.ssid;
        saved_password_ = password;
        saved_auth_ = network.auth;
        state_ = State::Connecting;
        error_.clear();
    }
    if (!store_preferences(network.ssid, password, true, true)) {
        set_state(State::Error, "WiFi 信息保存失败");
        return false;
    }

    Command command;
    command.type = CommandType::Connect;
    std::strncpy(command.ssid, network.ssid.c_str(), sizeof(command.ssid) - 1);
    std::strncpy(command.password, password.c_str(), sizeof(command.password) - 1);
    command.auth = static_cast<uint8_t>(network.auth);
    QueueHandle_t queue = static_cast<QueueHandle_t>(queue_);
    if (queue == nullptr ||
        xQueueSend(queue, &command, pdMS_TO_TICKS(100)) != pdTRUE) {
        set_state(State::Error, "WiFi 连接任务忙，请重试");
        return false;
    }
    return true;
}

void WifiService::task_entry(void *arg)
{
    static_cast<WifiService *>(arg)->task_loop();
}

void WifiService::task_loop()
{
    task_ = xTaskGetCurrentTaskHandle();

    if (enabled()) {
        if (!initialize_radio()) {
            return;
        }
        if (configured()) {
            connect_internal(saved_ssid(), saved_password_, saved_auth_);
        } else {
            scan_internal();
        }
    }

    QueueHandle_t queue = static_cast<QueueHandle_t>(queue_);
    for (;;) {
        Command command;
        if (xQueueReceive(queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (command.type) {
        case CommandType::Disable:
            if (radio_started_) {
                esp_wifi_disconnect();
                esp_wifi_stop();
                radio_started_ = false;
            }
            set_state(State::Disabled);
            break;
        case CommandType::Enable:
            if (!radio_initialized_ && !initialize_radio()) {
                break;
            }
            if (!radio_started_ && !start_radio()) {
                break;
            }
            if (configured()) {
                connect_internal(saved_ssid(), saved_password_, saved_auth_);
            } else {
                scan_internal();
            }
            break;
        case CommandType::Scan:
            if (!radio_initialized_ && !initialize_radio()) {
                break;
            }
            if (!radio_started_ && !start_radio()) {
                break;
            }
            scan_internal();
            break;
        case CommandType::Connect:
            if (!radio_initialized_ && !initialize_radio()) {
                break;
            }
            if (!radio_started_ && !start_radio()) {
                break;
            }
            connect_internal(command.ssid, command.password,
                             static_cast<wifi_auth_mode_t>(command.auth));
            break;
        }
    }
}

bool WifiService::initialize_radio()
{
    if (radio_initialized_) {
        return true;
    }
    set_state(State::Starting);
    bsp_set_wifi_power_enable(true);
    vTaskDelay(pdMS_TO_TICKS(150));

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        set_state(State::Error, std::string("网络栈初始化失败: ") + esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        set_state(State::Error, std::string("网络事件循环失败: ") + esp_err_to_name(err));
        return false;
    }

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (netif == nullptr) {
        set_state(State::Error, "无法创建 WiFi 网络接口");
        return false;
    }
    netif_ = netif;

    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiService::wifi_event_handler, this, nullptr);
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiService::ip_event_handler, this, nullptr);
    }
    if (err != ESP_OK) {
        set_state(State::Error, std::string("WiFi 事件注册失败: ") + esp_err_to_name(err));
        return false;
    }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) {
        set_state(State::Error, std::string("WiFi 初始化失败: ") + esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        set_state(State::Error, std::string("WiFi STA 模式失败: ") + esp_err_to_name(err));
        return false;
    }
    radio_initialized_ = true;
    return start_radio();
}

bool WifiService::start_radio()
{
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        set_state(State::Error, std::string("WiFi 启动失败: ") + esp_err_to_name(err));
        return false;
    }
    radio_started_ = true;
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    return true;
}

bool WifiService::scan_internal()
{
    set_state(State::Scanning);
    wifi_scan_config_t config{};
    config.show_hidden = true;
    const esp_err_t err = esp_wifi_scan_start(&config, false);
    if (err != ESP_OK) {
        set_state(State::Error, std::string("WiFi 扫描失败: ") + esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "WiFi scan started");
    return true;
}

bool WifiService::connect_internal(const std::string &ssid,
                                   const std::string &password,
                                   wifi_auth_mode_t auth)
{
    wifi_config_t config{};
    std::strncpy(reinterpret_cast<char *>(config.sta.ssid), ssid.c_str(),
                 sizeof(config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char *>(config.sta.password), password.c_str(),
                 sizeof(config.sta.password) - 1);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode =
        is_open(auth) ? WIFI_AUTH_OPEN : auth;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) {
        set_state(State::Error, std::string("WiFi 配置失败: ") + esp_err_to_name(err));
        return false;
    }
    set_state(State::Connecting);
    esp_wifi_disconnect();
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        set_state(State::Error, std::string("WiFi 连接失败: ") + esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "connecting to SSID '%s'", ssid.c_str());
    return true;
}

void WifiService::collect_scan_results()
{
    uint16_t count = 0;
    if (esp_wifi_scan_get_ap_num(&count) != ESP_OK || count == 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        networks_.clear();
        ++network_revision_;
        state_ = State::Idle;
        return;
    }

    std::vector<wifi_ap_record_t> records(count);
    uint16_t actual = count;
    if (esp_wifi_scan_get_ap_records(&actual, records.data()) != ESP_OK) {
        set_state(State::Error, "读取 WiFi 扫描结果失败");
        return;
    }

    std::map<std::string, Network> by_ssid;
    for (uint16_t i = 0; i < actual; ++i) {
        const char *ssid = reinterpret_cast<const char *>(records[i].ssid);
        if (ssid[0] == '\0') {
            continue;
        }
        Network network;
        network.ssid = ssid;
        network.rssi = records[i].rssi;
        network.auth = records[i].authmode;
        auto found = by_ssid.find(network.ssid);
        if (found == by_ssid.end() || network.rssi > found->second.rssi) {
            by_ssid[network.ssid] = network;
        }
    }

    std::vector<Network> result;
    result.reserve(by_ssid.size());
    for (auto &entry : by_ssid) {
        result.push_back(entry.second);
    }
    std::sort(result.begin(), result.end(), [](const Network &a, const Network &b) {
        return a.rssi > b.rssi;
    });

    {
        std::lock_guard<std::mutex> lock(mutex_);
        networks_ = std::move(result);
        ++network_revision_;
        state_ = State::Idle;
        error_.clear();
    }
    ESP_LOGI(TAG, "WiFi scan found %u unique networks", static_cast<unsigned>(networks().size()));
}

void WifiService::wifi_event_handler(void *arg, const char *,
                                     int32_t event_id, void *data)
{
    auto self = static_cast<WifiService *>(arg);
    if (self == nullptr) {
        return;
    }

    if (event_id == WIFI_EVENT_SCAN_DONE) {
        auto *event = static_cast<wifi_event_sta_scan_done_t *>(data);
        if (event != nullptr && event->status == 0) {
            self->collect_scan_results();
        } else {
            self->set_state(State::Error, "WiFi 扫描未完成");
        }
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->state_ = State::Connecting;
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        {
            std::lock_guard<std::mutex> lock(self->mutex_);
            self->connected_ssid_.clear();
            self->ip_address_.clear();
            if (!self->enabled_) {
                self->state_ = State::Disabled;
                return;
            }
            auto *event = static_cast<wifi_event_sta_disconnected_t *>(data);
            const int reason = event != nullptr ? event->reason : 0;
            self->state_ = State::Error;
            if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                reason == WIFI_REASON_HANDSHAKE_TIMEOUT) {
                self->error_ = "WiFi 密码错误或认证失败";
            } else if (reason == WIFI_REASON_NO_AP_FOUND) {
                self->error_ = "未找到 WiFi 网络";
            } else {
                self->error_ = "WiFi 已断开";
            }
        }
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d",
                 data != nullptr ? static_cast<wifi_event_sta_disconnected_t *>(data)->reason : -1);
    }
}

void WifiService::ip_event_handler(void *arg, const char *,
                                   int32_t event_id, void *data)
{
    auto self = static_cast<WifiService *>(arg);
    if (self == nullptr || event_id != IP_EVENT_STA_GOT_IP || data == nullptr) {
        return;
    }
    auto *event = static_cast<ip_event_got_ip_t *>(data);
    char address[16] = {};
    esp_ip4addr_ntoa(&event->ip_info.ip, address, sizeof(address));
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->state_ = State::Connected;
        self->connected_ssid_ = self->saved_ssid_;
        self->ip_address_ = address;
        self->error_.clear();
    }
    ESP_LOGI(TAG, "WiFi connected, SSID=%s IP=%s", self->saved_ssid().c_str(),
             self->ip_address().c_str());
}
