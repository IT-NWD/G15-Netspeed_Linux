# NVIDIA-DPMS-Hinweis für g15netspeed

## Hintergrund

`g15netspeed` hatte ursprünglich regelmäßig `nvidia-smi` aufgerufen, um die
GPU-Auslastung auf dem G15-LCD anzuzeigen. Auf dem System mit RTX 4080 SUPER,
DisplayPort-Monitor und NVIDIA Open Kernel Module 615.71.09 führte diese
Abfrage im Zusammenhang mit Display-Standby/Wakeup wiederholt zu einem
uninterruptiblen (`D`) Zustand. Der NVIDIA-Grafikpfad konnte dadurch den
DPMS-Wakeup blockieren; in einzelnen Fällen hing anschließend auch der
gesamte Desktop.

## Aktuelle Schutzmaßnahme

Die GPU-Anzeige und die Funktion `read_gpu_usage()` sind absichtlich entfernt.
Das Programm darf im normalen Aktualisierungszyklus keine NVIDIA-Abfrage
starten. CPU-, RAM- und Netzwerkdaten bleiben aktiv.

Vor einer Änderung, die GPU-Monitoring wieder einführt, muss zuerst geprüft
werden, wie Abfragen während DPMS-Aus/An, Suspend und Resume zuverlässig
pausiert werden. Ein Timeout im Child-Prozess allein ist kein ausreichender
Schutz: Der NVIDIA-Treiber kann den aufrufenden Prozess im Kernel trotzdem im
`D`-Zustand festhalten.

## Verifikation

Am 13.09.2026 wurde mit der deaktivierten GPU-Abfrage ein etwa fünfminütiger
DPMS-Standby/Wakeup-Test durchgeführt. `g15netspeed` lief weiter; es gab keinen
D-Zustand, keine Watchdog-Auslösung und keine neuen NVIDIA-/DRM-Fehler. Der
frühere Test mit aktivem GPU-Polling hatte dagegen einen hängenden
`nvidia-smi`-Prozess erzeugt und den Recovery-Watchdog ausgelöst.

## Regeln für künftige Änderungen

1. Keine `nvidia-smi`- oder NVML-Abfrage im Hauptloop ohne erneuten DPMS-A/B-Test.
2. Bei GPU-Monitoring muss die Abfrage vor Display-Standby pausiert und erst
   nach dem Wakeup wieder freigegeben werden.
3. Vor dem Zusammenführen prüfen:

   ```bash
   strings g15netspeed | rg -i 'nvidia|gpu|popen|pclose'
   ```

   Ein Treffer ist nicht automatisch falsch, muss aber erklärt und getestet
   werden. Das aktuelle Binary enthält absichtlich keine solchen Abfragen.
4. Einen längeren Standby-Test durchführen; ein einzelner schneller Aus-/An-
   Zyklus reicht nicht als Nachweis.

Die ausführliche Untersuchung steht in
`/home/micha/Codex/status-grafik-deadlock-2026-09-13.md`.
