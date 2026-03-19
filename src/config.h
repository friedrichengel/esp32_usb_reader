#pragma once

#include <Arduino.h>
#include <Preferences.h>

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

// ─── Defaults ────────────────────────────────────────────────────────────────
#define DEFAULT_URL_PARAM  "data"

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
    bool   setupDone;

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
