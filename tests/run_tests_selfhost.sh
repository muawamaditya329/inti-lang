#!/usr/bin/env bash
# run_tests_selfhost.sh — uji self-hosting: intib (ditulis dlm Inti) mengompilasi
# Inti/0 -> .ibin, lalu .ibin dijalankan ibin-loader dan dibandingkan byte-per-byte
# dengan interpreter `inti` (oracle semantik), plus uji titik tetap (fixed point):
# intib mengompilasi intib.inti -> intib2.ibin; intib2 mengompilasi intib.inti ->
# intib3.ibin; intib2.ibin == intib3.ibin (byte-identik).
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
W=${TMPDIR:-/tmp}/selfhost-test.$$
mkdir -p "$W"; trap 'rm -rf "$W"' EXIT

FAIL=0; PASS=0
say() { printf '%s\n' "$*"; }

# ---- bangun toolchain uji ----
make -s inti >/dev/null 2>&1 || make inti >/dev/null
make -s inti-cc >/dev/null 2>&1 || make inti-cc >/dev/null
gcc -O2 -no-pie -Wl,-Ttext-segment=0x20000000 -o "$W/ibin-loader" tools/ibin-loader.c || exit 1
"$ROOT/inti-cc" selfhost/intib.inti -o "$W/intib" || { say "GAGAL: inti-cc intib"; exit 1; }

# ---- 1. paritas per kasus ----
for f in tests/cases0/*.inti; do
    n=$(basename "$f" .inti)
    # oracle: interpreter (stdin bila ada file .in)
    IN=/dev/null; [ -f "tests/cases0/$n.in" ] && IN="tests/cases0/$n.in"
    timeout 10 "$ROOT/inti" "$f" < "$IN" > "$W/exp.out" 2>/dev/null
    exp_code=$?
    # jalur self-hosted: intib kompilasi -> loader jalankan
    timeout 10 "$W/intib" < "$f" > "$W/$n.ibin" 2> "$W/$n.cerr"
    ccode=$?
    if [ $ccode -ne 0 ]; then
        say "GAGAL  $n (intib keluar $ccode)"; head -2 "$W/$n.cerr"; FAIL=$((FAIL+1)); continue
    fi
    if [ -f "tests/cases0/$n.code" ]; then
        # kasus kode keluar (termasuk error runtime): bandingkan exit saja
        timeout 10 "$W/ibin-loader" "$W/$n.ibin" < "$IN" > "$W/got.out" 2>/dev/null
        got_code=$?
        want=$(cat "tests/cases0/$n.code")
        if [ "$got_code" = "$want" ]; then PASS=$((PASS+1)); say "LULUS  $n (exit $want)"
        else say "GAGAL  $n (exit ingin $want, dapat $got_code)"; FAIL=$((FAIL+1)); fi
        continue
    fi
    timeout 10 "$W/ibin-loader" "$W/$n.ibin" < "$IN" > "$W/got.out" 2>"$W/$n.rerr"
    got_code=$?
    if [ $got_code -ne $exp_code ]; then
        say "GAGAL  $n (exit beda: ingin $exp_code, dapat $got_code)"; FAIL=$((FAIL+1)); continue
    fi
    if cmp -s "$W/exp.out" "$W/got.out"; then
        PASS=$((PASS+1)); say "LULUS  $n (paritas byte)"
    else
        say "GAGAL  $n (keluaran beda)"; diff "$W/exp.out" "$W/got.out" | head -4; FAIL=$((FAIL+1))
    fi
done

# ---- 2. titik tetap (fixed point) ----
say "== titik tetap: intib mengompilasi dirinya =="
timeout 60 "$W/intib" < selfhost/intib.inti > "$W/intib2.ibin" 2>"$W/fp1.err" || {
    say "GAGAL  intib2 (kompilasi tahap 1)"; head -3 "$W/fp1.err"; FAIL=$((FAIL+1)); fp=0; }
if [ "${fp:-1}" = "1" ]; then
    timeout 60 "$W/ibin-loader" "$W/intib2.ibin" < selfhost/intib.inti > "$W/intib3.ibin" 2>"$W/fp2.err" || {
        say "GAGAL  intib3 (kompilasi oleh intib2)"; head -3 "$W/fp2.err"; FAIL=$((FAIL+1)); fp=0; }
fi
if [ "${fp:-1}" = "1" ]; then
    if cmp -s "$W/intib2.ibin" "$W/intib3.ibin"; then
        PASS=$((PASS+1)); say "LULUS  fixed point (intib2.ibin == intib3.ibin, $(stat -c%s "$W/intib2.ibin") byte)"
    else
        say "GAGAL  fixed point: intib2 != intib3"; FAIL=$((FAIL+1)); fp=0
    fi
fi

# ---- 3. intib2 mengompilasi kasus lagi (paritas via compiler hasil fixed point) ----
if [ "${fp:-1}" = "1" ]; then
    n=arith
    timeout 10 "$W/ibin-loader" "$W/intib2.ibin" < "tests/cases0/$n.inti" > "$W/$n.ibin2" 2>/dev/null
    timeout 10 "$W/ibin-loader" "$W/$n.ibin2" > "$W/$n.out2" 2>/dev/null
    timeout 10 "$ROOT/inti" "tests/cases0/$n.inti" > "$W/$n.outi" 2>/dev/null
    if cmp -s "$W/$n.out2" "$W/$n.outi"; then
        PASS=$((PASS+1)); say "LULUS  intib2 kompilasi ulang $n (paritas byte)"
    else
        say "GAGAL  intib2 kompilasi ulang $n"; FAIL=$((FAIL+1))
    fi
fi

say "== hasil: $PASS lulus, $FAIL gagal =="
[ $FAIL -eq 0 ]
