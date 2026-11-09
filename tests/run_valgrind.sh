#!/bin/bash
# 
# run_valgrind.sh - memory checks for mycp with Valgrind
#
# Usage:   make valgrind          (or: bash tests/run_valgrind.sh)
# Extra:   HELGRIND=1 bash tests/run_valgrind.sh   also checks threads for data races
#
# Valgrind options used:
#   --leak-check=full --show-leak-kinds=all   report every kind of leak
#   --track-origins=yes                       show where bad values came from
#   --trace-children=yes                      follow child processes too
#   --error-exitcode=1                        make any error fail the run
#
cd "$(dirname "$0")/.." || exit 2

command -v valgrind >/dev/null || { echo "valgrind not installed: sudo apt-get install -y valgrind"; exit 2; }
[ -x ./mycp ] || { echo "run 'make' first"; exit 2; }

VG="valgrind -q --leak-check=full --show-leak-kinds=all --track-origins=yes --trace-children=yes --error-exitcode=1"

WORK="$(mktemp -d /tmp/mycp_vg_XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
head -c 200000 /dev/urandom > "$WORK/big.bin"
printf 'hello\nworld\n' > "$WORK/note.txt"
mkdir -p "$WORK/tree/sub"
echo one > "$WORK/tree/a.txt"
head -c 30000 /dev/urandom > "$WORK/tree/sub/b.bin"
head -c 80000 "$WORK/big.bin" > "$WORK/part.bin"

pass=0
fail=0

# run_vg <name> <command...>
run_vg() {
    local name=$1
    shift
    if $VG "$@" >"$WORK/stdout.txt" 2>"$WORK/stderr.txt"; then
        pass=$((pass + 1))
        echo "[ OK ] $name"
    else
        fail=$((fail + 1))
        echo "[FAIL] $name"
        sed 's/^/        /' "$WORK/stderr.txt" | head -40
    fi
}

echo "== valgrind: mycp command line =="
run_vg "basic copy"                  ./mycp "$WORK/big.bin" "$WORK/o1.bin"
run_vg "small buffer, info, verify"  ./mycp -b 7 -i -c "$WORK/note.txt" "$WORK/o2.txt"
run_vg "progress + benchmark"        ./mycp -p -t "$WORK/big.bin" "$WORK/o3.bin"
run_vg "resume"                      ./mycp -R -c "$WORK/big.bin" "$WORK/part.bin"
run_vg "parallel copy (4 threads)"   ./mycp -j 4 -c "$WORK/big.bin" "$WORK/o4.bin"
run_vg "recursive copy"              ./mycp -r -c "$WORK/tree" "$WORK/tree2"
run_vg "recursive + sync (skip all)" ./mycp -r -s "$WORK/tree" "$WORK/tree2"
run_vg "recursive + threads"         ./mycp -r -j 3 "$WORK/tree" "$WORK/tree3"
run_vg "with log file"               ./mycp -l "$WORK/m.log" "$WORK/note.txt" "$WORK/o5.txt"

echo "== valgrind: unit tests =="
if [ -x tests/test_mycp ]; then
    run_vg "C unit tests (all 43)"   ./tests/test_mycp
else
    echo "[SKIP] tests/test_mycp not built (run: make tests/test_mycp)"
fi

if [ -n "$HELGRIND" ]; then
    echo "== helgrind: data-race check on the parallel copy =="
    if valgrind -q --tool=helgrind --error-exitcode=1 ./mycp -j 4 -c "$WORK/big.bin" "$WORK/h.bin" \
            >/dev/null 2>"$WORK/helgrind.txt"; then
        pass=$((pass + 1)); echo "[ OK ] helgrind: parallel copy"
    else
        fail=$((fail + 1)); echo "[FAIL] helgrind: parallel copy"; head -40 "$WORK/helgrind.txt"
    fi
fi

echo
echo "Valgrind runs: $pass clean, $fail with errors"
[ "$fail" -eq 0 ]
