# ESP32-S3 QR USB Reader

Ein PlatformIO/Arduino-Projekt für den **ESP32-S3 DevKitC-1**, das einen per USB angeschlossenen QR-Code-Scanner ausliest und bei jedem Scan eine konfigurierbare URL aufruft. Alle Einstellungen sind über ein integriertes Web-Interface verwaltbar.

---

## Features

- **USB HID Host** – liest QR-Code-Scanner, die sich als USB-HID-Tastatur anmelden (Standard bei nahezu allen kommerziellen Scannern)
- **HTTP-Callback** – bei jedem Scan wird `BASE_URL?PARAM=<QR-Inhalt>` per GET aufgerufen
- **HTTPS Web-Interface** (Port 443) – Einstellungen, OTA-Firmware-Update, Login, Reset
- **Selbst-signiertes TLS-Zertifikat** – wird beim ersten Start automatisch generiert (RSA-2048, 40 Jahre Gültigkeit) und dauerhaft in NVS gespeichert; HTTP Port 80 leitet auf HTTPS um
- **Erster Start** – Benutzer wird beim ersten Aufruf angelegt; Passwort wird nur als SHA-256-Hash gespeichert
- **OTA-Update** – zwei App-Partitionen ermöglichen sicheres Firmware-Update über den Browser
- **mDNS** – erreichbar als `https://qrreader.local`
- **Fallback-AP** – ist kein WLAN konfiguriert oder die Verbindung schlägt fehl, öffnet der ESP32-S3 einen eigenen Access Point

---

## Hardware

| Komponente | Hinweis |
|---|---|
| ESP32-S3 DevKitC-1 | 8 MB Flash (Standard) |
| USB-QR-Code-Scanner | muss sich als USB-HID-Tastatur anmelden |
| USB-OTG-Adapter | USB-A (Buchse) auf USB-C oder Micro-B (Stecker) |

> **Wichtig:** Den Scanner am **USB-OTG-Port** des DevKitC-1 anschließen (der direkt am ESP32-S3-Chip hängt), **nicht** am USB-UART-Port. Die serielle Ausgabe läuft weiterhin über den USB-UART-Bridge-Port.

---

## Partitionstabelle

8 MB Flash mit zwei OTA-App-Slots:

| Name | Typ | Größe |
|---|---|---|
| nvs | NVS | 20 KB |
| otadata | OTA-Daten | 8 KB |
| app0 (ota_0) | Firmware-Slot 1 | 3,2 MB |
| app1 (ota_1) | Firmware-Slot 2 | 3,2 MB |
| spiffs | Dateisystem | 1,6 MB |

---

## Schnellstart

### 1. Flashen

```bash
pio run -t upload
```

### 2. SSL-Zertifikat wird generiert

Beim allerersten Start generiert der ESP32-S3 automatisch ein selbst-signiertes RSA-2048-Zertifikat. **Dieser Vorgang kann bis zu 2 Minuten dauern.** Der Fortschritt ist im seriellen Monitor sichtbar. Das Zertifikat wird in NVS gespeichert und bei jedem weiteren Start sofort geladen.

Da es sich um ein selbst-signiertes Zertifikat handelt, zeigt der Browser eine Sicherheitswarnung. Diese muss einmalig bestätigt werden ("Trotzdem fortfahren" o. Ä.).

### 3. Ersten Benutzer anlegen

Nach der Zertifikatsgenerierung den Browser öffnen:

```
https://qrreader.local/setup
```

*(Alternativ die IP-Adresse aus dem seriellen Monitor verwenden, z. B. `https://192.168.1.100/setup`)*

Auf der Setup-Seite wird die **MAC-Adresse des Gerätes** angezeigt.
> **Wichtig:** MAC-Adresse notieren und sicher aufbewahren! Sie wird als Notfall-Zugang für den Werksreset benötigt.

Benutzername und Passwort vergeben – das Passwort wird nur als SHA-256-Hash gespeichert.

### 4. Einstellungen konfigurieren

Nach dem Login unter `/settings`:

| Feld | Beschreibung |
|---|---|
| WLAN SSID / Passwort | Zugangsdaten für das Heimnetz |
| Basis-URL | Vollständige URL inkl. Schema, z. B. `https://example.com/api` |
| Parameter-Name | Name des Query-Parameters, Standard: `data` |

Beispiel-Aufruf nach einem Scan: `https://example.com/api?data=ABC123`

---

## Web-Routen

Alle Routen laufen über **HTTPS (Port 443)**. HTTP (Port 80) leitet permanent auf HTTPS um.

| Route | Zugang | Funktion |
|---|---|---|
| `/` | – | Weiterleitung (Setup → Login → Einstellungen) |
| `/setup` | Nur bei erstem Start | Benutzer anlegen + MAC-Adresse anzeigen |
| `/login` | Öffentlich | Anmeldung |
| `/logout` | – | Sitzung beenden |
| `/settings` | Login erforderlich | WLAN, URL, Passwort ändern |
| `/update` | Login erforderlich | OTA-Firmware-Upload |
| `/reset` | Siehe unten | Werksreset |

