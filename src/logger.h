#pragma once

#include <Arduino.h>

#define LOG_MAX_CAT  10
#define LOG_MAX_MSG  160
#define LOG_RING_SZ  100

struct LogEntry {
    uint32_t idx;
    uint32_t ts;              // millis() at time of log
    char     cat[LOG_MAX_CAT];
    char     msg[LOG_MAX_MSG];
};

// ─── Logger ───────────────────────────────────────────────────────────────────
// Thread-safe ring buffer. add() is callable from any FreeRTOS task.
// getJson(sinceIdx) returns a JSON array of entries with idx >= sinceIdx,
// formatted as: [{"i":N,"t":ms,"c":"CAT","m":"message"}, ...]
class Logger {
public:
    // Add entry. Uses printf-style formatting.
    void add(const char* cat, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    // Returns JSON array of entries with idx >= sinceIdx.
    String getJson(uint32_t sinceIdx) const;

private:
    LogEntry          _buf[LOG_RING_SZ] = {};
    volatile uint32_t _count            = 0;
    portMUX_TYPE      _mux              = portMUX_INITIALIZER_UNLOCKED;
};

extern Logger AppLogger;
