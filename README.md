# g15netspeed

System-Monitor und Netzwerk-Geschwindigkeitsanzeige für das Logitech G15 LCD-Display.
Zeigt CPU-, GPU- und RAM-Auslastung sowie Upload- und Download-Geschwindigkeit als scrollende Graphen auf dem G15-LCD an.

## Abhängigkeiten

- **g15daemon** – LCD-Daemon für Logitech G15 (muss laufen)
- **libg15** – Low-Level-Bibliothek für G15-Kommunikation
- **libg15render** – Render-Bibliothek für das G15-LCD (Text, Linien, etc.)
- **g15daemon_client** – Client-Bibliothek zur Verbindung mit g15daemon
- **nvidia-utils** – NVIDIA-Treiber mit `nvidia-smi` (für GPU-Auslastung, optional)

Auf Arch Linux:
```bash
pacman -S g15daemon libg15 libg15render nvidia-utils
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

# Standard-Interface (enp7s0):
./g15netspeed

# Anderes Interface angeben:
./g15netspeed wlan0
```

Beenden mit `Ctrl+C`. Das Display wird beim Beenden automatisch geleert.

### Tasten

| Taste | Funktion                                              |
|-------|-------------------------------------------------------|
| L1    | Seite umschalten (Netzwerk → CPU → GPU → RAM)         |
| L2    | Interface wechseln (nur auf der Netzwerk-Seite)       |

## Architektur & Prozessdiagramm

