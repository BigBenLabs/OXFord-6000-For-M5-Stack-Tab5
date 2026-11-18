#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace clock_service {

bool init();
int64_t now();
int64_t start_of_day(int64_t timestamp);
std::string format_time(int64_t timestamp, const char *format = "%m-%d %H:%M");

} // namespace clock_service

