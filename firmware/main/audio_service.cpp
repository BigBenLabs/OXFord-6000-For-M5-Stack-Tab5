#include "audio_service.h"

#include "audio_player.h"
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"

#include <cstdio>

namespace {

constexpr const char *TAG = "audio";
bsp_codec_config_t *codec = nullptr;

esp_err_t mute_callback(AUDIO_PLAYER_MUTE_SETTING setting)
{
    return codec != nullptr ? codec->set_mute(setting == AUDIO_PLAYER_MUTE) : ESP_ERR_INVALID_STATE;
}

void audio_event_callback(audio_player_cb_ctx_t *event)
{
    if (event == nullptr) {
        return;
    }
    auto self = static_cast<AudioService *>(event->user_ctx);
    if (self == nullptr) {
        return;
    }
    if (event->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_IDLE ||
        event->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_PAUSE ||
        event->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN ||
        event->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_UNKNOWN_FILE_TYPE) {
        self->mark_stopped();
    }
}

} // namespace

bool AudioService::init(int volume)
{
    codec = bsp_get_codec_handle();
    if (codec == nullptr) {
        ESP_LOGE(TAG, "codec handle unavailable");
        return false;
    }
    volume_ = volume;
    codec->set_volume(volume_);
    codec->i2s_reconfig_clk_fn(48000, 16, I2S_SLOT_MODE_STEREO);

    audio_player_config_t config{};
    config.mute_fn = mute_callback;
    config.clk_set_fn = codec->i2s_reconfig_clk_fn;
    config.write_fn = codec->i2s_write;
    config.priority = 5;
    config.coreID = 0;
    if (audio_player_new(config) != ESP_OK) {
        ESP_LOGE(TAG, "audio player init failed");
        return false;
    }
    audio_player_callback_register(audio_event_callback, this);
    created_ = true;
    return true;
}

bool AudioService::play(const Word &word, bool british)
{
    if (!created_) {
        return false;
    }
    const char *file = british ? word.uk_audio.c_str() : word.us_audio.c_str();
    if (file == nullptr || file[0] == '\0') {
        return false;
    }

    char path[256];
    std::snprintf(path, sizeof(path), "/sdcard/audio/%s_audio_split_24m/%s",
                  british ? "uk" : "us", file);
    FILE *fp = std::fopen(path, "rb");
    if (fp == nullptr) {
        std::snprintf(path, sizeof(path), "/sdcard/audio/%s", file);
        fp = std::fopen(path, "rb");
    }
    if (fp == nullptr) {
        ESP_LOGW(TAG, "audio missing: %s", path);
        return false;
    }

    if (audio_player_play(fp) != ESP_OK) {
        std::fclose(fp);
        return false;
    }
    playing_ = true;
    return true;
}

void AudioService::stop()
{
    if (created_) {
        audio_player_stop();
    }
    playing_ = false;
}

void AudioService::set_volume(int volume)
{
    volume_ = volume;
    if (codec != nullptr) {
        codec->set_volume(volume_);
    }
}
