#include "web_server.h"
#include "config.h"
#include "version.h"
#include "logger.h"

#include <PsychicHttp.h>
#include <PsychicHttpsServer.h>
#include <esp_ota_ops.h>
#include <Update.h>
#include <WiFi.h>
#include <Preferences.h>
#include <map>

// mbedTLS 3.x – certificate generation
#include <new>
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

// ─── Singleton ────────────────────────────────────────────────────────────────
WebServerManager WebSrv;

static PsychicHttpsServer* httpsServer = nullptr;
static PsychicHttpServer*  httpServer  = nullptr;

// Global strings keep the PEM pointers valid for the server's lifetime
static String g_certPEM;
static String g_keyPEM;

// ─── Session store  (token → expiry millis) ──────────────────────────────────
static std::map<String, uint32_t> sessions;
static const uint32_t SESSION_TTL_MS = 30UL * 60UL * 1000UL;

// ─── OTA state (shared between upload + request callbacks) ───────────────────
static bool g_otaAuthFailed = false;
static bool g_otaError      = false;

// ═══════════════════════════════════════════════════════════════════════════════
//  Generic helpers
// ═══════════════════════════════════════════════════════════════════════════════

static String getMac() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static String getMyIP() {
    wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
        return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
}

static String generateToken() {
    char buf[33];
    for (int i = 0; i < 32; i++)
        buf[i] = "0123456789abcdef"[random(16)];
    buf[32] = '\0';
    return String(buf);
}

// ─── Session helpers ──────────────────────────────────────────────────────────
static bool isAuthenticated(PsychicRequest* req) {
    String token = req->getCookie("session");
    if (token.isEmpty()) return false;
    auto it = sessions.find(token);
    if (it == sessions.end()) return false;
    if (millis() > it->second) { sessions.erase(it); return false; }
    it->second = millis() + SESSION_TTL_MS; // sliding window
    return true;
}

static esp_err_t loginAndRedirect(PsychicResponse* res, const char* location) {
    String token = generateToken();
    sessions[token] = millis() + SESSION_TTL_MS;

    res->setCode(302);
    res->addHeader("Location", location);
    res->addHeader("Set-Cookie",
        ("session=" + token + "; Path=/; HttpOnly; SameSite=Strict").c_str());
    return res->send();
}

