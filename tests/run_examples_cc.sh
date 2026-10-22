#!/usr/bin/env bash
# run_examples_cc.sh — ekuivalensi nyata: output interpreter == output native
# Setiap contoh dikompilasi lalu dijalankan; hasil dibandingkan BYTE-per-BYTE
# dengan interpreter (Fase 0). time.inti berisi angka jam sehingga diverifikasi
# secara struktural (3 baris angka, detik masuk akal), bukan diff penuh.
cd "$(dirname "$0")/.." || exit 1
INTI=./inti
INTICC=./inti-cc
[ -x "$INTI" ] && [ -x "$INTICC" ] || { echo "jalankan 'make' dulu"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0

cc_run() { # $1 nama, $2 file stdin (opsional)
    local name=$1 stdin=${2:-/dev/null}
    "$INTICC" "examples/$name.inti" -o "$TMP/$name" 2>"$TMP/cc.err" || {
        echo "GAGAL  $name — kompilasi: $(cat "$TMP/cc.err")"; fail=$((fail+1)); return; }
    "$INTI"  "examples/$name.inti" <"$stdin" >"$TMP/$name.int.out" 2>/dev/null
    "$TMP/$name"                    <"$stdin" >"$TMP/$name.cc.out"  2>/dev/null
    local c1=$? c2=$?
    if [ $c1 -ne $c2 ]; then
        echo "GAGAL  $name — exit beda: interpreter=$c1 native=$c2"; fail=$((fail+1)); return
    fi
    if ! cmp -s "$TMP/$name.int.out" "$TMP/$name.cc.out"; then
        echo "GAGAL  $name — output beda"; diff "$TMP/$name.int.out" "$TMP/$name.cc.out" | head -4
        fail=$((fail+1)); return
    fi
    echo "LULUS  $name (output identik byte-per-byte, exit=$c1)"
    pass=$((pass+1))
}

cc_run hello
cc_run fib
cc_run primes
cc_run memory
cc_run fizzbuzz
# echo: input lewat FILE (bukan pipa) agar kedua backend membaca input yang sama
printf 'baris satu\nbaris dua\n' > "$TMP/echo.in"
cc_run echo "$TMP/echo.in"

# time.inti: nilai jam berubah — verifikasi struktural
"$INTICC" examples/time.inti -o "$TMP/time" 2>/dev/null && "$TMP/time" > "$TMP/time.out"
n=$($TMP/time 2>/dev/null | wc -l)
sec=$(head -1 "$TMP/time.out" 2>/dev/null)
pid=$(tail -1 "$TMP/time.out" 2>/dev/null)
if [ "$n" -eq 3 ] && [ "$sec" -gt 1700000000 ] 2>/dev/null && [ "$pid" -gt 1 ] 2>/dev/null; then
    echo "LULUS  time (3 baris: epoch=$sec, ms ok, pid=$pid — syscall nyata)"
    pass=$((pass+1))
else
    echo "GAGAL  time — output: $(cat "$TMP/time.out" 2>/dev/null | tr '\n' ' ')"
    fail=$((fail+1))
fi

# rekursi dalam 100rb level juga lewat binary native
"$INTICC" tests/cases/deep_rec.inti -o "$TMP/deep" 2>/dev/null && "$TMP/deep" > "$TMP/deep.out"
if [ "$(cat "$TMP/deep.out" 2>/dev/null)" = "5000050000" ]; then
    echo "LULUS  deep_rec native (100.000 level rekursi di stack mmap)"
    pass=$((pass+1))
else
    echo "GAGAL  deep_rec native — dapat: $(cat "$TMP/deep.out" 2>/dev/null)"
    fail=$((fail+1))
fi

echo
echo "Hasil: $pass lulus, $fail gagal (ekuivalensi interpreter vs native)"
[ "$fail" -eq 0 ] || exit 1
