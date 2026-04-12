# ESP32-S3 QR USB Reader

Ein PlatformIO/Arduino-Projekt für den **ESP32-S3 DevKitC-1**, das einen per USB angeschlossenen QR-Code-Scanner ausliest und bei jedem Scan eine konfigurierbare URL aufruft. Alle Einstellungen sind über ein integriertes Web-Interface verwaltbar.

---

## Features

- **USB HID Host** – liest QR-Code-Scanner, die sich als USB-HID-Tastatur anmelden (Standard bei nahezu allen kommerziellen Scannern)
- **HTTP-Callback** – bei jedem Scan wird eine konfigurierbare URL per GET aufgerufen (Query-Parameter- oder Pfad-Modus, siehe unten)
- **HTTPS Web-Interface** (Port 443) – Einstellungen, OTA-Firmware-Update, Login, Reset
- **Selbst-signiertes TLS-Zertifikat** – wird beim ersten Start automatisch generiert (RSA-2048, 40 Jahre Gültigkeit) und dauerhaft in NVS gespeichert; HTTP Port 80 leitet auf HTTPS um
- **Erster Start** – Benutzer wird beim ersten Aufruf angelegt; Passwort wird nur als SHA-256-Hash gespeichert
- **OTA-Update** – zwei App-Partitionen ermöglichen sicheres Firmware-Update über den Browser
- **Live-Monitor** (`/monitor`) – zeigt QR-Scans, HTTP-Trigger und Systemereignisse als Live-Log im Browser (JS-Polling, kein Neuladen)
- **Zertifikat-Download** (`/cert`) – selbst-signiertes Zertifikat als `.pem` herunterladen und im Browser/System installieren
- **Firmware-Version** (`/version`) – liefert die aktuelle Version als JSON; Version ist in `src/version.h` definiert
- **mDNS** – erreichbar als `https://qrreader.local`
- **Fallback-AP** – ist kein WLAN konfiguriert oder die Verbindung schlägt fehl, öffnet der ESP32-S3 einen eigenen Access Point
- **WLAN per QR-Code einrichten** – im AP-Modus kann ein WLAN-QR-Code (Format `WIFI:T:WPA;S:...;P:...;;`) eingescannt werden, um die Zugangsdaten automatisch zu übernehmen
- **Werksreset per QR-Code** – ein geheimer QR-Code löst nach zweimaligem Scannen hintereinander einen Werksreset aus

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

### 2. WLAN per QR-Code einrichten (optional)

Ist noch kein WLAN konfiguriert, startet der ESP32-S3 im AP-Modus (`QRReader-XXYYZZ`). Statt die Zugangsdaten manuell im Web-Interface einzutragen, kann ein **WLAN-QR-Code** eingescannt werden:

1. WLAN-QR-Code für das Zielnetzwerk erzeugen (die meisten Router-Apps, Windows 11 und Android bieten das direkt an)
2. QR-Code vor den angeschlossenen Scanner halten
3. Der ESP32-S3 erkennt das `WIFI:T:WPA;S:...;P:...;;`-Format automatisch, speichert die Zugangsdaten und startet neu

> Der Scan funktioniert ausschließlich wenn der ESP32-S3 sich im AP-Modus befindet (kein WLAN verbunden). Im normalen Betrieb werden WLAN-QR-Codes wie jeder andere Code an die konfigurierte URL weitergeleitet.

### 3. SSL-Zertifikat wird generiert

Beim allerersten Start generiert der ESP32-S3 automatisch ein selbst-signiertes RSA-2048-Zertifikat. **Dieser Vorgang kann bis zu 2 Minuten dauern.** Der Fortschritt ist im seriellen Monitor sichtbar. Das Zertifikat wird in NVS gespeichert und bei jedem weiteren Start sofort geladen.

Da es sich um ein selbst-signiertes Zertifikat handelt, zeigt der Browser eine Sicherheitswarnung. Diese muss einmalig bestätigt werden ("Trotzdem fortfahren" o. Ä.).

### 4. Ersten Benutzer anlegen

Nach der Zertifikatsgenerierung den Browser öffnen:

```
https://qrreader.local/setup
```

*(Alternativ die IP-Adresse aus dem seriellen Monitor verwenden, z. B. `https://192.168.1.100/setup`)*

Auf der Setup-Seite wird die **MAC-Adresse des Gerätes** angezeigt.
> **Wichtig:** MAC-Adresse notieren und sicher aufbewahren! Sie wird als Notfall-Zugang für den Werksreset benötigt.

Benutzername und Passwort vergeben – das Passwort wird nur als SHA-256-Hash gespeichert.

