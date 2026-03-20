#include "config.h"
#include "mbedtls/sha256.h"

// ─── Singleton instance ───────────────────────────────────────────────────────
ConfigManager Config;

// ─── Constructor ─────────────────────────────────────────────────────────────
ConfigManager::ConfigManager()
    : urlParam(DEFAULT_URL_PARAM), scanTerminator("crlf"), kbLayout("de"), setupDone(false),
      dhcpEnabled(true), staticSubnet("255.255.255.0"), staticDNS("8.8.8.8") {}

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
    scanTerminator= prefs.getString(KEY_SCAN_TERM,  "crlf");
    kbLayout      = prefs.getString(KEY_KB_LAYOUT,  "de");
    setupDone     = prefs.getBool  (KEY_SETUP_DONE, false);
    dhcpEnabled   = prefs.getBool  (KEY_DHCP_ENABLED, true);
    staticIP      = prefs.getString(KEY_STATIC_IP,  "");
    staticSubnet  = prefs.getString(KEY_STATIC_SN,  "255.255.255.0");
    staticGateway = prefs.getString(KEY_STATIC_GW,  "");
    staticDNS     = prefs.getString(KEY_STATIC_DNS, "8.8.8.8");

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
    prefs.putString(KEY_SCAN_TERM,  scanTerminator);
    prefs.putString(KEY_KB_LAYOUT,  kbLayout);
    prefs.putBool  (KEY_SETUP_DONE, setupDone);
    prefs.putBool  (KEY_DHCP_ENABLED, dhcpEnabled);
    prefs.putString(KEY_STATIC_IP,  staticIP);
    prefs.putString(KEY_STATIC_SN,  staticSubnet);
    prefs.putString(KEY_STATIC_GW,  staticGateway);
    prefs.putString(KEY_STATIC_DNS, staticDNS);

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
    scanTerminator= "crlf";
    kbLayout      = "de";
    setupDone     = false;
    dhcpEnabled   = true;
    staticIP      = "";
    staticSubnet  = "255.255.255.0";
    staticGateway = "";
    staticDNS     = "8.8.8.8";

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
