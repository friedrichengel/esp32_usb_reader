#include "http_caller.h"
#include "logger.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <UrlEncode.h>  // urlEncode() from plageoj/UrlEncode

// ─── call() ──────────────────────────────────────────────────────────────────
int HTTPCaller::call(const String& baseURL,
                     const String& paramName,
                     const String& data) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] Not connected to WiFi – skipping request");
        AppLogger.add("HTTP", "Kein WiFi – Anfrage übersprungen");
        return -1;
    }
    if (baseURL.isEmpty()) {
        Serial.println("[HTTP] Base URL is empty – skipping request");
        AppLogger.add("HTTP", "Keine Basis-URL konfiguriert");
        return -1;
    }

    // Build URL:  baseURL + "?" + paramName + "=" + urlEncoded(data)
    String url = baseURL;
    url += (url.indexOf('?') < 0) ? "?" : "&";
    url += paramName;
    url += "=";
    url += urlEncode(data);

    Serial.printf("[HTTP] GET %s\n", url.c_str());

    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);

    int code = http.GET();

    if (code > 0) {
        Serial.printf("[HTTP] Response: %d  body: %s\n",
                      code, http.getString().c_str());
        AppLogger.add("HTTP", "GET %s -> %d", url.c_str(), code);
    } else {
        Serial.printf("[HTTP] Error: %s\n", http.errorToString(code).c_str());
        AppLogger.add("HTTP", "Fehler %s: %s",
                      url.c_str(), http.errorToString(code).c_str());
    }

    http.end();
    return code;
}
