#include "logger.h"

#include <stdarg.h>
#include <cstring>

Logger AppLogger;

// ─── add() ────────────────────────────────────────────────────────────────────
void Logger::add(const char* cat, const char* fmt, ...) {
    // Format message outside critical section to keep lock as short as possible
    char tmp[LOG_MAX_MSG];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    portENTER_CRITICAL(&_mux);
    uint32_t idx  = _count++;
    uint32_t slot = idx % LOG_RING_SZ;

    _buf[slot].idx = idx;
    _buf[slot].ts  = (uint32_t)millis();
    strncpy(_buf[slot].cat, cat, LOG_MAX_CAT - 1);
    _buf[slot].cat[LOG_MAX_CAT - 1] = '\0';
    strncpy(_buf[slot].msg, tmp, LOG_MAX_MSG - 1);
    _buf[slot].msg[LOG_MAX_MSG - 1] = '\0';
    portEXIT_CRITICAL(&_mux);
}

// ─── getJson() ────────────────────────────────────────────────────────────────
String Logger::getJson(uint32_t sinceIdx) const {
    uint32_t total = _count;
    // Oldest available entry in the ring buffer
    uint32_t oldest = (total > (uint32_t)LOG_RING_SZ) ? total - LOG_RING_SZ : 0;
    uint32_t start  = (sinceIdx > oldest) ? sinceIdx : oldest;

    String out;
    out.reserve(1024);
    out = '[';
    bool first = true;

    for (uint32_t i = start; i < total; i++) {
        uint32_t slot = i % LOG_RING_SZ;
        if (_buf[slot].idx != i) continue; // slot not yet written (rare race)

        if (!first) out += ',';
        first = false;

        // Minimal JSON escaping: backslash and double-quote
        String m(_buf[slot].msg);
        m.replace("\\", "\\\\");
        m.replace("\"", "\\\"");

        char hdr[80];
        snprintf(hdr, sizeof(hdr),
                 "{\"i\":%lu,\"t\":%lu,\"c\":\"%s\",\"m\":\"",
                 (unsigned long)_buf[slot].idx,
                 (unsigned long)_buf[slot].ts,
                 _buf[slot].cat);
        out += hdr;
        out += m;
        out += "\"}";
    }

    out += ']';
    return out;
}
