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
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char suffix[7];
        snprintf(suffix, sizeof(suffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
        String apName = "QRReader-" + String(suffix);
        WiFi.softAP(apName.c_str());
        Serial.printf("[WiFi] AP started: %s  IP: %s\n",
                      apName.c_str(),
                      WiFi.softAPIP().toString().c_str());
        return;
    }

    WiFi.mode(WIFI_STA);
    if (!Config.dhcpEnabled && !Config.staticIP.isEmpty()) {
        IPAddress ip, sn, gw, dns;
        ip.fromString(Config.staticIP);
        sn.fromString(Config.staticSubnet);
        gw.fromString(Config.staticGateway);
        dns.fromString(Config.staticDNS);
        WiFi.config(ip, gw, sn, dns);
        Serial.printf("[WiFi] Static IP: %s / %s GW: %s DNS: %s\n",
                      Config.staticIP.c_str(), Config.staticSubnet.c_str(),
                      Config.staticGateway.c_str(), Config.staticDNS.c_str());
    }
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

// Flag: loop() führt den Neustart sauber aus statt aus Task-Kontext heraus
static volatile bool g_restartPending = false;

// ─── WiFi-QR-Code Parser ──────────────────────────────────────────────────────
// Format: WIFI:T:WPA;S:SSID;P:Passwort;H:false;;
// Felder können in beliebiger Reihenfolge auftreten.
// Rückgabe: true wenn ein SSID gefunden wurde
static bool parseWifiQR(const String& data, String& ssid, String& pass) {
    if (!data.startsWith("WIFI:") && !data.startsWith("wifi:")) return false;
    ssid = "";
    pass = "";

    String s = data.substring(5); // "WIFI:" überspringen
    int i = 0;
    while (i < (int)s.length()) {
        int colon = s.indexOf(':', i);
        if (colon < 0) break;
        String key = s.substring(i, colon);

        // Wert bis zum nächsten nicht-escapeten Semikolon lesen
        String val = "";
        int j = colon + 1;
        while (j < (int)s.length()) {
            char c = s[j];
            if (c == '\\' && j + 1 < (int)s.length()) {
                val += s[j + 1];
                j += 2;
            } else if (c == ';') {
                j++;
                break;
            } else {
                val += c;
                j++;
            }
        }
        i = j;

        if      (key == "S" || key == "s") ssid = val;
        else if (key == "P" || key == "p") pass = val;
    }
    return !ssid.isEmpty();
}

// ─── QR scan callback ─────────────────────────────────────────────────────────
static void onQRScanned(const String& data) {
    Serial.printf("[Scan] Callback: '%s'  WiFiMode=%d\n", data.c_str(), (int)WiFi.getMode());
    // WiFi-QR-Code im AP-Modus → Zugangsdaten übernehmen und neu verbinden
    if (WiFi.getMode() == WIFI_MODE_AP || WiFi.getMode() == WIFI_MODE_APSTA) {
        String ssid, pass;
        if (parseWifiQR(data, ssid, pass)) {
            Serial.println("[Scan] ══════════════════════════════════");
            Serial.printf ("[Scan] WiFi-QR erkannt!\n");
            Serial.printf ("[Scan] SSID: %s\n", ssid.c_str());
            Serial.printf ("[Scan] Zugangsdaten gespeichert – Neustart in 2 Sekunden...\n");
            Serial.println("[Scan] ══════════════════════════════════");
            AppLogger.add("Scan", "WiFi-QR: SSID='%s' gespeichert, Neustart folgt", ssid.c_str());
            Config.wifiSSID = ssid;
            Config.wifiPass = pass;
            Config.save();
            g_restartPending = true; // loop() führt den Neustart aus
            return;
        }
    }

    // Geheimer Reset-Code: zweimal hintereinander → Werksreset
    // Prüfung VOR dem Logging damit der Code nie im Klartext erscheint
    static int resetCount = 0;
    if (data == RESET_QR_CODE) {
        resetCount++;
        Serial.printf("[Scan] Reset-Code erkannt (%d/2)\n", resetCount);
        AppLogger.add("Scan", "Reset-Code erkannt (%d/2)", resetCount);
        if (resetCount >= 2) {
            Serial.println("[Scan] ══════════════════════════════════");
            Serial.println("[Scan] Werksreset via QR-Code!");
            Serial.println("[Scan] Neustart in 2 Sekunden...");
            Serial.println("[Scan] ══════════════════════════════════");
            AppLogger.add("Scan", "Werksreset wird ausgefuehrt...");
            Config.factoryReset();
            g_restartPending = true;
        }
        return;
    }

    // Anderer Code → Zähler zurücksetzen + normal verarbeiten
    resetCount = 0;
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
    // Neustart-Flag (gesetzt aus USB-Task-Kontext, hier sauber ausgeführt)
    if (g_restartPending) {
        delay(2000); // Serielle Ausgabe noch raussenden, dann neu starten
        ESP.restart();
    }

    // Automatischer Neustart nach konfiguriertem Intervall
    if (Config.autoRestartMin > 0) {
        static uint32_t autoRestartStart = millis();
        uint32_t interval = (uint32_t)Config.autoRestartMin * 60UL * 1000UL;
        if (millis() - autoRestartStart >= interval) {
            Serial.printf("[Main] Automatischer Neustart nach %d Minuten\n", Config.autoRestartMin);
            AppLogger.add("SYS", "Automatischer Neustart nach %d min", Config.autoRestartMin);
            delay(500);
            ESP.restart();
        }
    }

    // Drive the HTTPS/HTTP server event loop
    WebSrv.loop();

    // Fire scan callback when no terminator is configured (timeout mode)
    UsbHid.checkTimeout();

    // Periodically check WiFi and reconnect if dropped
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        lastCheck = millis();
        // Nur reconnecten wenn wir im STA-Modus sind und die Verbindung verloren haben.
        // Im AP-Modus (z.B. Fallback wegen falscher Credentials) nicht stören.
        if (!Config.wifiSSID.isEmpty() &&
            WiFi.getMode() == WIFI_MODE_STA &&
            WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Lost connection – reconnecting...");
            connectWiFi();
        }
    }
}