```
┌────────────────────────────────────────────────────────────────────────────┐
│                              HARDWARE                                      │
│                                                                            │
│   ┌──────────────┐         ┌──────────────┐         ┌──────────────┐       │
│   │ Logitech G15 │         │   CPU / RAM  │         │  NVIDIA GPU  │       │
│   │  (USB HID)   │         │              │         │              │       │
│   └──────┬───────┘         └──────┬───────┘         └──────┬───────┘       │
│          │ USB                    │                        │               │
└──────────┼────────────────────────┼────────────────────────┼───────────────┘
           │                        │                        │
┌──────────┼────────────────────────┼────────────────────────┼───────────────┐
│          │              LINUX KERNEL                       │               │
│          ▼                        │                        │               │
│   ┌──────────────┐                │                        │               │
│   │  HID Driver  │                ▼                        ▼               │
│   │  (usbhid)    │        ┌──────────────┐         ┌─────────────┐         │
│   └──────┬───────┘        │ /proc/stat   │         │ nvidia.ko   │         │
│          │                │ /proc/meminfo│         │ (Kernel Mod)│         │
│          │                │ /proc/net/dev│         └──────┬──────┘         │
│          │                └──────┬───────┘                │                │
│          │ /dev/hidrawX          │ procfs                 │                │
└──────────┼───────────────────────┼────────────────────────┼────────────────┘
           │                       │                        │
┌──────────┼───────────────────────┼────────────────────────┼────────────────┐
│          │             USERSPACE DAEMONS / TOOLS          │                │
│          ▼                       │                        ▼                │
│   ┌──────────────┐               │                 ┌─────────────┐         │
│   │  g15daemon   │               │                 │ nvidia-smi  │         │
│   │  (Daemon)    │               │                 │ (CLI Tool)  │         │
│   │              │               │                 └──────┬──────┘         │
│   │ • LCD-Mux    │               │                        │ popen()        │
│   │ • Client-Mgmt│               │                        │                │
│   └──────┬───────┘               │                        │                │
│          │ Unix Socket           │                        │                │
│          │ (localhost:15550)     │                        │                │
└──────────┼───────────────────────┼────────────────────────┼────────────────┘
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
│   │   │  read_net_bytes() ◄──────┘ fopen("/proc/net/dev") │        │  │  │
│   │   │       │                                           │        │  │  │
│   │   │       ├── rx_bytes (Download)                     │        │  │  │
│   │   │       └── tx_bytes (Upload)                       │        │  │  │
│   │   │                                                   │        │  │  │
│   │   │  read_cpu_usage() ◄──── fopen("/proc/stat")       │        │  │  │
│   │   │       └── CPU % (Differenz-Methode)               │        │  │  │
│   │   │                                                   │        │  │  │
│   │   │  read_ram_usage() ◄──── fopen("/proc/meminfo")    │        │  │  │
│   │   │       └── RAM %                                   │        │  │  │
│   │   │                                                   │        │  │  │
│   │   │  read_gpu_usage() ◄───────────────────────────────┘        │  │  │
│   │   │       └── GPU % (popen("nvidia-smi"), 1s Cache)            │  │  │
│   │   └────────────────────────────────────────────────────────────┘  │  │
│   │                          │                                        │  │
│   │   ┌──────────────────────▼─────────────────────────────────────┐  │  │
│   │   │              Datenverarbeitung                             │  │  │
│   │   │                                                            │  │  │
│   │   │  • KB/s berechnen: (diff / 1024) * (1000 / UPDATE_MS)      │  │  │
│   │   │  • push_history() → Ringpuffer (HISTORY_SIZE=126)          │  │  │
│   │   │  • find_max() → dynamische Y-Skalierung                    │  │  │
│   │   │  • speed_unit() → "KB/s", "MB/s", "GB/s"                   │  │  │
│   │   │  • format_speed_value() → "1.2", "45.3", "3.5"             │  │  │
│   │   └──────────────────────┬─────────────────────────────────────┘  │  │
│   │                          │                                        │  │
│   │   ┌──────────────────────▼────────────────────────────────────┐   │  │
│   │   │              Rendering (libg15render)                     │   │  │
│   │   │                                                           │   │  │
│   │   │  g15r_clearScreen()                                       │   │  │
│   │   │  g15r_renderString() → Interface, CPU%, GPU%, RAM%        │   │  │
│   │   │  g15r_renderString() → "DL", "UL", Speed-Labels           │   │  │
│   │   │  draw_graph()        → DL-Graph (y=10, wächst ↑)          │   │  │
│   │   │  g15r_drawLine()     → Trennlinie (y=27)                  │   │  │
│   │   │  draw_graph()        → UL-Graph (y=28, invertiert ↓)      │   │  │
│   │   └──────────────────────┬────────────────────────────────────┘   │  │
│   │                          │                                        │  │
│   │   ┌──────────────────────▼────────────────────────────────────┐   │  │
│   │   │              Ausgabe (g15daemon_client)                   │   │  │
│   │   │                                                           │   │  │
│   │   │  g15_send(fd, canvas.buffer, G15_BUFFER_LEN)              │   │  │
│   │   │       │                                                   │   │  │
│   │   │       └──► Unix Socket ──► g15daemon ──► USB ──► G15 LCD  │   │  │
│   │   └───────────────────────────────────────────────────────────┘   │  │
│   │                                                                   │  │
│   │   Hauptschleife: alle 150ms (UPDATE_MS) wiederholen               │  │
│   └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│   Signal-Handler: SIGINT/SIGTERM → running=0 → Cleanup & Exit            │
└──────────────────────────────────────────────────────────────────────────┘


Datenfluss-Zusammenfassung:

  /proc/stat ─────────► read_cpu_usage() ──┐
  /proc/meminfo ──────► read_ram_usage() ──┤
  nvidia-smi ─────────► read_gpu_usage() ──┤
  /proc/net/dev ──────► read_net_bytes() ──┤
                                           ▼
                                    ┌─────────────┐
                                    │  Berechnung │
                                    │  & History  │
                                    └──────┬──────┘
                                           ▼
                                    ┌─────────────┐
                                    │ libg15render│
                                    │  (Canvas)   │
                                    └──────┬──────┘
                                           ▼
                                    ┌──────────────────┐
                                    │ g15daemon_client │
                                    │  g15_send()      │
                                    └──────┬───────────┘
                                           ▼
                                    ┌─────────────┐
                                    │  g15daemon  │
                                    │  (Socket)   │
                                    └──────┬──────┘
                                           ▼
                                    ┌─────────────┐
                                    │   libg15    │
                                    │  (USB HID)  │
                                    └──────┬──────┘
                                           ▼
                                    ┌─────────────┐
                                    │  G15 LCD    │
                                    │  (160×43px) │
                                    └─────────────┘
```

## Code-Übersicht

### Dateistruktur

| Datei                  | Beschreibung                                      |
|------------------------|---------------------------------------------------|
| `g15netspeed.c`        | Gesamter Quellcode (Single-File)                  |
| `g15keytest.c`         | Debug-Tool zum Auslesen der G15-Tastencodes       |
| `Makefile`             | Build-System                                      |
| `g15netspeed.service`  | systemd-User-Unit für Autostart                   |
| `README.md`            | Diese Dokumentation                               |

