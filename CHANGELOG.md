# Changelog

Alle wesentlichen Änderungen dieses Projekts werden hier dokumentiert.

## 1.0.0 – 2026-09-13

### Hinzugefügt

- automatische Interface-Erkennung und erneute Erkennung nach Ausfällen
- Konfigurationsdatei und erweiterte CLI
- CPU-, Temperatur-, RAM- und Swap-Seiten
- drei getrennte LCD-Seiten für Netzwerk, CPU und Speicher
- Navigation des blauen G15-Erstmodells über L2–L5; L1 bleibt Appletwechsel
- automatische Wiederverbindung zu `g15daemon`
- adaptive Graphskalierung mit Ringpuffer
- Core- und CLI-Tests
- modulare Projektstruktur und Architekturdokumentation

### Geändert

- Geschwindigkeiten verwenden die reale monotone Messdauer
- Build und Installation unterstützen `PREFIX` und `DESTDIR`
- systemd-User-Unit startet dauerhaft neu und verwendet Schutzoptionen

### Entfernt

- sämtliche GPU-Seiten und NVIDIA-Abfragen aus dem Hauptprozess
