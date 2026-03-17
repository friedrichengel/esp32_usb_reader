#pragma once

#include <Arduino.h>

// ─── WebServerManager ─────────────────────────────────────────────────────────
// HTTPS server (port 443) with self-signed certificate.
// HTTP port 80 runs a permanent redirect to HTTPS.
//
// Routes:
//   GET  /           → redirect (setup / settings)
//   GET/POST /setup  → first-time user creation
//   GET/POST /login  → login
//   GET  /logout     → clear session
//   GET/POST /settings  → WiFi + URL config  (auth required)
//   GET  /update        → OTA upload page    (auth required)
//   POST /update        → OTA binary upload  (auth required)
//   GET  /reset         → factory-reset page (MAC-addr auth)
//   POST /reset/factory → execute reset      (MAC-addr auth)
class WebServerManager {
public:
    /** Start servers. Generates SSL cert on first call (can take ~60 s). */
    void begin();

    /** Must be called from Arduino loop() – drives the server event loop. */
    void loop();

    void stop();
};

extern WebServerManager WebSrv;
