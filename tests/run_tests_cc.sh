#!/usr/bin/env bash
# run_tests_cc.sh — test suite nyata untuk binary hasil COMPILER native
# Kasus yang SAMA dengan run_tests.sh, tapi: setiap .inti dikompilasi
# menjadi ELF native via ./inti-cc lalu dijalankan TANPA interpreter.
# Kasus error kompilasi: inti-cc harus gagal dengan fragmen stderr yang sama.
cd "$(dirname "$0")/.." || exit 1
INTICC=./inti-cc

if [ ! -x "$INTICC" ]; then echo "binary $INTICC tidak ada — jalankan 'make' dulu"; exit 1; fi

pass=0; fail=0; failed=""

for src in tests/cases/*.inti; do
    name=$(basename "$src" .inti)
    out_exp="tests/cases/$name.out"
    code_exp=0
    [ -f "tests/cases/$name.code" ] && code_exp=$(cat "tests/cases/$name.code")
    in_file="tests/cases/$name.in"
    stdin_src="/dev/null"
    [ -f "$in_file" ] && stdin_src="$in_file"
    errf=$(mktemp); binf=$(mktemp)

    if ! "$INTICC" "$src" -o "$binf" 2>"$errf"; then
        # ---- gagal kompilasi: hanya sah untuk kasus error kompilasi ----
        stderr=$(cat "$errf")
        if [ -f "$out_exp" ] || [ ! -f "tests/cases/$name.err" ]; then
            fail=$((fail+1)); failed="$failed $name"
            echo "GAGAL  $name — harusnya bisa dikompilasi; stderr: $stderr"
        else
            frag=$(cat "tests/cases/$name.err")
            case "$stderr" in
                *"$frag"*)
                    if [ -f "tests/cases/$name.code" ] && [ "$(cat tests/cases/$name.code)" != "1" ]; then
                        fail=$((fail+1)); failed="$failed $name"
                        echo "GAGAL  $name — exit kompilasi harus 1"
                    else
                        pass=$((pass+1)); echo "LULUS  $name (ditolak compiler, fragmen cocok)"
                    fi
                    ;;
                *) fail=$((fail+1)); failed="$failed $name"
                   echo "GAGAL  $name — fragmen '$frag' tidak ada; dapat: [$stderr]" ;;
            esac
        fi
        rm -f "$errf" "$binf"; continue
    fi
    rm -f "$errf"

    # ---- binary native dijalankan seperti interpreter test ----
    errf=$(mktemp)
    stdout=$("$binf" < "$stdin_src" 2>"$errf")
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
    rm -f "$binf"
done

echo
echo "Hasil (native via inti-cc): $pass lulus, $fail gagal (dari $((pass+fail)) test nyata)"
[ "$fail" -eq 0 ] || { echo "Gagal:$failed"; exit 1; }
