#!/bin/bash
#  
# test_cli.sh - end-to-end tests of the ./mycp command line
#
# Usage:  bash tests/test_cli.sh            (uses ./mycp)
#         MYCP=/path/to/mycp bash tests/test_cli.sh
#
MYCP="${MYCP:-./mycp}"
[ -x "$MYCP" ] || { echo "mycp binary not found: $MYCP (run 'make' first)"; exit 2; }
MYCP="$(realpath "$MYCP")" 

WORK="$(mktemp -d /tmp/mycp_cli_XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 2

pass=0
fail=0
out=""

ok()  { pass=$((pass + 1)); echo "[ OK ] $1"; }
bad() { fail=$((fail + 1)); echo "[FAIL] $1"; }

# expect_rc <code> <name> <command...>
expect_rc() {
    local want=$1 name=$2
    shift 2
    out=$("$@" 2>&1)
    local rc=$?
    if [ "$rc" -eq "$want" ]; then ok "$name"; else bad "$name (exit $rc, wanted $want)"; fi
}

# expect_out <name> <text> <command...>   (exit code must be 0)
expect_out() {
    local name=$1 text=$2
    shift 2
    out=$("$@" 2>&1)
    local rc=$?
    if [ "$rc" -eq 0 ] && grep -qF -- "$text" <<<"$out"; then ok "$name"; else bad "$name (exit $rc, missing '$text')"; fi
}

# same <name> <file1> <file2>
same() {
    if cmp -s "$2" "$3"; then ok "$1"; else bad "$1"; fi
}

# test data: random binary, a text file and a PNG-like file
head -c 300000 /dev/urandom > big.bin
printf 'hello\nworld\n' > note.txt
{ printf '\x89PNG\r\n\x1a\n'; head -c 50000 /dev/urandom; } > pic.png

echo "== argument errors =="
expect_rc 0 "help (-h)"                    "$MYCP" -h
expect_rc 1 "no arguments"                 "$MYCP"
expect_rc 1 "one argument only"            "$MYCP" big.bin
expect_rc 1 "unknown option"               "$MYCP" -z big.bin x
expect_rc 1 "bad buffer size (-b abc)"     "$MYCP" -b abc big.bin x
expect_rc 1 "bad buffer size (-b 0)"       "$MYCP" -b 0 big.bin x
expect_rc 1 "bad thread count (-j 0)"      "$MYCP" -j 0 big.bin x
expect_rc 1 "bad thread count (-j 65)"     "$MYCP" -j 65 big.bin x
expect_rc 1 "-j with -R is refused"        "$MYCP" -j 2 -R big.bin x
expect_rc 1 "missing source"               "$MYCP" nofile.bin x
expect_rc 1 "directory without -r"         "$MYCP" "$WORK" x
expect_rc 1 "copy file onto itself"        "$MYCP" note.txt note.txt

echo "== basic copy: text, binary, image =="
expect_rc 0 "copy binary"  "$MYCP" big.bin out.bin
same        "binary content identical" big.bin out.bin
expect_rc 0 "copy text"    "$MYCP" note.txt note_copy.txt
same        "text content identical" note.txt note_copy.txt
expect_rc 0 "copy image"   "$MYCP" pic.png pic_copy.png
same        "image content identical" pic.png pic_copy.png

echo "== options =="
expect_rc 0 "buffer size 1 byte (-b 1)"  "$MYCP" -b 1 note.txt b1.txt
same        "-b 1 content"  note.txt b1.txt
expect_rc 0 "buffer size 64 KB"          "$MYCP" -b 65536 big.bin b64.bin
same        "-b 65536 content" big.bin b64.bin
expect_out  "-i shows file info"   "Permissions" "$MYCP" -i note.txt i.txt
expect_out  "-p shows progress"    "100%"        "$MYCP" -p big.bin p.bin
expect_out  "-t shows speed"       "MB/s"        "$MYCP" -t big.bin t.bin
expect_out  "-c verifies"          "Verify OK"   "$MYCP" -c big.bin c.bin

chmod 640 big.bin
"$MYCP" big.bin perm.bin >/dev/null 2>&1
if [ "$(stat -c %a perm.bin)" = "640" ]; then ok "permissions preserved (640)"; else bad "permissions preserved (640)"; fi

mkdir -p dest
expect_rc 0 "copy into existing directory" "$MYCP" note.txt dest
same        "file landed as dest/note.txt" note.txt dest/note.txt

echo "== parallel (-j) =="
expect_out  "-j 4 with verify" "Verify OK" "$MYCP" -j 4 -c big.bin par.bin
same        "-j 4 content identical" big.bin par.bin
expect_rc 0 "-j 8 on a tiny file" "$MYCP" -j 8 note.txt par_tiny.txt
same        "-j 8 tiny content" note.txt par_tiny.txt

echo "== resume (-R) =="
head -c 100000 big.bin > part.bin
expect_out  "resume announces offset" "Resuming" "$MYCP" -R -c big.bin part.bin
same        "resumed file identical" big.bin part.bin

echo "== sync (-s) =="
expect_out  "-s skips an up-to-date file" "up to date" "$MYCP" -s big.bin out.bin
expect_rc 0 "-s copies a missing file" "$MYCP" -s note.txt brand_new.txt
same        "-s new file content" note.txt brand_new.txt

echo "== recursive (-r) =="
mkdir -p tree/sub/deep
echo "one"  > tree/a.txt
head -c 20000 /dev/urandom > tree/sub/b.bin
echo "deep" > tree/sub/deep/c.txt
expect_out  "-r copies a tree"       "Summary: 3 copied, 0 skipped, 0 failed" "$MYCP" -r -c tree tree2
if diff -r tree tree2 >/dev/null; then ok "tree identical"; else bad "tree identical"; fi
expect_out  "-r -s skips everything on 2nd run" "0 copied, 3 skipped" "$MYCP" -r -s tree tree2
expect_rc 0 "-r into itself terminates" "$MYCP" -r tree tree/inside
if [ ! -e tree/inside/inside ]; then ok "no endless nesting"; else bad "no endless nesting"; fi
expect_out  "-r with -j"  "Summary" "$MYCP" -r -j 3 -c tree tree3

echo "== logging (-l) =="
expect_rc 0 "-l writes a log" "$MYCP" -l mycp.log note.txt logged.txt
if grep -q "OK " mycp.log; then ok "log contains OK line"; else bad "log contains OK line"; fi

echo
echo "CLI tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
