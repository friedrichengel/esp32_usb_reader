#pragma once

#include <Arduino.h>
#include <Preferences.h>

// ─── Debug-Flags ─────────────────────────────────────────────────────────────
// Auskommentieren um die jeweiligen Debug-Ausgaben zu deaktivieren
//#define USB_DEBUG   // USB HID: Transfer-Berichte, Keycodes, Scan-Puffer

// ─── Configuration keys (NVS namespace "qrreader") ──────────────────────────
#define NVS_NAMESPACE    "qrreader"
#define KEY_WIFI_SSID    "wifi_ssid"
#define KEY_WIFI_PASS    "wifi_pass"
#define KEY_BASE_URL     "base_url"
#define KEY_URL_PARAM    "url_param"
#define KEY_ADMIN_USER   "admin_user"
#define KEY_ADMIN_HASH   "admin_hash"
#define KEY_SETUP_DONE   "setup_done"
#define KEY_SCAN_TERM    "scan_term"
#define KEY_KB_LAYOUT    "kb_layout"
#define KEY_DHCP_ENABLED "dhcp_en"
#define KEY_STATIC_IP    "static_ip"
#define KEY_STATIC_SN    "static_sn"
#define KEY_STATIC_GW    "static_gw"
#define KEY_STATIC_DNS   "static_dns"

// ─── Defaults ────────────────────────────────────────────────────────────────
#define DEFAULT_URL_PARAM  "data"

// ─── Geheimer Reset-QR-Code ───────────────────────────────────────────────────
// Zweimal hintereinander scannen (ohne anderen Code dazwischen) → Werksreset.
#define RESET_QR_CODE "QRR-7X9K2M5P8Q3N4W6Y1Z0A8B3C7D2E5F4G6H9J0K8L3M7N2P5Q4R1S6T9U0V8W3X7Y4Z2"

// ─── ConfigManager ───────────────────────────────────────────────────────────
class ConfigManager {
public:
    String wifiSSID;
    String wifiPass;
    String baseURL;
    String urlParam;
    String adminUser;
    String adminPassHash; // SHA-256 hex string
    // Scan terminator: "crlf" = CR+LF (Enter×2), "cr" = Enter, "tab" = Tab, "none" = 300ms timeout
    String scanTerminator;
    // Keyboard layout: "de" = QWERTZ (default), "us" = QWERTY
    String kbLayout;
    bool   setupDone;

    // Network / IP
    bool   dhcpEnabled;   // true = DHCP (default), false = static IP
    String staticIP;
    String staticSubnet;
    String staticGateway;
    String staticDNS;

    ConfigManager();

    /** Load all settings from NVS. */
    void load();

    /** Persist all settings to NVS. */
    void save();

    /** Clear NVS and reset to factory defaults (in-memory + NVS). */
    void factoryReset();

    /** Returns SHA-256 hex digest of input. */
    static String sha256(const String& input);
};

// Singleton
extern ConfigManager Config;
