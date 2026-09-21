#pragma once
#include <cstdint>
extern int64_t test_time_us;
inline int64_t esp_timer_get_time() { return test_time_us; }
