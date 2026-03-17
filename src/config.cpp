#include "config.h"
#include "mbedtls/sha256.h"

// ─── Singleton instance ───────────────────────────────────────────────────────
ConfigManager Config;

// ─── Constructor ─────────────────────────────────────────────────────────────
ConfigManager::ConfigManager()
    : urlParam(DEFAULT_URL_PARAM), setupDone(false) {}

// ─── load() ──────────────────────────────────────────────────────────────────
void ConfigManager::load() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);

    wifiSSID      = prefs.getString(KEY_WIFI_SSID,  "");
    wifiPass      = prefs.getString(KEY_WIFI_PASS,  "");
    baseURL       = prefs.getString(KEY_BASE_URL,   "");
    urlParam      = prefs.getString(KEY_URL_PARAM,  DEFAULT_URL_PARAM);
    adminUser     = prefs.getString(KEY_ADMIN_USER, "");
    adminPassHash = prefs.getString(KEY_ADMIN_HASH, "");
    setupDone     = prefs.getBool  (KEY_SETUP_DONE, false);

    prefs.end();

    Serial.printf("[Config] Loaded – SSID: %s, BaseURL: %s, SetupDone: %d\n",
                  wifiSSID.c_str(), baseURL.c_str(), setupDone ? 1 : 0);
}

// ─── save() ──────────────────────────────────────────────────────────────────
void ConfigManager::save() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);

    prefs.putString(KEY_WIFI_SSID,  wifiSSID);
    prefs.putString(KEY_WIFI_PASS,  wifiPass);
    prefs.putString(KEY_BASE_URL,   baseURL);
    prefs.putString(KEY_URL_PARAM,  urlParam);
    prefs.putString(KEY_ADMIN_USER, adminUser);
    prefs.putString(KEY_ADMIN_HASH, adminPassHash);
    prefs.putBool  (KEY_SETUP_DONE, setupDone);

    prefs.end();
    Serial.println("[Config] Saved.");
}

// ─── factoryReset() ──────────────────────────────────────────────────────────
void ConfigManager::factoryReset() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
    prefs.clear();
    prefs.end();

    // Reset in-memory values
    wifiSSID      = "";
    wifiPass      = "";
    baseURL       = "";
    urlParam      = DEFAULT_URL_PARAM;
    adminUser     = "";
    adminPassHash = "";
    setupDone     = false;

    Serial.println("[Config] Factory reset done.");
}

// ─── sha256() ────────────────────────────────────────────────────────────────
String ConfigManager::sha256(const String& input) {
    unsigned char hash[32];

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, /*is224=*/0);
    mbedtls_sha256_update(&ctx,
                          reinterpret_cast<const unsigned char*>(input.c_str()),
                          input.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    char hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(hex + i * 2, "%02x", hash[i]);
    }
    hex[64] = '\0';
    return String(hex);
}
