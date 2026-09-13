# Mitwirken

## Lokale Prüfung

Vor jedem Commit:

```bash
make clean
make test CFLAGS='-Wall -Wextra -Wpedantic -Werror -O2'
git diff --check
```

Falls `clang-tidy` installiert ist:

```bash
clang-tidy src/*.c tests/test-core.c -- \
  -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude
```

Änderungen am Display oder an Tasten müssen zusätzlich auf einer Logitech G15
geprüft und im Pull Request als Hardwaretest dokumentiert werden.

## Commits

- kurze, aussagekräftige Betreffzeile im Imperativ
- zusammengehörige Änderungen in einem Commit
- Commit-Text mit Zweck, wesentlichen Änderungen und ausgeführten Tests
- Dokumentation im selben Commit aktualisieren

## Pull Requests

Ein Pull Request beschreibt:

1. Ausgangslage und Ziel
2. technische Änderungen
3. Sicherheitsauswirkungen
4. ausgeführte automatisierte Tests
5. Ergebnis des Hardwaretests oder dessen noch offenen Status

## NVIDIA-/GPU-Regel

`g15netspeed` darf im normalen Betrieb weder `nvidia-smi` ausführen noch NVML
laden. Änderungen an dieser Grenze benötigen vorab die in
[docs/DPMS-NVIDIA-HINWEIS.md](docs/DPMS-NVIDIA-HINWEIS.md) beschriebenen
DPMS-Tests und eine ausdrückliche Freigabe.
