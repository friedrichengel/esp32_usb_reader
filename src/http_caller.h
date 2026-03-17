#pragma once

#include <Arduino.h>

// ─── HTTPCaller ───────────────────────────────────────────────────────────────
// Builds a URL from the configured base URL + URL-encoded QR data,
// performs an HTTP GET, and logs the response.
class HTTPCaller {
public:
    /**
     * Call baseURL?urlParam=data  (e.g. https://example.com/api?code=ABC123).
     * @param baseURL   Full base URL including scheme
     * @param paramName Query-parameter name (e.g. "code")
     * @param data      Raw QR code payload – will be URL-encoded
     * @return HTTP status code, or -1 on connection error
     */
    static int call(const String& baseURL,
                    const String& paramName,
                    const String& data);

    // URL encoding is provided by plageoj/UrlEncode (urlEncode / urlDecode)
};
