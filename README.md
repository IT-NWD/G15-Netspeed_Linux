# g15netspeed

System-Monitor und Netzwerk-Geschwindigkeitsanzeige für das Logitech G15 LCD-Display.
Zeigt CPU- und RAM-Auslastung sowie Upload- und Download-Geschwindigkeit als scrollende Graphen auf dem G15-LCD an. Die GPU-Seite und alle `nvidia-smi`-Abfragen sind deaktiviert.

**Wichtig:** Die GPU-Abfrage bleibt absichtlich deaktiviert. Hintergrund und
Regeln für künftige Änderungen stehen in
[`DPMS-NVIDIA-HINWEIS.md`](DPMS-NVIDIA-HINWEIS.md).

## Abhängigkeiten

- **g15daemon** – LCD-Daemon für Logitech G15 (muss laufen)
- **libg15** – Low-Level-Bibliothek für G15-Kommunikation
- **libg15render** – Render-Bibliothek für das G15-LCD (Text, Linien, etc.)
- **g15daemon_client** – Client-Bibliothek zur Verbindung mit g15daemon

Auf Arch Linux:
```bash
pacman -S g15daemon libg15 libg15render
```

## Bauen & Installieren

```bash
make              # Kompiliert g15netspeed
make install      # Installiert nach /usr/local/bin/
make clean        # Entfernt Build-Artefakte
```

## Verwendung

```bash
# g15daemon muss laufen:
g15daemon

# Interface der IPv4-Standardroute automatisch erkennen:
./g15netspeed

# Anderes Interface angeben:
./g15netspeed --interface wlan0

# Aktualisierung alle 500 ms und Messwerte im Terminal ausgeben:
./g15netspeed --refresh 500 --verbose

# Verfügbare Interfaces anzeigen:
./g15netspeed --list-interfaces
```

Ein einzelnes Positionsargument wie `./g15netspeed wlan0` bleibt aus
Kompatibilitätsgründen möglich. `--refresh` akzeptiert 50 bis 5000 ms.

Beenden mit `Ctrl+C`. Das Display wird beim Beenden automatisch geleert.

## Architektur & Prozessdiagramm

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              HARDWARE                                       │
│                                                                             │
│   ┌──────────────┐         ┌──────────────┐         ┌──────────────┐       │
│   │ Logitech G15 │         │   CPU / RAM   │         │   Netzwerk   │       │
│   │  (USB HID)   │         │              │         │              │       │
│   └──────┬───────┘         └──────┬───────┘         └──────┬───────┘       │
│          │ USB                    │                        │               │
└──────────┼────────────────────────┼────────────────────────┼───────────────┘
           │                        │                        │
┌──────────┼────────────────────────┼────────────────────────┼───────────────┐
│          │              LINUX KERNEL                       │               │
│          ▼                        │                        │               │
│   ┌──────────────┐               │                        │               │
│   │  HID Driver  │               ▼                        ▼               │
│   │  (usbhid)    │        ┌─────────────┐         ┌─────────────┐        │
│   └──────┬───────┘        │ /proc/stat  │         │/proc/net/dev│        │
│          │                │ /proc/meminfo│         │ RX/TX-Zähler│        │
│          │                │ /proc/net/dev│         └──────┬──────┘        │
│          │                └──────┬──────┘                │               │
│          │ /dev/hidrawX          │ procfs                │               │
└──────────┼───────────────────────┼────────────────────────┼───────────────┘
           │                       │                        │
┌──────────┼───────────────────────┼────────────────────────┼───────────────┐
│          │             USERSPACE DAEMONS / TOOLS          │               │
│          ▼                       │                        ▼               │
│   ┌──────────────┐               │                 ┌─────────────┐       │
│   │  g15daemon   │               │                 │ keine externe│       │
│   │  (Daemon)    │               │                 │ GPU-Abfrage  │       │
│   │              │               │                 └──────┬──────┘       │
│   │ • LCD-Mux    │               │                        │             │
│   │ • Client-Mgmt│               │                        │              │
│   └──────┬───────┘               │                        │              │
│          │ Unix Socket           │                        │              │
│          │ (localhost:15550)      │                        │              │
└──────────┼───────────────────────┼────────────────────────┼──────────────┘
           │                       │                        │
