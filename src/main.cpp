#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "config.h"
#include "usb_hid_host.h"
#include "http_caller.h"
#include "web_server.h"
#include "logger.h"

// ─── WiFi connection ──────────────────────────────────────────────────────────
static void connectWiFi() {
    if (Config.wifiSSID.isEmpty()) {
        Serial.println("[WiFi] No SSID configured – starting AP mode");
        WiFi.mode(WIFI_AP);
        String apName = "QRReader-" + WiFi.macAddress().substring(9);
        apName.replace(":", "");
        WiFi.softAP(apName.c_str());
        Serial.printf("[WiFi] AP started: %s  IP: %s\n",
                      apName.c_str(),
                      WiFi.softAPIP().toString().c_str());
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(Config.wifiSSID.c_str(), Config.wifiPass.c_str());
    Serial.printf("[WiFi] Connecting to %s", Config.wifiSSID.c_str());

    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] Connected. IP: %s\n",
                      WiFi.localIP().toString().c_str());
        // mDNS: reachable as https://qrreader.local
        if (MDNS.begin("qrreader")) {
            MDNS.addService("https", "tcp", 443);
            MDNS.addService("http",  "tcp", 80);
            Serial.println("[mDNS] qrreader.local registered");
        }
    } else {
        Serial.println("[WiFi] Connection failed – starting AP as fallback");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("QRReader-Fallback");
        Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    }
}

// ─── QR scan callback ─────────────────────────────────────────────────────────
static void onQRScanned(const String& data) {
    Serial.printf("[Scan] %s\n", data.c_str());
    AppLogger.add("Scan", "%s", data.c_str());
    HTTPCaller::call(Config.baseURL, Config.urlParam, data);
}

// ─── setup() ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ESP32-S3 QR USB Reader ===");

    // 1. Load configuration from NVS
    Config.load();

    // 2. Connect to WiFi (or start AP)
    connectWiFi();

    // 3. Start web server
    WebSrv.begin();

    // 4. Start USB HID host (QR scanner)
    //    The USB OTG peripheral must be connected to a scanner via USB OTG
    //    adapter on the USB OTG port of the ESP32-S3 DevKitC-1.

    // VBUS aktivieren für USB-Host
    pinMode(GPIO_NUM_21, OUTPUT); // GPIO21 = VBUS enable auf DevKitC-1
    digitalWrite(GPIO_NUM_21, HIGH);
    delay(100);

    UsbHid.begin(onQRScanned);

    Serial.println("[Main] Setup complete.");
    if (!Config.setupDone) {
        Serial.printf("[Main] *** First boot: open https://%s/setup ***\n",
                      WiFi.localIP().toString().c_str());
    }
}

// ─── loop() ──────────────────────────────────────────────────────────────────
void loop() {
    // Drive the HTTPS/HTTP server event loop
    WebSrv.loop();

    // Fire scan callback when no terminator is configured (timeout mode)
    UsbHid.checkTimeout();

    // Periodically check WiFi and reconnect if dropped
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        lastCheck = millis();
        if (!Config.wifiSSID.isEmpty() && WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Lost connection – reconnecting...");
            connectWiFi();
        }
    }
}