### Konfigurierbare Konstanten (`#define`)

| Konstante        | Wert    | Beschreibung                                                        |
|------------------|---------|---------------------------------------------------------------------|
| `LCD_WIDTH`      | 160     | Breite des G15-LCD in Pixeln                                        |
| `LCD_HEIGHT`     | 43      | Höhe des G15-LCD in Pixeln                                          |
| `GRAPH_WIDTH`    | 124     | Breite der Graphen in Pixeln                                        |
| `GRAPH_HEIGHT`   | 16      | Höhe des Download-Graphen in Pixeln                                 |
| `GRAPH_X`        | 35      | X-Position beider Graphen (Platz links für Labels)                  |
| `HISTORY_SIZE`   | 126     | Anzahl gespeicherter Messwerte (= `GRAPH_WIDTH`)                    |
| `UPDATE_MS`      | 150     | Aktualisierungsintervall in Millisekunden                           |
| `DEFAULT_IFACE`  | enp7s0  | Standard-Netzwerk-Interface                                         |
| `L1_KEY`         | 0x00800000 | Tastaturcode für die L1-Taste (1. LCD-Taste, Seitenumschaltung)  |
| `L2_KEY`         | 0x01000000 | Tastaturcode für die L2-Taste (2. LCD-Taste, Interface-Wechsel)  |
| `MAX_IFACES`     | 32         | Maximale Anzahl erkannter Netzwerk-Interfaces                    |
| `IFACE_NAME_LEN` | 32         | Maximale Länge eines Interface-Namens                            |

### Funktionen

#### `scan_interfaces(void)`
- **Zweck:** Liest alle Netzwerk-Interfaces aus `/proc/net/dev` ein (ohne `lo`).
- **Rückgabe:** Anzahl gefundener Interfaces.
- **Details:** Füllt das globale Array `iface_list[]` und setzt `iface_count`.

#### `find_iface_index(const char *iface)`
- **Zweck:** Sucht ein Interface in der Liste und gibt dessen Index zurück.
- **Rückgabe:** Index (0-basiert) oder `-1` wenn nicht gefunden.

#### `read_cpu_usage(void)`
- **Zweck:** Liest die CPU-Auslastung aus `/proc/stat` und berechnet den Prozentsatz als Differenz zum vorherigen Aufruf.
- **Rückgabe:** CPU-Auslastung in Prozent (0–100).

#### `read_ram_usage(void)`
- **Zweck:** Liest `MemTotal` und `MemAvailable` aus `/proc/meminfo` und berechnet die RAM-Auslastung.
- **Rückgabe:** RAM-Auslastung in Prozent (0–100).

#### `read_gpu_usage(void)`
- **Zweck:** Liest die GPU-Auslastung über `nvidia-smi` via `popen()`. Der Wert wird gecacht und maximal 1x pro Sekunde neu abgefragt.
- **Rückgabe:** GPU-Auslastung in Prozent (0–100), oder `-1` wenn `nvidia-smi` nicht verfügbar.

#### `read_net_bytes(const char *iface, unsigned long long *rx, unsigned long long *tx)`
- **Zweck:** Liest die empfangenen (`rx`) und gesendeten (`tx`) Bytes für ein Interface aus `/proc/net/dev`.
- **Rückgabe:** `0` bei Erfolg, `-1` bei Fehler (Datei nicht lesbar oder Interface nicht gefunden).
- **Hinweis:** Die Datei wird bei jedem Aufruf neu geöffnet und geschlossen, da `/proc/net/dev` ein virtuelles Dateisystem ist und die Werte bei jedem Lesen aktualisiert werden.

#### `speed_unit(double kbps)`
- **Zweck:** Gibt die passende Einheit als String zurück (`"KB/s"`, `"MB/s"` oder `"GB/s"`).
- **Logik:**
  - ≥ 1048576 KB/s → `"GB/s"`
  - ≥ 1024 KB/s → `"MB/s"`
  - < 1024 KB/s → `"KB/s"`

