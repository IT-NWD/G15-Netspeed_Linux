## Ziel

<!-- Welches Problem wird gelöst? -->

## Änderungen

<!-- Die wesentlichen technischen Änderungen als Liste. -->

## Tests

- [ ] `make clean`
- [ ] `make test CFLAGS='-Wall -Wextra -Wpedantic -Werror -O2'`
- [ ] `git diff --check`
- [ ] Binärscan auf `nvidia-smi`, `popen`, `pclose` und NVML
- [ ] Hardwaretest auf einer Logitech G15 oder Begründung, warum er offen ist

## Sicherheitsprüfung

- [ ] Keine NVIDIA-/GPU-Abfrage in den Hauptprozess eingeführt
- [ ] Fehler- und Wiederverbindungsverhalten geprüft

## Dokumentation

- [ ] README/Architekturdokumentation aktualisiert
- [ ] CHANGELOG aktualisiert