┌──────────┼───────────────────────┼────────────────────────┼──────────────┐
│          │            SHARED LIBRARIES                    │              │
│          │                       │                        │              │
│   ┌──────┴──────────────┐        │                        │              │
│   │ libg15              │        │                        │              │
│   │ (USB-Kommunikation) │        │                        │              │
│   └─────────────────────┘        │                        │              │
│   ┌─────────────────────┐        │                        │              │
│   │ libg15render        │        │                        │              │
│   │ (Pixel/Text/Linien) │        │                        │              │
│   └─────────────────────┘        │                        │              │
│   ┌─────────────────────┐        │                        │              │
│   │ g15daemon_client    │        │                        │              │
│   │ (Socket-Client-API) │        │                        │              │
│   └─────────────────────┘        │                        │              │
│                                  │                        │              │
└──────────────────────────────────┼────────────────────────┼──────────────┘
                                   │                        │
┌──────────────────────────────────┼────────────────────────┼──────────────┐
│                        g15netspeed (Anwendung)            │              │
│                                  │                        │              │
│   ┌──────────────────────────────┼────────────────────────┼───────────┐  │
│   │                         main()                        │           │  │
│   │                              │                        │           │  │
│   │   ┌──────────────────────────┼────────────────────────┼────────┐  │  │
│   │   │              Datenquellen lesen                   │        │  │  │
│   │   │                          │                        │        │  │  │
│   │   │  read_net_bytes() ◄──────┘ fopen("/proc/net/dev")│        │  │  │
│   │   │       │                                           │        │  │  │
│   │   │       ├── rx_bytes (Download)                     │        │  │  │
│   │   │       └── tx_bytes (Upload)                       │        │  │  │
│   │   │                                                   │        │  │  │
│   │   │  read_cpu_usage() ◄──── fopen("/proc/stat")      │        │  │  │
│   │   │       └── CPU % (Differenz-Methode)               │        │  │  │
│   │   │                                                   │        │  │  │
│   │   │  read_ram_usage() ◄──── fopen("/proc/meminfo")   │        │  │  │
│   │   │       └── RAM %                                   │        │  │  │
│   │   │                                                   │        │  │  │
│   │   │  Keine GPU-Abfrage (absichtlich deaktiviert)      │        │  │  │
│   │   └───────────────────────────────────────────────────────────┘  │  │
│   │                          │                                       │  │
│   │   ┌──────────────────────▼────────────────────────────────────┐  │  │
│   │   │              Datenverarbeitung                             │  │  │
│   │   │                                                           │  │  │
│   │   │  • KB/s aus Byte-Differenz und realer Messdauer          │  │  │
│   │   │  • push_history() → Ringpuffer (HISTORY_SIZE=139)        │  │  │
│   │   │  • find_max() → dynamische Y-Skalierung                  │  │  │
│   │   │  • format_speed() → "1.2M", "45K", "3.5K"               │  │  │
│   │   └──────────────────────┬────────────────────────────────────┘  │  │
│   │                          │                                       │  │
│   │   ┌──────────────────────▼────────────────────────────────────┐  │  │
│   │   │              Rendering (libg15render)                     │  │  │
│   │   │                                                           │  │  │
│   │   │  g15r_clearScreen()                                       │  │  │
│   │   │  g15r_renderString() → Interface, CPU%, RAM%            │  │  │
│   │   │  g15r_renderString() → "DL", "UL", Speed-Labels         │  │  │
│   │   │  draw_graph()        → DL-Graph (y=10, wächst ↑)        │  │  │
│   │   │  g15r_drawLine()     → Trennlinie (y=27)                │  │  │
│   │   │  draw_graph()        → UL-Graph (y=28, invertiert ↓)    │  │  │
│   │   └──────────────────────┬────────────────────────────────────┘  │  │
│   │                          │                                       │  │
│   │   ┌──────────────────────▼────────────────────────────────────┐  │  │
│   │   │              Ausgabe (g15daemon_client)                   │  │  │
│   │   │                                                           │  │  │
│   │   │  g15_send(fd, canvas.buffer, G15_BUFFER_LEN)             │  │  │
│   │   │       │                                                   │  │  │
│   │   │       └──► Unix Socket ──► g15daemon ──► USB ──► G15 LCD │  │  │
│   │   └───────────────────────────────────────────────────────────┘  │  │
│   │                                                                  │  │
│   │   Hauptschleife: im konfigurierten Intervall wiederholen         │  │
│   └──────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│   Signal-Handler: SIGINT/SIGTERM → running=0 → Cleanup & Exit          │
└─────────────────────────────────────────────────────────────────────────┘