// ─── HTML page template ───────────────────────────────────────────────────────
static String page(const String& title, const String& body) {
    return String(R"(<!DOCTYPE html><html lang="de">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>)") + title + R"(</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:sans-serif;background:#f0f2f5;display:flex;justify-content:center;padding:40px 16px}
  .card{background:#fff;border-radius:10px;box-shadow:0 2px 10px #0002;padding:32px;width:100%;max-width:500px}
  h1{font-size:1.4rem;margin-bottom:24px;color:#333}
  h2{font-size:1.05rem;margin:20px 0 10px;color:#444;border-bottom:1px solid #eee;padding-bottom:4px}
  label{display:block;font-size:.85rem;color:#555;margin-bottom:4px}
  input[type=text],input[type=password],input[type=url],input[type=file]{
    width:100%;padding:10px;border:1px solid #ccc;border-radius:6px;margin-bottom:14px;font-size:.95rem}
  .btn{display:block;width:100%;padding:11px;background:#2563eb;color:#fff;border:none;
    border-radius:6px;cursor:pointer;font-size:.95rem;margin-top:4px;text-align:center;
    text-decoration:none}
  .btn:hover{background:#1d4ed8}
  .btn-danger{background:#dc2626}.btn-danger:hover{background:#b91c1c}
  .btn-secondary{background:#6b7280}.btn-secondary:hover{background:#4b5563}
  .msg{padding:10px 14px;border-radius:6px;margin-bottom:16px;font-size:.88rem}
  .msg.ok{background:#dcfce7;color:#166534}.msg.err{background:#fee2e2;color:#991b1b}
  .msg.warn{background:#fef9c3;color:#854d0e}
  nav{margin-bottom:20px;font-size:.85rem}
  nav a{margin-right:12px;color:#2563eb;text-decoration:none}
  nav a:hover{text-decoration:underline}
  .info{font-size:.82rem;color:#777;margin:-10px 0 14px}
  .mac{font-family:monospace;font-size:1rem;background:#f3f4f6;padding:4px 10px;
    border-radius:4px;letter-spacing:.05em}
  #progress{margin-top:12px}
</style></head><body><div class="card">)" + body + R"(</div></body></html>)";
}

static String navBar() {
    return R"(<nav>
      <a href="/settings">Einstellungen</a>
      <a href="/update">Firmware-Update</a>
      <a href="/monitor">Monitor</a>
      <a href="/logout">Abmelden</a>
    </nav>)";
}

// Shorthand: get a param value or empty string
static String param(PsychicRequest* req, const char* name) {
    return req->hasParam(name) ? req->getParam(name)->value() : String("");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Route handlers
// ═══════════════════════════════════════════════════════════════════════════════

// ─── GET / ────────────────────────────────────────────────────────────────────
static esp_err_t handleRoot(PsychicRequest* req, PsychicResponse* res) {
    return res->redirect(Config.setupDone ? "/settings" : "/setup");
}

// ─── GET /setup ───────────────────────────────────────────────────────────────
static esp_err_t handleSetupGet(PsychicRequest* req, PsychicResponse* res) {
    if (Config.setupDone) return res->redirect("/login");

    String mac  = getMac();
    String body = R"(<h1>Ersteinrichtung</h1>
<div class="msg warn">
  <strong>Wichtig:</strong> Notieren Sie sich Ihre MAC-Adresse:<br>
  <span class="mac">)" + mac + R"(</span><br><br>
  Falls Sie Ihr Passwort vergessen, können Sie sich auf der
  Reset-Seite (<code>/reset</code>) mit der MAC-Adresse als
  Benutzername <em>und</em> Passwort anmelden, um das Gerät
  zurückzusetzen. <strong>Bitte MAC-Adresse sicher aufbewahren!</strong>
</div>
<form method="POST" action="/setup">
  <label>Benutzername</label>
  <input type="text" name="username" required autocomplete="username">
  <label>Passwort</label>
  <input type="password" name="password" required minlength="6" autocomplete="new-password">
  <label>Passwort bestätigen</label>
  <input type="password" name="password2" required autocomplete="new-password">
  <button type="submit" class="btn">Konto erstellen</button>
</form>)";
    return res->send(200, "text/html", page("Ersteinrichtung", body).c_str());
}

// ─── POST /setup ──────────────────────────────────────────────────────────────
static esp_err_t handleSetupPost(PsychicRequest* req, PsychicResponse* res) {
    if (Config.setupDone) return res->redirect("/login");

    String user  = param(req, "username"); user.trim();
    String pass  = param(req, "password");
    String pass2 = param(req, "password2");

    if (user.isEmpty() || pass.length() < 6) {
        return res->send(400, "text/html",
            page("Fehler", R"(<h1>Fehler</h1>
<p class="msg err">Benutzername leer oder Passwort zu kurz (mind. 6 Zeichen).</p>
<a href="/setup" class="btn btn-secondary">Zur&uuml;ck</a>)").c_str());
    }
    if (pass != pass2) {
        return res->send(400, "text/html",
            page("Fehler", R"(<h1>Fehler</h1>
<p class="msg err">Passw&ouml;rter stimmen nicht &uuml;berein.</p>
<a href="/setup" class="btn btn-secondary">Zur&uuml;ck</a>)").c_str());
    }

    Config.adminUser     = user;
    Config.adminPassHash = ConfigManager::sha256(pass);
    Config.setupDone     = true;
    Config.save();

    return loginAndRedirect(res, "/settings");
}

// ─── GET /login ───────────────────────────────────────────────────────────────
static esp_err_t handleLoginGet(PsychicRequest* req, PsychicResponse* res) {
    if (!Config.setupDone)      return res->redirect("/setup");
    if (isAuthenticated(req))   return res->redirect("/settings");

    bool failed = req->hasParam("err");
    String msg  = failed ? R"(<p class="msg err">Ung&uuml;ltige Zugangsdaten.</p>)" : "";
    String body = "<h1>Anmeldung</h1>" + msg + R"(
<form method="POST" action="/login">
  <label>Benutzername</label>
  <input type="text" name="username" required autocomplete="username">
  <label>Passwort</label>
  <input type="password" name="password" required autocomplete="current-password">
  <button type="submit" class="btn">Anmelden</button>
</form>)";
    return res->send(200, "text/html", page("Login", body).c_str());
}

// ─── POST /login ──────────────────────────────────────────────────────────────
static esp_err_t handleLoginPost(PsychicRequest* req, PsychicResponse* res) {
    String user = param(req, "username");
    String pass = param(req, "password");

    if (user == Config.adminUser &&
        ConfigManager::sha256(pass) == Config.adminPassHash) {
        return loginAndRedirect(res, "/settings");
    }
    return res->redirect("/login?err=1");
}

// ─── GET /logout ──────────────────────────────────────────────────────────────
static esp_err_t handleLogout(PsychicRequest* req, PsychicResponse* res) {
    String token = req->getCookie("session");
    if (!token.isEmpty()) sessions.erase(token);

    res->setCode(302);
    res->addHeader("Location", "/login");
    res->addHeader("Set-Cookie",
        "session=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    return res->send();
}

// ─── GET /settings ────────────────────────────────────────────────────────────
static esp_err_t handleSettingsGet(PsychicRequest* req, PsychicResponse* res) {
    if (!isAuthenticated(req)) return res->redirect("/login");

    bool saved  = req->hasParam("saved");
    String msg  = saved ? R"(<p class="msg ok">Einstellungen gespeichert.</p>)" : "";

    // Use "delim" raw string delimiter to avoid early termination by )"
    // inside placeholder="(leer = unveraendert)"
    String body = navBar() + "<h1>Einstellungen</h1>" + msg + R"delim(
<form method="POST" action="/settings">
  <h2>WLAN</h2>
  <label>SSID</label>
  <input type="text" name="wifi_ssid" value=")delim" + Config.wifiSSID + R"delim(" required>
  <label>Passwort</label>
  <input type="password" name="wifi_pass" placeholder="(leer = unver&auml;ndert)">

  <h2>QR-Code URL</h2>
  <label>Basis-URL</label>
  <input type="url" name="base_url" value=")delim" + Config.baseURL + R"delim(" placeholder="https://example.com/api">
  <p class="info">Aufruf: <code>basis-url?{param}={qr-inhalt}</code></p>
  <label>Parameter-Name</label>
  <input type="text" name="url_param" value=")delim" + Config.urlParam + R"delim(">

  <h2>Passwort &auml;ndern</h2>
  <label>Neues Passwort (leer = unver&auml;ndert, mind. 6 Zeichen)</label>
  <input type="password" name="new_pass" autocomplete="new-password">
  <label>Neues Passwort best&auml;tigen</label>
  <input type="password" name="new_pass2" autocomplete="new-password">

  <button type="submit" class="btn">Speichern</button>
</form>
<p style="margin-top:16px;font-size:.82rem;color:#999">
  Reset: <a href="/reset">/reset</a>
</p>)delim";

    return res->send(200, "text/html", page("Einstellungen", body).c_str());
}

// ─── POST /settings ───────────────────────────────────────────────────────────
static esp_err_t handleSettingsPost(PsychicRequest* req, PsychicResponse* res) {
    if (!isAuthenticated(req)) return res->redirect("/login");

    String ssid    = param(req, "wifi_ssid");
    String wpass   = param(req, "wifi_pass");
    String url     = param(req, "base_url");
    String prm     = param(req, "url_param");
    String newPass = param(req, "new_pass");
    String newPass2= param(req, "new_pass2");

    if (!newPass.isEmpty()) {
        if (newPass.length() < 6 || newPass != newPass2) {
            return res->send(400, "text/html",
                page("Fehler", navBar() + R"(<h1>Fehler</h1>
<p class="msg err">Passwort zu kurz oder stimmt nicht &uuml;berein.</p>
<a href="/settings" class="btn btn-secondary">Zur&uuml;ck</a>)").c_str());
        }
        Config.adminPassHash = ConfigManager::sha256(newPass);
    }

    if (!ssid.isEmpty())  Config.wifiSSID  = ssid;
    if (!wpass.isEmpty()) Config.wifiPass  = wpass;
    Config.baseURL  = url;
    Config.urlParam = prm.isEmpty() ? "data" : prm;
    Config.save();

    return res->redirect("/settings?saved=1");
}

// ─── GET /update ──────────────────────────────────────────────────────────────
static esp_err_t handleUpdateGet(PsychicRequest* req, PsychicResponse* res) {
    if (!isAuthenticated(req)) return res->redirect("/login");

    String partition = String(esp_ota_get_running_partition()->label);
    String body = navBar() + R"(<h1>Firmware-Update</h1>
<p class="msg ok">Aktive Partition: <strong>)" + partition + R"(</strong></p>
<form method="POST" action="/update" enctype="multipart/form-data">
  <label>Firmware-Datei (.bin)</label>
  <input type="file" name="firmware" accept=".bin" required>
  <button type="submit" class="btn">Hochladen &amp; installieren</button>
</form>
<p class="info" style="margin-top:8px">Das Ger&auml;t startet nach dem Update automatisch neu.</p>)";
    return res->send(200, "text/html", page("Firmware-Update", body).c_str());
}

// ─── POST /update  (multipart/form-data via PsychicUploadHandler) ─────────────
// The handler object is created in begin() – see registerRoutes().

// ─── GET /reset ───────────────────────────────────────────────────────────────
static esp_err_t handleResetGet(PsychicRequest* req, PsychicResponse* res) {
    bool authed = isAuthenticated(req);
    bool failed = req->hasParam("err");
    String mac  = getMac();
    String body = "<h1>Werksreset</h1>";

    if (!authed) {
        String errMsg = failed
            ? R"(<p class="msg err">Zugangsdaten falsch.</p>)" : "";
        body += errMsg + R"(
<div class="msg warn">
  Benutzername <strong>und</strong> Passwort m&uuml;ssen jeweils die
  <strong>MAC-Adresse</strong> dieses Ger&auml;tes sein.
</div>
<form method="POST" action="/reset/factory">
  <label>Benutzername (= MAC-Adresse)</label>
  <input type="text" name="mac_user" placeholder=")" + mac + R"(" required autocomplete="off">
  <label>Passwort (= MAC-Adresse)</label>
  <input type="password" name="mac_pass" placeholder=")" + mac + R"(" required autocomplete="off">
  <button type="submit" class="btn btn-danger">Auf Werkseinstellungen zur&uuml;cksetzen</button>
</form>
<p class="info" style="margin-top:10px">MAC-Adresse dieses Ger&auml;ts: <span class="mac">)" + mac + R"(</span></p>)";
    } else {
        body += R"(
<p class="msg err">&#9888; Diese Aktion l&ouml;scht <strong>alle</strong> Einstellungen und Zugangsdaten unwiderruflich!</p>
<form method="POST" action="/reset/factory">
  <input type="hidden" name="authed" value="1">
  <button type="submit" class="btn btn-danger">Jetzt zur&uuml;cksetzen</button>
</form>
<a href="/settings" class="btn btn-secondary" style="margin-top:8px">Abbrechen</a>)";
    }

    return res->send(200, "text/html", page("Werksreset", body).c_str());
}

// ─── POST /reset/factory ──────────────────────────────────────────────────────
static esp_err_t handleResetFactory(PsychicRequest* req, PsychicResponse* res) {
    bool authed = isAuthenticated(req);

    if (!authed) {
        String macUser = param(req, "mac_user"); macUser.trim(); macUser.toUpperCase();
        String macPass = param(req, "mac_pass"); macPass.trim(); macPass.toUpperCase();
        String expected = getMac(); // already upper-case with colons

        if (macUser != expected || macPass != expected) {
            return res->redirect("/reset?err=1");
        }
    }

    sessions.clear();
    Config.factoryReset();

    esp_err_t err = res->send(200, "text/html",
        page("Reset", R"(<h1>Werksreset abgeschlossen</h1>
<p class="msg ok">Alle Einstellungen wurden gel&ouml;scht. Das Ger&auml;t startet in 3 Sekunden neu...</p>
<script>setTimeout(()=>{window.location='/setup'},3000)</script>)").c_str());

    delay(3000);
    ESP.restart();
    return err;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  SSL Certificate  (mbedTLS 3.x, 40-year self-signed, stored in NVS)
// ═══════════════════════════════════════════════════════════════════════════════

#define SSL_NVS_NS    "sslcert"
#define SSL_KEY_CERT  "cert"
#define SSL_KEY_PK    "pk"
#define SSL_KEY_VALID "valid"
#define SSL_KEY_VER   "ver"
// Bump this when cert extensions change → forces NVS regeneration on next boot
#define SSL_CERT_VER  2

static bool generateCert(String& certPEM, String& keyPEM) {
    Serial.println("[SSL] Generating RSA-2048 self-signed certificate ...");
    Serial.println("[SSL] This may take up to 2 minutes on first boot.");

    // ── Heap-allocate all large mbedTLS structures ────────────────────────────
    // Stack-allocating these (~10+ KB total) overflows the 8 KB Arduino task stack.
    mbedtls_pk_context*       key     = new (std::nothrow) mbedtls_pk_context;
    mbedtls_entropy_context*  entropy = new (std::nothrow) mbedtls_entropy_context;
    mbedtls_ctr_drbg_context* drbg    = new (std::nothrow) mbedtls_ctr_drbg_context;
    mbedtls_x509write_cert*   crt     = new (std::nothrow) mbedtls_x509write_cert;
    uint8_t*                  certBuf = new (std::nothrow) uint8_t[4096]();
    uint8_t*                  keyBuf  = new (std::nothrow) uint8_t[4096]();

    bool ok = false;

    if (!key || !entropy || !drbg || !crt || !certBuf || !keyBuf) {
        Serial.println("[SSL] Out of heap memory for cert generation!");
    } else {
        mbedtls_pk_init(key);
        mbedtls_entropy_init(entropy);
        mbedtls_ctr_drbg_init(drbg);
        mbedtls_x509write_crt_init(crt);

        // RSA-2048 keygen blocks for 30-120 s – disable task WDT for this task
        disableLoopWDT();

        do {
            const char* pers = "qrreader_ssl";
            if (mbedtls_ctr_drbg_seed(drbg, mbedtls_entropy_func, entropy,
                                       (const unsigned char*)pers, strlen(pers))) break;
            if (mbedtls_pk_setup(key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) break;
            if (mbedtls_rsa_gen_key(mbedtls_pk_rsa(*key),
                                    mbedtls_ctr_drbg_random, drbg, 2048, 65537)) break;

            mbedtls_x509write_crt_set_version(crt, MBEDTLS_X509_CRT_VERSION_3);
            mbedtls_x509write_crt_set_md_alg(crt, MBEDTLS_MD_SHA256);
            mbedtls_x509write_crt_set_subject_key(crt, key);
            mbedtls_x509write_crt_set_issuer_key(crt, key);

            if (mbedtls_x509write_crt_set_subject_name(
                    crt, "CN=qrreader.local,O=QRReader,C=DE")) break;
            if (mbedtls_x509write_crt_set_issuer_name(
                    crt, "CN=qrreader.local,O=QRReader,C=DE")) break;
            // Valid 2026-01-01 → 2066-01-01  (40 years)
            if (mbedtls_x509write_crt_set_validity(
                    crt, "20260101000000", "20660101000000")) break;

            // Serial number (mbedTLS 3.x API)
            uint8_t serial[8] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
            if (mbedtls_x509write_crt_set_serial_raw(crt, serial, sizeof(serial))) break;

            // Basic Constraints: not a CA
            if (mbedtls_x509write_crt_set_basic_constraints(crt, 0, -1)) break;

            // Key Usage: Digital Signature + Key Encipherment
            if (mbedtls_x509write_crt_set_key_usage(crt,
                    MBEDTLS_X509_KU_DIGITAL_SIGNATURE |
                    MBEDTLS_X509_KU_KEY_ENCIPHERMENT)) break;

            // Subject Alternative Name: DNS:qrreader.local
            // Required by Chrome/Firefox since RFC 6125 – without SAN the browser
            // sends a TLS fatal alert instead of showing the "not trusted" warning.
            // OID 2.5.29.17 = { 0x55, 0x1d, 0x11 }
            // Value = GeneralNames SEQUENCE { [2] IMPLICIT IA5String "qrreader.local" }
            {
                static const char     san_oid[] = "\x55\x1d\x11";
                static const uint8_t  san_val[] = {
                    0x30, 0x10,   // SEQUENCE (length 16)
                    0x82, 0x0e,   // [2] IMPLICIT IA5String – dNSName, length 14
                    'q','r','r','e','a','d','e','r','.','l','o','c','a','l'
                };
                if (mbedtls_x509write_crt_set_extension(crt, san_oid, 3, 0,
                        san_val, sizeof(san_val))) break;
            }

            // Extended Key Usage: id-kp-serverAuth (1.3.6.1.5.5.7.3.1)
            // OID 2.5.29.37 = { 0x55, 0x1d, 0x25 }
            {
                static const char     eku_oid[] = "\x55\x1d\x25";
                static const uint8_t  eku_val[] = {
                    0x30, 0x0a,                                  // SEQUENCE (length 10)
                    0x06, 0x08,                                  // OID (length 8)
                    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01  // serverAuth
                };
                if (mbedtls_x509write_crt_set_extension(crt, eku_oid, 3, 0,
                        eku_val, sizeof(eku_val))) break;
            }

            if (mbedtls_x509write_crt_pem(crt, certBuf, 4096,
                                           mbedtls_ctr_drbg_random, drbg)) break;
            if (mbedtls_pk_write_key_pem(key, keyBuf, 4096)) break;

            certPEM = String((char*)certBuf);
            keyPEM  = String((char*)keyBuf);
            ok = certPEM.length() > 0 && keyPEM.length() > 0;
        } while (false);

        enableLoopWDT();

        mbedtls_x509write_crt_free(crt);
        mbedtls_pk_free(key);
        mbedtls_entropy_free(entropy);
        mbedtls_ctr_drbg_free(drbg);
    }

    delete key;
    delete entropy;
    delete drbg;
    delete crt;
    delete[] certBuf;
    delete[] keyBuf;

    if (ok) Serial.println("[SSL] Certificate generated.");
    else    Serial.println("[SSL] Certificate generation FAILED.");
    return ok;
}

static bool loadOrCreateCert(String& certPEM, String& keyPEM) {
    Preferences prefs;
    prefs.begin(SSL_NVS_NS, /*readOnly=*/true);
    bool    valid  = prefs.getBool(SSL_KEY_VALID, false);
    uint8_t verNVS = prefs.getUChar(SSL_KEY_VER, 0);

    if (valid && verNVS == SSL_CERT_VER) {
        certPEM = prefs.getString(SSL_KEY_CERT, "");
        keyPEM  = prefs.getString(SSL_KEY_PK,   "");
        prefs.end();
        if (certPEM.length() > 0 && keyPEM.length() > 0) {
            Serial.printf("[SSL] Cert loaded from NVS (%u / %u bytes)\n",
                          certPEM.length(), keyPEM.length());
            return true;
        }
    } else {
        prefs.end();
        if (valid && verNVS != SSL_CERT_VER) {
            Serial.printf("[SSL] Cert version mismatch (NVS=%u, expected=%u) – regenerating\n",
                          verNVS, (uint8_t)SSL_CERT_VER);
        }
    }

    // Generate new cert
    if (!generateCert(certPEM, keyPEM)) return false;

    prefs.begin(SSL_NVS_NS, /*readOnly=*/false);
    prefs.putString(SSL_KEY_CERT, certPEM);
    prefs.putString(SSL_KEY_PK,   keyPEM);
    prefs.putBool(SSL_KEY_VALID, true);
    prefs.putUChar(SSL_KEY_VER, SSL_CERT_VER);
    prefs.end();
    Serial.println("[SSL] Cert saved to NVS.");
    return true;
}

// ─── GET /cert ────────────────────────────────────────────────────────────────
// Serves the self-signed certificate PEM for download / browser import.
static esp_err_t handleCert(PsychicRequest* req, PsychicResponse* res) {
    if (g_certPEM.isEmpty()) {
        return res->send(503, "text/plain", "Kein Zertifikat verfügbar.");
    }
    res->setCode(200);
    res->setContentType("application/x-pem-file");
    res->addHeader("Content-Disposition",
                   "attachment; filename=\"qrreader.pem\"");
    return res->send(200, "application/x-pem-file", g_certPEM.c_str());
}

// ─── GET /monitor ─────────────────────────────────────────────────────────────
static esp_err_t handleMonitor(PsychicRequest* req, PsychicResponse* res) {
    if (!isAuthenticated(req)) return res->redirect("/login");

    // Full-page layout (wider than the card template)
    static const char html[] = R"mon(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Monitor – QR Reader</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#1a1a2e;color:#e0e0e0;padding:16px}
h1{font-size:1.3rem;margin-bottom:12px;color:#a9c4e4}
nav{margin-bottom:12px;font-size:.85rem}
nav a{margin-right:14px;color:#569cd6;text-decoration:none}
nav a:hover{text-decoration:underline}
#log{background:#0d0d1a;border:1px solid #2a2a4a;border-radius:6px;
  padding:10px 14px;height:calc(100vh - 140px);overflow-y:auto;
  font-family:monospace;font-size:.82rem;line-height:1.55}
.entry{white-space:pre-wrap;word-break:break-all}
.ts{color:#6a9955}.cat-QR{color:#4ec9b0}.cat-HTTP{color:#ce9178}
.cat-ERR{color:#f48771}.cat-WIFI{color:#569cd6}.cat-SYS{color:#9cdcfe}
.cat-default{color:#d7ba7d}
#statusbar{margin-top:6px;font-size:.75rem;color:#555;display:flex;
  justify-content:space-between;align-items:center}
#dl-cert{color:#4ec9b0;font-size:.8rem;text-decoration:none}
#dl-cert:hover{text-decoration:underline}
</style>
</head>
<body>
<nav>
  <a href="/settings">Einstellungen</a>
  <a href="/update">Firmware-Update</a>
  <a href="/monitor">Monitor</a>
  <a href="/logout">Abmelden</a>
</nav>
<h1>Monitor</h1>
<div id="log"></div>
<div id="statusbar">
  <span id="status">Verbinde…</span>
  <a href="/cert" id="dl-cert">&#11015; Zertifikat herunterladen</a>
</div>
<script>
var since=0, paused=false;
var logEl=document.getElementById('log');
var statusEl=document.getElementById('status');

function fmtTs(ms){
  var s=Math.floor(ms/1000),h=Math.floor(s/3600),
      m=Math.floor((s%3600)/60),ss=s%60;
  return [h,m,ss].map(function(x){return String(x).padStart(2,'0')}).join(':');
}
var catClass={QR:'cat-QR',HTTP:'cat-HTTP',ERR:'cat-ERR',WIFI:'cat-WIFI',SYS:'cat-SYS'};
function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}

function appendEntries(arr){
  if(!arr.length)return;
  var atBottom=logEl.scrollHeight-logEl.scrollTop<=logEl.clientHeight+40;
  var frag=document.createDocumentFragment();
  arr.forEach(function(e){
    var d=document.createElement('div');
    d.className='entry';
    var cc=catClass[e.c]||'cat-default';
    d.innerHTML='<span class="ts">['+fmtTs(e.t)+']</span> '
      +'<span class="'+cc+'">['+esc(e.c)+']</span> '+esc(e.m);
    frag.appendChild(d);
    since=e.i+1;
  });
  logEl.appendChild(frag);
  if(atBottom && !paused) logEl.scrollTop=logEl.scrollHeight;
}

logEl.addEventListener('scroll',function(){
  paused=(logEl.scrollHeight-logEl.scrollTop>logEl.clientHeight+60);
});

function poll(){
  fetch('/monitor/log?since='+since)
    .then(function(r){return r.json();})
    .then(function(data){
      appendEntries(data);
      statusEl.textContent='Letzte Aktualisierung: '+new Date().toLocaleTimeString()
        +(paused?' (pausiert – scrollen zum Fortfahren)':'');
    })
    .catch(function(){statusEl.textContent='Verbindungsfehler';});
}
poll();
setInterval(poll,2000);
</script>
</body>
</html>)mon";

    return res->send(200, "text/html", html);
}

// ─── GET /monitor/log ─────────────────────────────────────────────────────────
static esp_err_t handleMonitorLog(PsychicRequest* req, PsychicResponse* res) {
    if (!isAuthenticated(req)) return res->send(401, "application/json", "[]");

    uint32_t since = 0;
    if (req->hasParam("since")) {
        since = (uint32_t)req->getParam("since")->value().toInt();
    }

    String json = AppLogger.getJson(since);
    res->addHeader("Cache-Control", "no-store");
    return res->send(200, "application/json", json.c_str());
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Route registration
// ═══════════════════════════════════════════════════════════════════════════════

static void registerRoutes(PsychicHttpServer* srv) {
    srv->on("/",         HTTP_GET,  handleRoot);
    srv->on("/setup",    HTTP_GET,  handleSetupGet);
    srv->on("/setup",    HTTP_POST, handleSetupPost);
    srv->on("/login",    HTTP_GET,  handleLoginGet);
    srv->on("/login",    HTTP_POST, handleLoginPost);
    srv->on("/logout",   HTTP_GET,  handleLogout);
    srv->on("/settings", HTTP_GET,  handleSettingsGet);
    srv->on("/settings", HTTP_POST, handleSettingsPost);
    srv->on("/update",   HTTP_GET,  handleUpdateGet);
    srv->on("/reset",    HTTP_GET,  handleResetGet);
    srv->on("/reset/factory", HTTP_POST, handleResetFactory);
    srv->on("/cert",         HTTP_GET,  handleCert);
    srv->on("/monitor",      HTTP_GET,  handleMonitor);
    srv->on("/monitor/log",  HTTP_GET,  handleMonitorLog);
    srv->on("/version",  HTTP_GET,  [](PsychicRequest* req, PsychicResponse* res) {
        return res->send(200, "application/json",
            "{\"version\":\"" FW_VERSION "\"}");
    });

    // ── OTA upload via multipart/form-data ──────────────────────────────────
    PsychicUploadHandler* otaHandler = new PsychicUploadHandler();

    otaHandler->onUpload([](PsychicRequest* req,
                             const String& /*filename*/,
                             uint64_t index,
                             uint8_t* data, size_t len, bool last) -> esp_err_t {
        if (index == 0) {
            g_otaAuthFailed = !isAuthenticated(req);
            g_otaError      = false;
            if (!g_otaAuthFailed && !Update.begin(UPDATE_SIZE_UNKNOWN))
                g_otaError = true;
        }
        if (!g_otaAuthFailed && !g_otaError) {
            if (Update.write(data, len) != len)
                g_otaError = true;
        }
        if (last && !g_otaAuthFailed && !g_otaError) {
            if (!Update.end(true))
                g_otaError = true;
        }
        return ESP_OK; // always OK to keep connection alive
    });

    otaHandler->onRequest([](PsychicRequest* req, PsychicResponse* res) -> esp_err_t {
        if (g_otaAuthFailed) return res->redirect("/login");
        if (g_otaError) {
            char msg[64];
            snprintf(msg, sizeof(msg),
                     "Update fehlgeschlagen (Fehler %d)", Update.getError());
            return res->send(500, "text/plain", msg);
        }
        esp_err_t err = res->send(200, "text/html",
            page("Update", navBar() + R"(<h1>Update erfolgreich</h1>
<p class="msg ok">Firmware installiert. Das Ger&auml;t startet neu...</p>)").c_str());
        delay(1000);
        ESP.restart();
        return err;
    });

    srv->on("/update", otaHandler);

    // ── 404 ─────────────────────────────────────────────────────────────────
    srv->onNotFound([](PsychicRequest* req, PsychicResponse* res) {
        return res->send(404, "text/html",
            page("404", R"(<h1>404 &ndash; Nicht gefunden</h1>
<p>Die angeforderte Seite existiert nicht.</p>
<a href="/" class="btn btn-secondary">Startseite</a>)").c_str());
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
//  WebServerManager
// ═══════════════════════════════════════════════════════════════════════════════

void WebServerManager::begin() {
    bool hasCert = loadOrCreateCert(g_certPEM, g_keyPEM);

    if (hasCert) {
        // ── HTTPS server (port 443) ──────────────────────────────────────────
        httpsServer = new PsychicHttpsServer(443);
        httpsServer->setCertificate(g_certPEM.c_str(), g_keyPEM.c_str());
        httpsServer->config.max_uri_handlers = 24;
        httpsServer->begin();
        registerRoutes(httpsServer);

        // ── HTTP server (port 80) – permanent redirect to HTTPS ──────────────
        httpServer = new PsychicHttpServer(80);
        httpServer->begin();
        httpServer->onNotFound([](PsychicRequest* req, PsychicResponse* res) {
            String url = "https://" + getMyIP() + req->uri();
            res->setCode(301);
            res->addHeader("Location", url.c_str());
            return res->send();
        });

        Serial.println("[WebSrv] HTTPS :443 started  (HTTP :80 -> redirect)");

    } else {
        // ── Fallback: plain HTTP ─────────────────────────────────────────────
        Serial.println("[WebSrv] WARNING: No SSL cert - falling back to HTTP :80");
        httpServer = new PsychicHttpServer(80);
        httpServer->begin();
        registerRoutes(httpServer);
    }
}

void WebServerManager::loop() {
    // PsychicHttp is event-driven – no polling needed.
    // Kept for API compatibility with main.cpp.
}

void WebServerManager::stop() {
    if (httpsServer) { delete httpsServer; httpsServer = nullptr; }
    if (httpServer)  { delete httpServer;  httpServer  = nullptr; }
}