### 5. Einstellungen konfigurieren

Nach dem Login unter `/settings`:

| Feld | Beschreibung |
|---|---|
| WLAN SSID / Passwort | Zugangsdaten für das Heimnetz |
| Basis-URL | Vollständige URL inkl. Schema (siehe Modi unten) |
| Parameter-Name | Name des Query-Parameters im Query-Modus, Standard: `data` |

#### URL-Modi

**Query-Modus** – kein Platzhalter in der Basis-URL, der QR-Inhalt wird als Query-Parameter angehängt:

```
Basis-URL:  https://example.com/api
Parameter:  data
→ Aufruf:   https://example.com/api?data=ABC123
```

**Pfad-Modus** – die Basis-URL enthält den festen Platzhalter `{data}`, der durch den URL-kodierten QR-Inhalt ersetzt wird:

```
Basis-URL:  http://10.42.2.196:5173/persons/finish/{data}
→ Aufruf:   http://10.42.2.196:5173/persons/finish/ABC123
```

Im Pfad-Modus wird das Feld „Parameter-Name" ignoriert.

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
| `/monitor` | Login erforderlich | Live-Log-Anzeige (QR-Scans, HTTP-Aufrufe, System) |
| `/monitor/log?since=N` | Login erforderlich | JSON-API: Logeinträge ab Index N |
| `/cert` | Öffentlich | Selbst-signiertes Zertifikat als `.pem` herunterladen |
| `/version` | Öffentlich | Firmware-Version als JSON: `{"version":"1.0.0"}` |
| `/reset` | Siehe unten | Werksreset |

### Zertifikat installieren

Um die Browser-Warnung dauerhaft zu vermeiden, kann das Zertifikat unter `/cert` heruntergeladen und manuell als vertrauenswürdig hinterlegt werden:

- **Windows:** Doppelklick auf `.pem` → „Zertifikat installieren" → „Lokaler Computer" → „Vertrauenswürdige Stammzertifizierungsstellen"
- **macOS:** Keychain Access → Datei importieren → Vertrauen auf „Immer vertrauen" setzen
- **Android / iOS:** `.pem`-Datei öffnen und als CA-Zertifikat installieren

### Monitor (`/monitor`)

Die Monitor-Seite zeigt alle Ereignisse in Echtzeit an (Polling alle 2 Sekunden, kein Seitenneulade):

| Kategorie | Inhalt |
|---|---|
| `Scan` | Gescannter QR-Code-Inhalt |
| `HTTP` | URL + HTTP-Statuscode bei jedem Trigger-Aufruf |
| `WEB` | Aufgerufene Web-Routen |
| `SYS` | Systemereignisse |
| `ERR` | Fehler |

Der Ringpuffer speichert die letzten **100 Einträge** im RAM (kein Verlust nach Neustart ist nicht garantiert – für Persistenz wäre SPIFFS nötig).

---

## Werksreset

### Über das Web-Interface (`/reset`)

Die Reset-Seite (`https://<IP>/reset`) ist ohne Login erreichbar, erfordert aber eine Authentifizierung mit der **MAC-Adresse des Geräts**:

- **Benutzername:** MAC-Adresse im Format `A1:B2:C3:D4:E5:F6` (Großbuchstaben, mit Doppelpunkten)
- **Passwort:** dieselbe MAC-Adresse

Die MAC-Adresse wird sowohl auf der Reset-Seite als auch auf der Setup-Seite angezeigt und ist jederzeit im seriellen Monitor sichtbar.

Nach dem Reset werden alle gespeicherten Einstellungen und Zugangsdaten gelöscht und der ESP32-S3 startet neu. Beim nächsten Aufruf erscheint wieder die Ersteinrichtungs-Seite (`/setup`).

Ist man bereits eingeloggt, genügt ein Klick auf den Reset-Button ohne erneute Passwortabfrage.

### Über QR-Code

Der Werksreset kann auch ohne Web-Interface ausgelöst werden – nützlich wenn kein Netzwerkzugang möglich ist:

1. Den geheimen Reset-QR-Code **zweimal hintereinander** scannen
2. Wird ein anderer QR-Code dazwischen eingescannt, wird der Zähler zurückgesetzt (Schutz vor versehentlichem Auslösen)
3. Nach dem zweiten Scan löscht das Gerät alle Einstellungen und startet neu

Der Reset-Code ist in `src/config.h` unter `RESET_QR_CODE` definiert. Der Code erscheint **niemals im Klartext im Log** – lediglich `Reset-Code erkannt (1/2)` bzw. `(2/2)` wird ausgegeben. Den QR-Code aus dem definierten String erzeugen, z. B. mit einem Online-QR-Generator oder einer Smartphone-App.