#### `format_speed_value(double kbps, char *buf, size_t len)`
- **Zweck:** Formatiert den Zahlenwert einer Geschwindigkeit (in KB/s) ohne Einheit, mit einer Nachkommastelle.
- **Logik:**
  - ≥ 1048576 KB/s → Wert in GB/s (z.B. `"1.2"`)
  - ≥ 1024 KB/s → Wert in MB/s (z.B. `"45.3"`)
  - < 1024 KB/s → Wert in KB/s (z.B. `"3.5"`)

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
  1. Interface aus Kommandozeile oder Default übernehmen
  2. Signal-Handler für sauberes Beenden registrieren
  3. Verbindung zum g15daemon herstellen (`new_g15_screen`)
  4. Initiales Lesen der Byte-Zähler
  5. **Hauptschleife:**
     - `nanosleep` für das konfigurierte Intervall
     - Aktuelle Bytes lesen
     - Differenz berechnen und auf KB/s hochrechnen: `(diff / 1024) * (1000 / UPDATE_MS)`
     - History aktualisieren
     - Maximum finden (Minimum: 10 KB/s für sinnvolle Skalierung)
     - LCD-Canvas leeren und neu zeichnen (Labels, Graphen, Trennlinie)
     - Canvas an g15daemon senden
  6. Bei `SIGINT`/`SIGTERM`: Display leeren, Verbindung schließen

### LCD-Layout

```
+----------------------------------------------------------+ y=0
| enp7s0                        D:1.23GB U:256.00MB        |
+----------------------------------------------------------+
| DL(KB/s)|▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓| | y=10..25
| 1.2     |▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓| | (wächst ↑)
|---------+----------------------------------------------+--| y=27 (Trennlinie)
| UL(KB/s)|▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓| | y=28..41
| 0.3     |▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓| | (wächst ↓)
+----------------------------------------------------------+ y=43
         x=33                                        x=159
```

- **Zeile 0:** Interface-Name (links) und Gesamtdatenmenge D:/U: (rechts)
- **y=10–25:** Download-Graph mit Label inkl. Einheit links, Zahlenwert darunter (wächst nach oben)
- **y=27:** Horizontale Trennlinie
- **y=28–41:** Upload-Graph (invertiert, wächst nach unten von der Trennlinie aus) mit Label inkl. Einheit links, Zahlenwert darunter

## Häufige Anpassungen

### Anderes Default-Interface
`DEFAULT_IFACE` in `g15netspeed.c` ändern. Verfügbare Interfaces anzeigen:
```bash
cat /proc/net/dev
```

### Aktualisierungsrate ändern
`UPDATE_MS` anpassen. Die KB/s-Berechnung skaliert automatisch auf 1 Sekunde:
```c
double dl_kbps = (double)(curr_rx - prev_rx) / 1024.0 * (1000.0 / UPDATE_MS);
```

### Graph-Größe / Position ändern
- `GRAPH_X` – Horizontaler Offset (Platz für Labels)
- `GRAPH_WIDTH` – Breite des Graphen (auch `HISTORY_SIZE` anpassen!)
- `GRAPH_HEIGHT` – Höhe des DL-Graphen
- UL-Graph-Höhe ist hardcoded auf 14px in `main()` → bei Bedarf als Konstante extrahieren

### Mindest-Skalierung der Y-Achse
In `main()` wird `dl_max` / `ul_max` auf mindestens 10 KB/s gesetzt, damit der Graph bei wenig Traffic nicht wild ausschlägt. Diesen Wert bei Bedarf anpassen.

### Geschwindigkeits-Formatierung
`speed_unit()` und `format_speed_value()` anpassen, z.B. für Bits statt Bytes oder andere Schwellwerte.

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
ExecStart=/usr/local/bin/g15netspeed wlan0
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
- **Kein Autodetect:** Das aktive Interface wird nicht automatisch erkannt.
- **32-Bit-Overflow:** `/proc/net/dev` kann bei >4 GB Traffic überlaufen (Kernel-abhängig). Die `unsigned long long`-Typen fangen das ab, aber die Differenzberechnung könnte bei einem Wrap-Around kurzzeitig falsche Werte liefern.
- **UL-Graph-Höhe hardcoded:** Die Upload-Graph-Höhe (14px) ist direkt im `main()` angegeben statt als Konstante.

## Lizenz

Dieses Projekt steht unter der MIT-Lizenz. Siehe [LICENSE](LICENSE) für Details.