Datenfluss-Zusammenfassung:

  /proc/stat ─────────► read_cpu_usage() ──┐
  /proc/meminfo ──────► read_ram_usage() ──┤
  /proc/net/dev ──────► read_net_bytes() ──┤
                                           ▼
                                    ┌─────────────┐
                                    │  Berechnung  │
                                    │  & History   │
                                    └──────┬──────┘
                                           ▼
                                    ┌─────────────┐
                                    │ libg15render │
                                    │  (Canvas)    │
                                    └──────┬──────┘
                                           ▼
                                    ┌──────────────────┐
                                    │ g15daemon_client  │
                                    │  g15_send()       │
                                    └──────┬───────────┘
                                           ▼
                                    ┌─────────────┐
                                    │  g15daemon   │
                                    │  (Socket)    │
                                    └──────┬──────┘
                                           ▼
                                    ┌─────────────┐
                                    │   libg15     │
                                    │  (USB HID)   │
                                    └──────┬──────┘
                                           ▼
                                    ┌─────────────┐
                                    │  G15 LCD     │
                                    │  (160×43px)  │
                                    └─────────────┘
```

## Code-Übersicht

### Dateistruktur

| Datei                      | Beschreibung                                      |
|----------------------------|---------------------------------------------------|
| `g15netspeed.c`            | Gesamter Quellcode (Single-File)                  |
| `g15keytest.c`             | Diagnosewerkzeug für G15-Tastencodes             |
| `g15netspeed.service`      | systemd-User-Unit                                 |
| `DPMS-NVIDIA-HINWEIS.md`   | Hintergrund zur deaktivierten NVIDIA-Abfrage     |
| `tests/test-cli.sh`        | Automatische Tests für CLI und GPU-Freiheit      |
| `Makefile`                 | Build-System                                      |
| `README.md`                | Diese Dokumentation                               |

### Konfigurierbare Konstanten (`#define`)

| Konstante        | Wert    | Beschreibung                                                        |
|------------------|---------|---------------------------------------------------------------------|
| `LCD_WIDTH`      | 160     | Breite des G15-LCD in Pixeln                                        |
| `LCD_HEIGHT`     | 43      | Höhe des G15-LCD in Pixeln                                         |
| `GRAPH_WIDTH`    | 139     | Breite der Graphen in Pixeln                                        |
| `GRAPH_HEIGHT`   | 16      | Höhe des Download-Graphen in Pixeln                                 |
| `GRAPH_DL_Y`     | 2       | Y-Position des Download-Graphen (derzeit unbenutzt, siehe Layout)   |
| `GRAPH_UL_Y`     | 24      | Y-Position des Upload-Graphen (derzeit unbenutzt, siehe Layout)     |
| `GRAPH_X`        | 20      | X-Position beider Graphen (Platz links für Labels)                  |
| `HISTORY_SIZE`   | 139     | Anzahl gespeicherter Messwerte (= `GRAPH_WIDTH`)                   |
| `DEFAULT_UPDATE_MS` | 150  | Standard-Aktualisierungsintervall in Millisekunden                  |
| `MIN_UPDATE_MS`  | 50      | Kleinstes erlaubtes Aktualisierungsintervall                        |
| `MAX_UPDATE_MS`  | 5000    | Größtes erlaubtes Aktualisierungsintervall                          |

