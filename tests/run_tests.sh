#!/usr/bin/env bash
# Runs savebackup on the PC against a fake memory card, using the small stand-ins for the
# DS libraries in tests/stub. Needs bash and a C compiler; BlocksDS is not needed.
#
#   bash tests/run_tests.sh          (CC=clang bash tests/run_tests.sh for another compiler)
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

prog="$work/savebackup"
"${CC:-cc}" -std=gnu11 -O1 -Wall -Wextra -I "$here/stub" -o "$prog" \
    "$root/source/main.c" "$here/stub/stubs.c" || exit 1

# The ':' makes the paths look like "fat:/..." to the program, as on a DS.
card="$work/card:"
out="$work/out.txt"
failures=0

# run <folder the flashcart reports> [noargv]
# "noargv" starts the program with an empty argv[0], like a flashcart that passes no argv.
run() {
    if [ "${2:-}" = noargv ]; then
        SB_CWD="$1" SB_DRIVE="$card/" bash -c 'exec -a "" "$0"' "$prog" >"$out" 2>&1
    else
        SB_CWD="$1" SB_DRIVE="$card/" "$prog" >"$out" 2>&1
    fi
    status=$?
}

expect() {
    local name=$1
    shift
    if "$@"; then
        printf 'ok    %s\n' "$name"
    else
        printf 'FAIL  %s\n' "$name"
        sed 's/^/      | /' "$out"
        failures=$((failures + 1))
    fi
}

said()      { grep -qF -- "$1" "$out"; }
exists()    { [ -e "$1" ]; }
missing()   { [ ! -e "$1" ]; }
sets()      { find "$card/backups/$1" -mindepth 1 -maxdepth 1 -type d -name '[0-9][0-9][0-9][0-9]_*' 2>/dev/null | wc -l; }
set_count() { [ "$(sets "$1")" -eq "$2" ]; }
newest()    { find "$card/backups/$1" -mindepth 1 -maxdepth 1 -type d -name '[0-9]*' ! -name '*_INCOMPLETE' 2>/dev/null | sort | tail -n 1; }
oldest_is() { [ "$(find "$card/backups/$1" -mindepth 1 -maxdepth 1 -type d -name '[0-9]*' | sort | head -n 1 | xargs basename | cut -c1-4)" = "$2" ]; }
ok_exit()   { [ "$status" -eq 0 ]; }
touch_file() { mkdir -p "$(dirname "$1")" && printf '%s\n' "${2:-$1}" >"$1"; }

# A game folder with an emulator, ROMs, saves, settings and things that must be skipped.
games="$card/games-gb"
for f in savebackup.nds lameboy.nds Game.gb Game.gbc Game.gba Game.sav Game.ls1 \
         settings.ini savebackup.sav .hidden sub/inner.sav; do
    touch_file "$games/$f"
done
touch_file "$card/_DS_MENU.DAT"

echo "Started from a folder the flashcart reports:"
run "$games/"
set1=$(newest games-gb)
expect "exits normally"                    ok_exit
expect "makes backup 0001"                 set_count games-gb 1
expect "copies the saves"                  exists "$set1/Game.sav"
expect "copies the savestate"              exists "$set1/Game.ls1"
expect "copies other files"                exists "$set1/settings.ini"
expect "skips .gb ROMs"                    missing "$set1/Game.gb"
expect "skips .gbc ROMs"                   missing "$set1/Game.gbc"
expect "skips .gba ROMs"                   missing "$set1/Game.gba"
expect "skips .nds programs"               missing "$set1/lameboy.nds"
expect "skips its own files"               missing "$set1/savebackup.sav"
expect "skips hidden files"                missing "$set1/.hidden"
expect "skips subfolders"                  missing "$set1/sub"
expect "copies are identical"              cmp -s "$games/Game.sav" "$set1/Game.sav"
expect "waits for A, then switches off"    said "Press A to switch off."

echo "Started again without changes:"
run "$games/"
expect "makes no new backup"               set_count games-gb 1
expect "says so"                           said "No changes since backup 0001"

echo "Started after a save changed:"
touch_file "$games/Game.sav" "new save"
run "$games/"
expect "makes backup 0002"                 set_count games-gb 2
expect "backup 0002 has the new save"      cmp -s "$games/Game.sav" "$(newest games-gb)/Game.sav"

echo "Flashcart passes no argv (like the original R4):"
for d in a/saves b/saves; do
    touch_file "$card/$d/savebackup.nds"
    touch_file "$card/$d/Game.sav" "save in $d"
done
touch_file "$card/c/saves/Game.sav"   # no copy of the program here
run "$card/" noargv
expect "searches the card"                 said "Looking for savebackup.nds"
expect "finds all 3 copies"                said "Found it in 3 folders"
expect "backs up a/saves"                  set_count a/saves 1
expect "backs up b/saves separately"       cmp -s "$card/b/saves/Game.sav" "$(newest b/saves)/Game.sav"
expect "leaves folders without it alone"   missing "$card/backups/c"
expect "leaves the card root alone"        missing "$card/backups/root"
expect "games-gb is unchanged"             set_count games-gb 2

echo "Old backups:"
for i in $(seq 1 20); do
    touch_file "$games/Game.sav" "save $i"
    run "$games/"
done
expect "keeps the newest 20"               set_count games-gb 20
expect "deleted the oldest two"            oldest_is games-gb 0003

if [ "$(id -u)" -ne 0 ]; then
    echo "A file can't be read:"
    touch_file "$games/Game.sav" "one more save"
    chmod 000 "$games/Game.ls1"
    run "$games/"
    chmod 644 "$games/Game.ls1"
    expect "reports the failure"           said "FAILED"
    expect "marks the backup INCOMPLETE"   exists "$(find "$card/backups/games-gb" -maxdepth 1 -name '*_INCOMPLETE' | head -n 1)"
    expect "deletes no old backups"        oldest_is games-gb 0003
else
    echo "(skipping the unreadable-file test: running as root)"
fi

echo "No copy of the program on the card:"
find "$card" -name savebackup.nds -delete
run "$card/" noargv
expect "says it can't find itself"         said "Can't find savebackup.nds"
expect "still switches off"                said "<switched off>"

echo
if [ "$failures" -eq 0 ]; then
    echo "All tests passed."
else
    echo "$failures test(s) FAILED."
    exit 1
fi
