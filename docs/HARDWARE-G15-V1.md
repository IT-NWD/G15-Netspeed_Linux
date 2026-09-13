# Logitech G15 – blaues Erstmodell

Dieses Projekt wird auf dem ersten G15-Modell mit blauer Beleuchtung,
18 G-Tasten und den Modustasten M1, M2, M3 und MR entwickelt.

## Getestete Displayknöpfe

Die vier Displayknöpfe wurden am 13. September 2026 mit laufendem `g15daemon`,
gestopptem `g15netspeed.service` und `g15keytest --foreground` von links nach
rechts erfasst:

| Reihenfolge | libg15-Bezeichnung | Bitmaske |
|---:|---|---:|
| 1 | `G15_KEY_L2` | `0x00800000` |
| 2 | `G15_KEY_L3` | `0x01000000` |
| 3 | `G15_KEY_L4` | `0x02000000` |
| 4 | `G15_KEY_L5` | `0x04000000` |

Der runde Applet-Knopf wird von `g15daemon` als L1 beziehungsweise Cycle-Key
verarbeitet und deshalb nicht als normaler Tastencode an den aktiven Client
weitergereicht. Er bleibt unangetastet.

## Nicht für die Displaysteuerung verwendet

- M1, M2 und M3 wählen die Belegungsebene der G-Tasten.
- MR gehört zur Makroaufzeichnung.
- G1 bis G18 bleiben frei für ihre eigentlichen Makros.

## Diagnoseablauf

```bash
systemctl --user stop g15netspeed.service
make tools
./g15keytest --foreground
systemctl --user start g15netspeed.service
```

Das Diagnosewerkzeug fordert nur den Display-Vordergrund an. Es übernimmt nicht
die exklusive `G15DAEMON_KEY_HANDLER`-Rolle.
