# Debug Tools

## http_logger

Einfacher HTTP-Server zum Debuggen von HTTP-Aufrufen vom ESP32-S3.
Gibt jeden eingehenden Request mit Zeitstempel, Absender-IP, Headern und Parametern auf der Konsole aus.

Zwei Implementierungen stehen zur Verfügung – beide verhalten sich identisch:

| Tool | Datei | Voraussetzung |
|---|---|---|
| Python | `python/http_logger.py` | Python 3 (keine Zusatzpakete) |
| Perl   | `perl/http_logger.pl`   | Perl (Windows: Strawberry Perl) |

### Starten

```bash
# Python – Port 80 (Root/Admin):
sudo python3 python/http_logger.py

# Python – ohne Root:
python3 python/http_logger.py 8080

# Perl – Port 80 (Root/Admin):
sudo perl perl/http_logger.pl

# Perl – ohne Root:
perl perl/http_logger.pl 8080
```

### Konfiguration im ESP32

In den Einstellungen (`/settings`) die Basis-URL auf den PC mit dem Logger zeigen:

```
http://192.168.1.10/api        ← Port 80
http://192.168.1.10:8080/api   ← alternativer Port
```
