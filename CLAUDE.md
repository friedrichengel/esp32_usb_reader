# CLAUDE.md – ESP32-S3 QR USB Reader

## Projektübersicht

ESP32-S3 DevKitC-1 Firmware, die USB-HID-kompatible QR-Code-Scanner ausliest und HTTP-Callbacks auslöst. Konfiguration über ein HTTPS-Webinterface mit OTA-Updates, Session-Auth und selbst-signiertem TLS-Zertifikat.

## Build & Flash

```bash
# Build & Upload
pio run -t upload

# Serieller Monitor
pio device monitor --baud 115200
```

**Platform**: PlatformIO, Arduino-Framework für ESP32-S3
**Board**: `esp32-s3-devkitc-1`
**Upload-Speed**: 921600 Baud

## Projektstruktur

```
src/
├── main.cpp            # Einstiegspunkt: WiFi, QR-Callback, Reset-QR, Event-Loop
├── config.h/.cpp       # Konfigurationsverwaltung & NVS-Persistenz
├── usb_hid_host.h/.cpp # USB-HID-Host: FreeRTOS-Tasks, HID-Parsing, Tastaturlayout
├── web_server.h/.cpp   # HTTPS-Server: Routen, TLS, Login, OTA-Handler
├── http_caller.h/.cpp  # HTTP-GET-Client für Scan-Callbacks
├── logger.h/.cpp       # Thread-sicherer Ringpuffer-Logger (100 Einträge)
└── version.h           # Firmware-Versionskonstante
debug_tools/
├── python/http_logger.py   # Debug-HTTP-Server zum Empfangen von Callbacks
└── perl/http_logger.pl     # Dasselbe in Perl
```

## Wichtige Konfigurationsdateien

- **platformio.ini** – Build-Umgebung, Board, Libraries, Build-Flags
- **partitions.csv** – Flash-Partitionstabelle (8 MB, 2 OTA-Slots)
- **src/config.h** – Compile-Time-Konstanten: NVS-Keys, Reset-QR-Code, USB_DEBUG-Flag

## Libraries

| Library | Zweck |
|---------|-------|
| PsychicHttp | HTTPS-Server (Wrapper um ESP-IDF's esp_https_server) |
| ArduinoJson ^7.3.0 | JSON für API-Antworten & Konfiguration |
| UrlEncode ^1.0.1 | URL-Kodierung für HTTP-Callbacks |
| mbedTLS (built-in) | SHA-256, RSA-2048, TLS |
| ESP-IDF USB Host (built-in) | USB-HID-Stack |

## NVS-Konfigurationsschlüssel (Namespace: `qrreader`)

| Schlüssel | Bedeutung |
|-----------|-----------|
| `wifi_ssid`, `wifi_pass` | WLAN-Zugangsdaten |
| `base_url`, `url_param` | HTTP-Callback-Endpunkt |
| `admin_user`, `admin_hash` | Login (Passwort als SHA-256-Hash) |
| `setup_done` | Ersteinrichtungs-Flag |
| `scan_term` | Scan-Terminator: `crlf`, `cr`, `tab`, `none` |
| `kb_layout` | Tastaturlayout: `de` oder `us` |
| `dhcp_en`, `static_ip`, `static_sn`, `static_gw`, `static_dns` | Netzwerkkonfiguration |

SSL-Zertifikat wird separat in NVS gespeichert und beim Factory-Reset **nicht** gelöscht.

## Web-Routen

| Route | Auth | Zweck |
|-------|------|-------|
| `/setup` | Keine (nur Erststart) | Erste Benutzereinrichtung |
| `/login`, `/logout` | Öffentlich | Authentifizierung |
| `/settings` | Session | WLAN, URL, IP-Einstellungen |
| `/update` | Session | OTA-Firmware-Upload |
| `/monitor` | Session | Live-Ereignisprotokoll |
| `/monitor/log?since=N` | Session | JSON-API für neue Logs |
| `/cert` | Öffentlich | TLS-Zertifikat als PEM herunterladen |
| `/version` | Öffentlich | Firmware-Version als JSON |
| `/reset` | MAC-Adresse als Auth | Factory Reset |

**mDNS**: `https://qrreader.local`
**HTTP** Port 80 → 302-Redirect auf HTTPS Port 443

## USB-HID & QR-Scanning

- Unterstützt DE (QWERTZ) und US (QWERTY) Tastaturlayout
- Scan-Terminatoren: Enter (CR/CRLF), Tab oder 300 ms Timeout
- WiFi-QR-Format: `WIFI:T:WPA;S:SSID;P:Password;H:false;;`
- Reset-QR: Muss zweimal hintereinander gescannt werden (ohne anderen Scan dazwischen)

## Debugging

USB-Debug aktivieren in `src/config.h`:
```cpp
#define USB_DEBUG   // Auskommentieren entfernen
```

Debug-HTTP-Server starten (empfängt Scan-Callbacks):
```bash
python3 debug_tools/python/http_logger.py
# oder
perl debug_tools/perl/http_logger.pl
```

## Wichtige Hinweise

- **Erster Boot**: Zertifikatsgenerierung (RSA-2048) dauert bis zu 2 Minuten
- **Factory Reset**: Löscht alle NVS-Einstellungen **außer** dem SSL-Zertifikat
- **OTA**: Zwei App-Partitionen (`ota_0`/`ota_1`) je 3,2 MB für sichere Updates
- **AP-Fallback**: Wenn kein WLAN konfiguriert oder Verbindung fehlschlägt, öffnet das Gerät einen Access Point
