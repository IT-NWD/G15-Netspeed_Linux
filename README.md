# g15netspeed

System- und Netzwerkmonitor für das monochrome LCD der Logitech G15 unter
Linux. Das Programm zeigt Netzwerkverkehr, CPU-Auslastung und Arbeitsspeicher
als laufende Graphen an und verbindet sich über `g15daemon` mit dem Display.

> **Wichtig:** GPU-Monitoring und sämtliche NVIDIA-Abfragen sind absichtlich
> deaktiviert. Insbesondere werden weder `nvidia-smi` noch NVML aufgerufen.
> Hintergrund: [NVIDIA-/DPMS-Hinweis](docs/DPMS-NVIDIA-HINWEIS.md).

## Funktionen

- automatische Auswahl des Interfaces der IPv4-Standardroute
- manuelle Auswahl per CLI oder Konfiguration
- exakte Geschwindigkeitsberechnung mit monotoner Zeitmessung
- Schutz vor Peaks nach Interface- oder Zähler-Resets
- automatische Wiederverbindung zu `g15daemon`
- adaptive Graphskalierung, die einzelne Ausreißer ausblendet
- drei vorbereitete LCD-Seiten:
  - Netzwerk: Download, Upload und übertragene Datenmenge
  - CPU: Auslastung und Temperatur, sofern ein Sensor gefunden wird
  - Speicher: RAM und Swap
- Konfigurationsdatei unter `~/.config/g15netspeed/config`
- automatisierte Tests für Parser, Ringpuffer, Skalierung und CLI

## Voraussetzungen

- Linux mit `/proc` und `/sys`
- `g15daemon`
- `libg15`
- `libg15render`
- C11-kompatibler Compiler und `make`

Für Arch Linux werden die Pakete `g15daemon`, `libg15` und `libg15render`
beziehungsweise deren `-git`-Varianten benötigt.

## Bauen und testen

```bash
make
make test
```

Für eine besonders strenge Prüfung:

```bash
make clean
make test CFLAGS='-Wall -Wextra -Wpedantic -Werror -O2'
```

Das Diagnosewerkzeug für G15-Tastencodes wird separat gebaut:

```bash
make tools
./g15keytest
```

## Verwendung

```bash
# Interface automatisch erkennen
./g15netspeed

# Interface fest vorgeben
./g15netspeed --interface wlan0

# Langsamer aktualisieren und Messwerte ausgeben
./g15netspeed --refresh 500 --verbose

# Interfaces anzeigen
./g15netspeed --list-interfaces
```

Ein einzelnes Positionsargument bleibt kompatibel:

```bash
./g15netspeed wlan0
```

### Optionen

| Option | Bedeutung |
|---|---|
| `-i`, `--interface NAME` | Interface oder `auto` |
| `-r`, `--refresh MS` | Messintervall von 50 bis 5000 ms |
| `-R`, `--reconnect MS` | Wartezeit bis zur Wiederverbindung |
| `-s`, `--scale MODUS` | `adaptive` oder `maximum` |
| `-c`, `--config DATEI` | alternative Konfiguration |
| `-n`, `--no-config` | Konfigurationsdatei ignorieren |
| `-l`, `--list-interfaces` | Interfaces ausgeben |
| `-v`, `--verbose` | Messwerte ins Terminal/Journal schreiben |
| `-V`, `--version` | Programmversion ausgeben |
| `-h`, `--help` | Hilfe anzeigen |

### LCD-Tasten des blauen Erstmodells

| Physische Taste | g15daemon-Code | Funktion |
|---|---:|---|
| runder Applet-Knopf | `DISPLAY_L1` | bleibt bei `g15daemon` zum Appletwechsel |
| erster Displayknopf | `DISPLAY_L2` | vorherige Seite |
| zweiter Displayknopf | `DISPLAY_L3` | nächste Seite |
| dritter Displayknopf | `DISPLAY_L4` | vorheriges Interface |
| vierter Displayknopf | `DISPLAY_L5` | nächstes Interface |

M1–M3, MR und G1–G18 bleiben unberührt. Ein manueller Interfacewechsel beendet
die automatische Auswahl bis zum nächsten Programmstart. Details zum geprüften
Layout: [docs/HARDWARE-G15-V1.md](docs/HARDWARE-G15-V1.md).

## Konfiguration

Vorlage installieren:

```bash
mkdir -p ~/.config/g15netspeed
cp config/g15netspeed.conf.example ~/.config/g15netspeed/config
```

Beispiel:

```ini
interface=auto
refresh_ms=150
reconnect_ms=5000
verbose=false
scale=adaptive
```

CLI-Optionen überschreiben Werte aus der Konfigurationsdatei. Alternativ wird
`$XDG_CONFIG_HOME/g15netspeed/config` berücksichtigt.

## Installation

```bash
sudo make install
systemctl --user daemon-reload
systemctl --user enable --now g15netspeed.service
```

Standardmäßig wird nach `/usr/local` installiert. Für Paketbau oder alternative
Ziele unterstützt das Makefile `PREFIX`, `DESTDIR`, `BINDIR`,
`SYSTEMD_USER_DIR` und `DOCDIR`.

Der User-Service wartet intern auf `g15daemon` und verbindet sich nach einem
Ausfall automatisch erneut. Status und Logs:

```bash
systemctl --user status g15netspeed.service
journalctl --user -u g15netspeed.service -f
```

## Projektstruktur

```text
.
├── .github/workflows/     GitHub-Automatisierung
├── config/                Konfigurationsvorlage
├── docs/                  technische Hinweise
├── include/g15netspeed/   öffentliche Modul-Header
├── packaging/systemd/     Vorlage der systemd-User-Unit
├── src/                   Programmimplementierung
├── tests/                 Core- und CLI-Tests
├── tools/                 Diagnosewerkzeuge
├── LICENSE
├── Makefile
└── README.md
```

Die Modulgrenzen und der Datenfluss sind in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) beschrieben.

## Bekannte Grenzen

- Es wird jeweils ein Netzwerk-Interface dargestellt.
- Die automatische Auswahl bevorzugt eine IPv4-Standardroute; auf reinen
  IPv6-Systemen wird auf das erste aktive Nicht-Loopback-Interface gefallen.
- CPU-Temperatur hängt von einem unterstützten `hwmon`- oder Thermal-Sensor ab.
- Die Display-Tastencodes anderer G15-Revisionen können abweichen;
  `g15keytest --foreground` hilft bei der Diagnose.

## Entwicklung

Änderungen müssen `make test` bestehen und dürfen keine NVIDIA-Abfrage in den
Hauptprozess einführen. Weitere Regeln stehen in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Lizenz

MIT, siehe [LICENSE](LICENSE).
