#!/usr/bin/env python3
"""
HTTP Debug Logger für ESP32-S3 QR USB Reader
Gibt alle eingehenden Requests mit Zeitstempel, Absender-IP,
Headern und GET-/POST-Parametern auf der Konsole aus.

Verwendung:
    python http_logger.py          # Port 80 (benötigt Root/Admin)
    python http_logger.py 8080     # Alternativer Port ohne Root
"""

import sys
import socket
from datetime import datetime
from urllib.parse import urlparse, parse_qs, unquote_plus
from http.server import HTTPServer, BaseHTTPRequestHandler


class DebugHandler(BaseHTTPRequestHandler):

    def _handle(self):
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        client_ip, client_port = self.client_address

        print("─" * 60)
        print(f"[{timestamp}]  {client_ip}:{client_port}")
        print("─" * 60)
        print(f"  {self.command} {self.path} {self.request_version}")

        # GET-Parameter aus URL parsen
        parsed = urlparse(self.path)
        if parsed.query:
            params = parse_qs(parsed.query, keep_blank_values=True)
            print("\n  GET-Parameter:")
            for key, values in params.items():
                for val in values:
                    print(f"    {key:<20} = {val}")

        # Header ausgeben
        print("\n  Header:")
        for key, val in self.headers.items():
            print(f"    {key}: {val}")

        # Body lesen und ausgeben
        content_length = int(self.headers.get("Content-Length", 0))
        body = ""
        if content_length > 0:
            raw = self.rfile.read(content_length).decode("utf-8", errors="replace")
            content_type = self.headers.get("Content-Type", "")
            print(f"\n  Body ({content_length} Bytes):")
            if "application/x-www-form-urlencoded" in content_type:
                # URL-encoded POST-Parameter parsen
                params = parse_qs(raw, keep_blank_values=True)
                print("  POST-Parameter:")
                for key, values in params.items():
                    for val in values:
                        print(f"    {key:<20} = {val}")
            else:
                print(f"    {raw}")
            body = raw

        print()

        # Antwort senden
        response = (
            f"OK - Request empfangen um {timestamp} von {client_ip}:{client_port}\n"
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def do_GET(self):
        self._handle()

    def do_POST(self):
        self._handle()

    def log_message(self, format, *args):
        # Standard-Logging unterdrücken (wir haben eigene Ausgabe)
        pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 80

    try:
        server = HTTPServer(("", port), DebugHandler)
    except PermissionError:
        print(f"Fehler: Port {port} konnte nicht geöffnet werden.")
        print("Tipp: Ports < 1024 benötigen Root/Admin-Rechte.")
        print(f"      Alternativ: python http_logger.py 8080")
        sys.exit(1)
    except OSError as e:
        print(f"Fehler: {e}")
        sys.exit(1)

    # Alle lokalen IP-Adressen ermitteln
    ips = []
    try:
        hostname = socket.gethostname()
        infos = socket.getaddrinfo(hostname, None)
        for info in infos:
            ip = info[4][0]
            if not ip.startswith("127.") and ":" not in ip:  # kein Loopback, kein IPv6
                if ip not in ips:
                    ips.append(ip)
    except Exception:
        pass
    ips.insert(0, "127.0.0.1")  # Loopback immer anzeigen

    print("=" * 60)
    print(f"  HTTP Debug Logger lauscht auf Port {port}")
    print(f"  Erreichbar unter:")
    for ip in ips:
        print(f"    http://{ip}:{port}/")
    print(f"  Abbrechen mit Ctrl+C")
    print("=" * 60)
    print()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nServer beendet.")
        server.server_close()


if __name__ == "__main__":
    main()
