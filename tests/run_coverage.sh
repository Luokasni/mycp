#!/bin/bash
# 
# run_coverage.sh - code coverage of mycp using gcov (+ lcov for an HTML report)
#
# Usage:   make coverage          (or: bash tests/run_coverage.sh)
# Needs:   gcc, g++, libgtest-dev   (lcov is optional: sudo apt-get install -y lcov)
#
# It builds everything with --coverage in ./cov, runs
#   1. the C unit tests      (tests/test_mycp.c)
#   2. the Google Test suite (tests/gtest_mycp.cpp)
#   3. the command-line tests (tests/test_cli.sh)
# and prints the percentage of lines and branches executed per source file.
# HTML report (if lcov is installed): cov/html/index.html
#
cd "$(dirname "$0")/.." || exit 2

OUT=cov
LIBS="copy parallel dir queue utils log"
SRCS="main.c copy.c parallel.c dir.c queue.c utils.c log.c"
CF="-std=gnu11 -Wall -Wextra -pthread -O0 -g --coverage -I."

rm -rf "$OUT" ./*.gcda ./*.gcno ./*.gcov
mkdir -p "$OUT/obj"

echo "== building with coverage =="
for m in main $LIBS; do
    gcc $CF -c "$m.c" -o "$OUT/obj/$m.o" || exit 1
done
LIBOBJS=""
for m in $LIBS; do LIBOBJS="$LIBOBJS $OUT/obj/$m.o"; done

gcc $CF -o "$OUT/mycp_cov" "$OUT/obj/main.o" $LIBOBJS -pthread || exit 1
gcc $CF -o "$OUT/test_c" tests/test_mycp.c $LIBOBJS -pthread || exit 1

HAVE_GTEST=1
if ! g++ -std=c++17 -pthread -O0 -g --coverage -I. -o "$OUT/test_gtest" \
        tests/gtest_mycp.cpp $LIBOBJS -lgtest_main -lgtest 2>"$OUT/gtest_build.log"; then
    HAVE_GTEST=0
    echo "(Google Test not available - skipping it: sudo apt-get install -y libgtest-dev)"
fi

echo "== running tests =="
"$OUT/test_c" | tail -1
if [ "$HAVE_GTEST" -eq 1 ]; then
    "$OUT/test_gtest" >"$OUT/gtest.log" 2>&1
    grep -E "PASSED|FAILED" "$OUT/gtest.log"
fi
MYCP="$OUT/mycp_cov" bash tests/test_cli.sh | tail -1

echo
echo "== coverage per file (lines / branches) =="
printf "%-12s %12s %12s\n" "file" "lines" "branches"
total_l=0; total_e=0
for f in $SRCS; do
    base="${f%.c}"
    res=$(gcov -b -n -o "$OUT/obj" "$f" 2>/dev/null)
    lines=$(grep -A1 "File '$f'" <<<"$res" | grep -o "Lines executed:[0-9.]*% of [0-9]*" | head -1)
    br=$(grep -A3 "File '$f'" <<<"$res" | grep -o "Branches executed:[0-9.]*%" | head -1)
    pct=$(grep -o "[0-9.]*%" <<<"$lines" | head -1)
    printf "%-12s %12s %12s\n" "$f" "${pct:-n/a}" "$(grep -o '[0-9.]*%' <<<"$br" | head -1)"
done

if command -v lcov >/dev/null && command -v genhtml >/dev/null; then
    RC="--rc branch_coverage=1"
    IGN="--ignore-errors mismatch,inconsistent,unused,gcov,negative,source"
    lcov -q --capture --directory "$OUT/obj" --output-file "$OUT/all.info" $RC $IGN >/dev/null 2>&1
    EXTRACT=""
    for f in $SRCS; do EXTRACT="$EXTRACT $PWD/$f"; done
    lcov -q --extract "$OUT/all.info" $EXTRACT --output-file "$OUT/coverage.info" $RC $IGN >/dev/null 2>&1
    genhtml -q "$OUT/coverage.info" --output-directory "$OUT/html" $RC $IGN >/dev/null 2>&1
    echo
    lcov --summary "$OUT/coverage.info" $RC $IGN 2>&1 | grep -E "lines|branches|functions"
    echo "HTML report: $OUT/html/index.html"
else
    echo
    echo "(install lcov for an HTML report: sudo apt-get install -y lcov)"
fi
