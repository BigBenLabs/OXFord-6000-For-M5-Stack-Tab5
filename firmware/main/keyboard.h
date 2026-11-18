#pragma once

#include <cstddef>
#include <cstdint>

struct KeyEvent {
    enum class Type : int {
        None = 0,
        Text,
        Backspace,
        Enter,
        Tab,
        Escape,
        Screenshot,
    };
    Type type = Type::None;
    char text[16] = {};
};

class KeyboardService {
public:
    bool init();
    bool connected() const
    {
        return connected_;
    }
    bool poll(KeyEvent &event, unsigned timeout_ms = 0);
    void set_enabled(bool enabled);

private:
    static void interrupt_handler(void *arg);
    static void task_entry(void *arg);
    void task_loop();
    bool read_and_parse_events();
    bool read_register(uint8_t reg, uint8_t *data, size_t len);
    bool write_register(uint8_t reg, const uint8_t *data, size_t len);

    void *bus_ = nullptr;
    void *device_ = nullptr;
    void *queue_ = nullptr;
    void *task_ = nullptr;
    bool connected_ = false;
    volatile bool enabled_ = false;
};

