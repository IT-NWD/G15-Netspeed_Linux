# Architektur

## Datenfluss

```text
/proc/net/dev ──► network ──► Übertragungsrate ──┐
/proc/stat ─────► metrics ──► CPU ───────────────┤
/proc/meminfo ──► metrics ──► RAM/Swap ─────────┤
/sys/class/* ───► metrics ──► CPU-Temperatur ───┤
                                                  ▼
                                       history + display
                                                  │
                                                  ▼
                                      g15daemon_client
                                                  │
                                                  ▼
                                           Logitech G15
```

Es existiert bewusst kein GPU-Datenpfad.

## Module

### `config`

Lädt Standardwerte und optional
`$XDG_CONFIG_HOME/g15netspeed/config` beziehungsweise
`~/.config/g15netspeed/config`. Danach werden CLI-Argumente angewendet, sodass
sie Konfigurationswerte überschreiben.

### `network`

Parst `/proc/net/dev`, listet Interfaces und liest RX-/TX-Zähler. Die
automatische Auswahl nutzt zunächst `/proc/net/route` und danach den
`operstate` verfügbarer Nicht-Loopback-Interfaces.

### `metrics`

Berechnet CPU-Auslastung aus zwei `/proc/stat`-Messungen und liest RAM sowie
Swap aus `/proc/meminfo`. CPU-Temperatur wird höchstens einmal pro Sekunde aus
einem passenden `hwmon`- oder Thermal-Sensor gelesen.

### `history`

Implementiert einen Ringpuffer ohne Verschieben des gesamten Arrays. Die
adaptive Skalierung verwendet ab 20 Messwerten das 95. Perzentil plus Reserve,
damit einzelne Peaks den Graphen nicht dauerhaft zusammendrücken.

### `display`

Rendert Netzwerk-, CPU- und Speicherseite mit `libg15render`. Das Modul kennt
keine Datenquellen und erhält ausschließlich bereits aufbereitete Messwerte.

### `main`

Koordiniert Sampling, automatische Interface-Erholung, Tasteneingaben, Rendering und die
Wiederverbindung zu `g15daemon`. Netzwerkdaten werden auch während einer
getrennten Displayverbindung weiter gesammelt.

Nach einer Verbindung fordert der Client den Display-Vordergrund an, aber nicht
die exklusive Keyhandler-Rolle. Dadurch empfängt er seine Displayknöpfe, während
L1 weiterhin regulär zwischen den Applets wechseln kann.

Die Displayknöpfe werden per nicht blockierendem `select`/`recv` ausgewertet.
L1 bleibt dem Appletwechsel von `g15daemon` vorbehalten; L2/L3 navigieren durch
Seiten und L4/L5 durch Interfaces. Ein Interfacewechsel setzt Baseline und
Netzwerkverlauf zurück, damit kein künstlicher Peak entsteht.

## Ausfallverhalten

- Netzwerkzähler sinken: Baseline und Netzwerkverlauf werden zurückgesetzt.
- automatisches Interface verschwindet: Standardroute wird erneut erkannt.
- festes Interface verschwindet: Warnung wird einmal ausgegeben, danach wird
  weiter auf die Rückkehr gewartet.
- g15daemon verschwindet: Socket wird geschlossen und im konfigurierten
  Intervall neu aufgebaut.
- CPU-Temperatur fehlt: Anzeige verwendet `N/A`; andere Metriken laufen weiter.

## Sicherheitsgrenze für GPU-Monitoring

Der Hauptprozess darf weder externe NVIDIA-Werkzeuge starten noch NVML laden.
Details und Voraussetzungen für jede spätere Neubewertung stehen in
[DPMS-NVIDIA-HINWEIS.md](DPMS-NVIDIA-HINWEIS.md).