### Funktionen

#### `read_cpu_usage(void)`
- **Zweck:** Liest die CPU-Auslastung aus `/proc/stat` und berechnet den Prozentsatz als Differenz zum vorherigen Aufruf.
- **Rückgabe:** CPU-Auslastung in Prozent (0–100).

#### `read_ram_usage(void)`
- **Zweck:** Liest `MemTotal` und `MemAvailable` aus `/proc/meminfo` und berechnet die RAM-Auslastung.
- **Rückgabe:** RAM-Auslastung in Prozent (0–100).

#### `read_net_bytes(const char *iface, unsigned long long *rx, unsigned long long *tx)`
- **Zweck:** Liest die empfangenen (`rx`) und gesendeten (`tx`) Bytes für ein Interface aus `/proc/net/dev`.
- **Rückgabe:** `0` bei Erfolg, `-1` bei Fehler (Datei nicht lesbar oder Interface nicht gefunden).
- **Hinweis:** Die Datei wird bei jedem Aufruf neu geöffnet und geschlossen, da `/proc/net/dev` ein virtuelles Dateisystem ist und die Werte bei jedem Lesen aktualisiert werden.

#### `format_speed(double kbps, char *buf, size_t len)`
- **Zweck:** Formatiert eine Geschwindigkeit (in KB/s) als lesbaren String.
- **Logik:**
  - ≥ 1024 KB/s → `"X.XM"` (Megabyte/s)
  - ≥ 10 KB/s → `"XXK"` (ohne Dezimalstelle)
  - < 10 KB/s → `"X.XK"` (mit Dezimalstelle)

#### `draw_graph(g15canvas *canvas, double *history, int count, int x, int y, int w, int h, double max_val, int inverted)`
- **Zweck:** Zeichnet einen scrollenden Balkendiagramm-Graphen auf den Canvas.
- **Parameter:**
  - `history` / `count` – Array mit Messwerten und aktuelle Anzahl
  - `x, y, w, h` – Position und Größe des Graphen
  - `max_val` – Maximaler Wert für die Skalierung (Y-Achse)
  - `inverted` – Wenn gesetzt, wächst der Graph von oben nach unten (für Upload)
- **Details:** Zeichnet zuerst einen Rahmen, dann für jeden Wert einen vertikalen Balken. Zusätzlich wird eine Verbindungslinie zwischen benachbarten Balken gezeichnet.

#### `find_max(double *history, int count)`
- **Zweck:** Findet den maximalen Wert im History-Array. Wird für die dynamische Y-Achsen-Skalierung verwendet.

#### `push_history(double *history, int *count, int max_size, double value)`
- **Zweck:** Fügt einen neuen Wert zum History-Array hinzu. Wenn das Array voll ist, werden alle Werte um eine Position nach links geschoben (ältester Wert fällt raus) – das erzeugt den Scroll-Effekt.

#### `main()`
- **Ablauf:**
  1. Optionen auswerten und Interface automatisch erkennen oder übernehmen
  2. Signal-Handler für sauberes Beenden registrieren
  3. Verbindung zum g15daemon herstellen (`new_g15_screen`)
  4. Initiales Lesen der Byte-Zähler
  5. **Hauptschleife:**
     - `nanosleep` für das konfigurierte Intervall
     - Aktuelle Bytes lesen
     - tatsächliche Messdauer mit `CLOCK_MONOTONIC` bestimmen
     - Zähler-Reset erkennen und künstliche Peaks verhindern
     - Differenz anhand der realen Messdauer in KB/s umrechnen
     - History aktualisieren
     - Maximum finden (Minimum: 10 KB/s für sinnvolle Skalierung)
     - LCD-Canvas leeren und neu zeichnen (Labels, Graphen, Trennlinie)
     - Canvas an g15daemon senden
  6. Bei `SIGINT`/`SIGTERM`: Display leeren, Verbindung schließen

