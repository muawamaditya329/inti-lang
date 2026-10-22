#!/usr/bin/env bash
# run_tests.sh — test suite nyata bahasa Inti
# Setiap kasus: jalankan program, bandingkan stdout, kode keluar,
# dan (opsional) potongan stderr. TIDAK ADA simulasi.
cd "$(dirname "$0")/.." || exit 1
INTI=./inti

if [ ! -x "$INTI" ]; then echo "binary $INTI tidak ada — jalankan 'make' dulu"; exit 1; fi

pass=0; fail=0; failed=""

for src in tests/cases/*.inti; do
    name=$(basename "$src" .inti)
    out_exp="tests/cases/$name.out"
    code_exp=0
    [ -f "tests/cases/$name.code" ] && code_exp=$(cat "tests/cases/$name.code")
    in_file="tests/cases/$name.in"
    stdin_src="/dev/null"
    [ -f "$in_file" ] && stdin_src="$in_file"

    errf=$(mktemp)
    stdout=$("$INTI" "$src" < "$stdin_src" 2>"$errf")
    code=$?
    stderr=$(cat "$errf"); rm -f "$errf"

    ok=1; why=""
    if [ "$code" -ne "$code_exp" ]; then
        ok=0; why="exit=$code (harap $code_exp); stderr: $stderr"
    fi
    if [ $ok -eq 1 ]; then
        if [ -f "$out_exp" ]; then
            exp=$(cat "$out_exp")
            if [ "$stdout" != "$exp" ]; then ok=0; why="stdout beda: dapat [$stdout] harap [$exp]"; fi
        elif [ -n "$stdout" ]; then
            ok=0; why="stdout tak diharapkan: [$stdout]"
        fi
    fi
    if [ $ok -eq 1 ] && [ -f "tests/cases/$name.err" ]; then
        frag=$(cat "tests/cases/$name.err")
        case "$stderr" in
            *"$frag"*) ;;
            *) ok=0; why="stderr tidak memuat '$frag'; dapat: [$stderr]" ;;
        esac
    fi

    if [ $ok -eq 1 ]; then
        pass=$((pass+1)); echo "LULUS  $name"
    else
        fail=$((fail+1)); failed="$failed $name"; echo "GAGAL  $name — $why"
    fi
done

echo
echo "Hasil: $pass lulus, $fail gagal (dari $((pass+fail)) test nyata)"
[ "$fail" -eq 0 ] || { echo "Gagal:$failed"; exit 1; }
