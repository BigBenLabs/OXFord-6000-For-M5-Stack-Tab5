#pragma once

#include "app_types.h"

class AudioService {
public:
    bool init(int volume);
    bool play(const Word &word, bool british);
    void stop();
    void set_volume(int volume);
    bool busy() const
    {
        return playing_;
    }
    void mark_stopped()
    {
        playing_ = false;
    }

private:
    bool created_ = false;
    volatile bool playing_ = false;
    int volume_ = 70;
};

