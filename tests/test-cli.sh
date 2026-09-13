#!/bin/sh
set -eu

program=${1:-./g15netspeed}
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

"$program" --help >"$test_dir/help"
grep -q -- '--interface' "$test_dir/help"
grep -q -- '--refresh' "$test_dir/help"
grep -q -- '--list-interfaces' "$test_dir/help"

"$program" --list-interfaces >"$test_dir/interfaces"
test -s "$test_dir/interfaces"

if "$program" --refresh 10 >"$test_dir/invalid-refresh" 2>&1; then
    echo "Fehler: Ungültige Aktualisierungsrate wurde akzeptiert." >&2
    exit 1
else
    status=$?
    test "$status" -eq 2
fi

if "$program" eth0 wlan0 >"$test_dir/too-many-arguments" 2>&1; then
    echo "Fehler: Zu viele Positionsargumente wurden akzeptiert." >&2
    exit 1
else
    status=$?
    test "$status" -eq 2
fi

if strings "$program" | grep -Eiq 'nvidia-smi|popen|pclose|nvml'; then
    echo "Fehler: Die Binärdatei enthält eine unerwünschte GPU-Abfrage." >&2
    exit 1
fi

echo "CLI-Tests erfolgreich."