---

## Werksreset (`/reset`)

Die Reset-Seite (`https://<IP>/reset`) ist ohne Login erreichbar, erfordert aber eine Authentifizierung mit der **MAC-Adresse des Geräts**:

- **Benutzername:** MAC-Adresse im Format `A1:B2:C3:D4:E5:F6` (Großbuchstaben, mit Doppelpunkten)
- **Passwort:** dieselbe MAC-Adresse

Die MAC-Adresse wird sowohl auf der Reset-Seite als auch auf der Setup-Seite angezeigt und ist jederzeit im seriellen Monitor sichtbar.

Nach dem Reset werden alle gespeicherten Einstellungen und Zugangsdaten gelöscht und der ESP32-S3 startet neu. Beim nächsten Aufruf erscheint wieder die Ersteinrichtungs-Seite (`/setup`).

Ist man bereits eingeloggt, genügt ein Klick auf den Reset-Button ohne erneute Passwortabfrage.

### Was wird beim Reset gelöscht / behalten?

| Daten | Nach Reset |
|---|---|
| WLAN-Zugangsdaten (SSID + Passwort) | **gelöscht** |
| Benutzername + Passwort-Hash | **gelöscht** |
| Basis-URL + URL-Parameter | **gelöscht** |
| SSL-Zertifikat | **bleibt erhalten** (separater NVS-Bereich) |

Das SSL-Zertifikat bleibt absichtlich erhalten, damit kein erneuter Generierungsvorgang (bis zu 2 Minuten) beim Neustart nötig ist.

### Nach dem Reset: Verbindung zum Gerät

Da keine WLAN-Daten mehr vorhanden sind, startet der ESP32-S3 automatisch als **WLAN Access Point**.

**Schritt 1 – Mit dem AP verbinden**

Der AP-Name setzt sich aus den letzten drei Bytes der MAC-Adresse zusammen:

```
QRReader-XXYYZZ
```

*(z. B. `QRReader-D4E5F6` – der genaue Name ist jederzeit im seriellen Monitor sichtbar)*

Mit diesem Netzwerk per Smartphone, Tablet oder PC verbinden. Ein WLAN-Passwort ist für den AP nicht gesetzt.

**Schritt 2 – Setup-Seite aufrufen**

Nach der WLAN-Verbindung im Browser aufrufen:

```
https://192.168.4.1/setup
```

> Der Browser zeigt eine Zertifikatswarnung (selbst-signiert). Mit „Trotzdem fortfahren" o. Ä. bestätigen.

**Schritt 3 – Neues Benutzerkonto anlegen**

Auf der Setup-Seite erneut Benutzername und Passwort vergeben. Die MAC-Adresse wird wieder angezeigt.

**Schritt 4 – WLAN konfigurieren**

Nach dem Login unter `/settings` die WLAN-Zugangsdaten (SSID + Passwort) eintragen und speichern. Das Gerät verbindet sich beim nächsten Neustart mit dem konfigurierten Netzwerk und ist dann wieder über `https://qrreader.local` erreichbar.

---

## Projektstruktur

```
├── platformio.ini        # Build-Konfiguration
├── partitions.csv        # Partitionstabelle (2× OTA)
└── src/
    ├── main.cpp          # Setup, WiFi, Haupt-Loop
    ├── config.h/.cpp     # Einstellungen (NVS / Preferences), SHA-256
    ├── usb_hid_host.h/.cpp  # USB-Host-Stack (ESP-IDF), HID-Report-Parser
    ├── http_caller.h/.cpp   # HTTP-GET mit URL-Encoding
    ├── web_server.h/.cpp    # Async-Webserver, Login, OTA, Reset
```

---

## Abhängigkeiten

- [PsychicHttp](https://github.com/hoeken/PsychicHttp) – Arduino-freundlicher Wrapper um den nativen `esp_https_server` aus dem ESP-IDF
- [UrlEncode](https://github.com/plageoj/urlencode) – URL-Encode/Decode-Hilfsfunktionen
- [ArduinoJson](https://arduinojson.org/) ≥ 7
- ESP-IDF USB Host Library (im Platform-Paket enthalten)
- mbedTLS SHA-256 & RSA (im ESP-IDF enthalten – wird für HTTPS-Zertifikat und Passwort-Hashing genutzt)

## SSL-Hinweis

Da das Zertifikat selbst-signiert ist, zeigt jeder Browser eine Sicherheitswarnung. Das Zertifikat muss **einmalig** manuell als Ausnahme akzeptiert werden. Alternativ kann das erzeugte Zertifikat aus dem seriellen Monitor exportiert und im Browser als vertrauenswürdig hinterlegt werden.