### LCD-Layout

```
+----------------------------------------------------------+ y=0
| enp7s0                         CPU:45% RAM:62%          |
+----------------------------------------------------------+
| DL   |▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓| | y=10..25
| 1.2K |▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓| | (wächst ↑)
|------+------------------------------------------------+--| y=27 (Trennlinie)
| UL   |▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓| | y=28..41
| 0.3K |▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓| | (wächst ↓)
+----------------------------------------------------------+ y=43
         x=20                                        x=159
```

- **Zeile 0:** Interface-Name (links) und CPU-/RAM-Auslastung (rechts)
- **y=10–25:** Download-Graph mit Label links (wächst nach oben)
- **y=27:** Horizontale Trennlinie
- **y=28–41:** Upload-Graph (invertiert, wächst nach unten von der Trennlinie aus) mit Label links

## Häufige Anpassungen

### Interface festlegen

Standardmäßig wird das Interface der IPv4-Standardroute erkannt. Ein festes
Interface kann per Option übergeben werden:

```bash
g15netspeed --interface wlan0
g15netspeed --list-interfaces
```

### Aktualisierungsrate ändern

Das Intervall lässt sich ohne Neukompilierung einstellen:

```bash
g15netspeed --refresh 500
```

Die Geschwindigkeitsberechnung verwendet die tatsächlich vergangene monotone
Zeit und bleibt daher auch bei Scheduler-Verzögerungen korrekt.

### Graph-Größe / Position ändern
- `GRAPH_X` – Horizontaler Offset (Platz für Labels)
- `GRAPH_WIDTH` – Breite des Graphen (auch `HISTORY_SIZE` anpassen!)
- `GRAPH_HEIGHT` – Höhe des DL-Graphen
- UL-Graph-Höhe ist hardcoded auf 14px in `main()` → bei Bedarf als Konstante extrahieren

### Mindest-Skalierung der Y-Achse
In `main()` wird `dl_max` / `ul_max` auf mindestens 10 KB/s gesetzt, damit der Graph bei wenig Traffic nicht wild ausschlägt. Diesen Wert bei Bedarf anpassen.

### Geschwindigkeits-Formatierung
`format_speed()` anpassen, z.B. für Bits statt Bytes oder andere Schwellwerte.

## Autostart (systemd)

Eine systemd-User-Unit ist im Repository enthalten (`g15netspeed.service`).

### Installation

```bash
# Service-Datei in den User-systemd-Ordner kopieren:
mkdir -p ~/.config/systemd/user
cp g15netspeed.service ~/.config/systemd/user/

# Service aktivieren (startet automatisch beim Login):
systemctl --user enable g15netspeed.service

# Sofort starten:
systemctl --user start g15netspeed.service
```

### Anderes Interface verwenden

Die Service-Datei anpassen, z.B. für `wlan0`:
```ini
ExecStart=/usr/local/bin/g15netspeed --interface wlan0
```

### Nützliche Befehle

```bash
# Status prüfen:
systemctl --user status g15netspeed.service

# Logs anzeigen:
journalctl --user -u g15netspeed.service -f

# Stoppen:
systemctl --user stop g15netspeed.service

# Autostart deaktivieren:
systemctl --user disable g15netspeed.service
```

**Hinweis:** g15daemon muss als System-Service laufen (`systemctl enable g15daemon.service`), bevor g15netspeed starten kann.

## Bekannte Einschränkungen

- **Nur ein Interface:** Es wird nur ein Interface gleichzeitig überwacht.
- **IPv6-only:** Die automatische Auswahl bevorzugt die IPv4-Standardroute und fällt anschließend auf ein aktives Interface zurück.
- **UL-Graph-Höhe hardcoded:** Die Upload-Graph-Höhe (14px) ist direkt im `main()` angegeben statt als Konstante.

## Lizenz

Veröffentlicht unter der MIT-Lizenz, siehe [`LICENSE`](LICENSE).