> **Hinweis:** Den Reset-QR-Code sicher aufbewahren – wer ihn kennt, kann das Gerät ohne Login zurücksetzen.

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

## Debug

### USB_DEBUG Flag

In `src/config.h` kann die ausführliche USB-HID-Diagnoseausgabe aktiviert werden:

```c
#define USB_DEBUG   // Kommentar-// entfernen = aktiv
```

Wenn aktiv, gibt der serielle Monitor für jeden USB-Transfer aus:
- Byte-Anzahl des empfangenen Reports
- Jeden erkannten Keycode mit Shift-Status und decodiertem Zeichen
- Den zusammengebauten Scan-String kurz vor dem Callback-Aufruf

Nach der Diagnose wieder auskommentieren, da die Ausgabe sehr umfangreich ist.

### HTTP Debug Logger

Startet einen einfachen HTTP-Server und gibt alle eingehenden Requests mit Zeitstempel, Absender-IP, Headern und GET-/POST-Parametern auf der Konsole aus. Nützlich um zu prüfen ob der ESP32-S3 bei einem Scan tatsächlich einen HTTP-Request absetzt und welche Daten übertragen werden.

In den ESP32-Einstellungen (`/settings`) die Basis-URL auf den PC zeigen:

```
http://192.168.1.10/api
http://192.168.1.10:8080/api   ← bei abweichendem Port
```

#### Python (`debug_tools/python/http_logger.py`)

**Voraussetzungen:** Python 3 (keine zusätzlichen Pakete nötig)

```bash
# Port 80 (benötigt Root/Admin):
sudo python3 debug_tools/python/http_logger.py

# Ohne Root – alternativer Port:
python3 debug_tools/python/http_logger.py 8080

# Windows:
python debug_tools\python\http_logger.py 8080
```

#### Perl (`debug_tools/perl/http_logger.pl`)

**Voraussetzungen:** Perl installiert (Windows: [Strawberry Perl](https://strawberryperl.com/))

```bash
# Port 80 (benötigt Root/Admin):
sudo perl debug_tools/perl/http_logger.pl

# Ohne Root – alternativer Port:
perl debug_tools/perl/http_logger.pl 8080
```

#### Beispielausgabe (beide Tools identisch)

```
────────────────────────────────────────────────────────────
[2024-03-22 14:23:05]  192.168.1.42:51234
────────────────────────────────────────────────────────────
  GET /api?data=ABC123 HTTP/1.1

  GET-Parameter:
    data                 = ABC123

  Header:
    Host: 192.168.1.10
    Connection: close
```

---

## Projektstruktur

```
├── platformio.ini        # Build-Konfiguration
├── partitions.csv        # Partitionstabelle (2× OTA)
└── src/
    ├── main.cpp          # Setup, WiFi, Haupt-Loop
    ├── config.h/.cpp        # Einstellungen (NVS / Preferences), SHA-256
    ├── usb_hid_host.h/.cpp  # USB-Host-Stack (ESP-IDF), HID-Report-Parser
    ├── http_caller.h/.cpp   # HTTP-GET mit URL-Encoding
    ├── web_server.h/.cpp    # Async-Webserver, Login, OTA, Reset, Monitor
    ├── logger.h/.cpp        # Ringpuffer-Logger (100 Einträge, Thread-safe)
    └── version.h            # Firmware-Version (FW_VERSION)
```

---

## Abhängigkeiten

- [PsychicHttp](https://github.com/hoeken/PsychicHttp) – Arduino-freundlicher Wrapper um den nativen `esp_https_server` aus dem ESP-IDF
- [UrlEncode](https://github.com/plageoj/urlencode) – URL-Encode/Decode-Hilfsfunktionen
- [ArduinoJson](https://arduinojson.org/) ≥ 7
- ESP-IDF USB Host Library (im Platform-Paket enthalten)
- mbedTLS SHA-256 & RSA (im ESP-IDF enthalten – wird für HTTPS-Zertifikat und Passwort-Hashing genutzt)

## SSL-Hinweis

Da das Zertifikat selbst-signiert ist, zeigt jeder Browser eine Sicherheitswarnung. Das Zertifikat kann über **`/cert`** als `.pem`-Datei heruntergeladen und einmalig im Betriebssystem oder Browser als vertrauenswürdig hinterlegt werden (Details siehe Abschnitt „Zertifikat installieren" oben). Danach erscheint keine Warnung mehr.

## Lizenz

Dieses Projekt steht unter der [MIT-Lizenz](LICENSE).
