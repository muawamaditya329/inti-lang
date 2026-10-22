#!/usr/bin/env bash
# run_bench.sh — benchmark nyata: Inti vs C vs Python
# Metode: best-of-N waktu dinding via fork+wait4; puncak RSS (KB) dari run terbaik.
cd "$(dirname "$0")/.." || exit 1
[ -x ./inti ] || { echo "binary ./inti tidak ada — jalankan 'make' dulu"; exit 1; }

cc -O2 -o bench/sum_c bench/sum.c
cc -O2 -o bench/fib_c bench/fib.c
cc -O2 -o bench/mem_c bench/mem.c

run_best() { # $1 = jumlah run, sisanya = perintah
    local n=$1; shift
    local best=999999 best_rss=0 out=""
    local of=$(mktemp) ef=$(mktemp)
    for _ in $(seq 1 "$n"); do
        python3 bench/measure.py "$@" >"$of" 2>"$ef" || { echo "GAGAL: $*" >&2; cat "$ef" >&2; rm -f "$of" "$ef"; exit 1; }
        local wall rss
        wall=$(awk '{print $1}' "$ef")
        rss=$(awk '{print $2}' "$ef")
        [ -z "$out" ] && out=$(tr -d '\n' <"$of")
        if awk -v a="$wall" -v b="$best" 'BEGIN{exit !(a<b)}'; then best=$wall; best_rss=$rss; fi
    done
    rm -f "$of" "$ef"
    echo "$best|$best_rss|$out"
}

baris() { # $1 label, $2 n_runs, sisanya perintah
    local label=$1 n=$2; shift 2
    local res
    res=$(run_best "$n" "$@") || return 1
    local w r o
    IFS='|' read -r w r o <<<"$res"
    printf "| %s | %.3f | %s | %s |\n" "$label" "$w" "$r" "$o"
}

echo "Mesin: $(uname -m), $(nproc) inti CPU, kernel $(uname -r)"
echo "Metode: best-of-N, waktu dinding detik; RSS = puncak memori proses (KB)"
echo "Ukuran runtime: inti = $(stat -c %s ./inti) byte, sum_c = $(stat -c %s bench/sum_c) byte"
echo
echo "| Program | Waktu (s) | RSS (KB) | Keluaran |"
echo "|---|---|---|---|"
baris "sum  C gcc -O2"        3 bench/sum_c
baris "sum  Inti"             3 ./inti bench/sum.inti
baris "sum  Python 3.12"      1 python3 bench/sum.py
baris "fib27 C gcc -O2"       3 bench/fib_c
baris "fib27 Inti"            3 ./inti bench/fib.inti
baris "fib27 Python 3.12"     1 python3 bench/fib.py
baris "mem4M C gcc -O2"       3 bench/mem_c
baris "mem4M Inti"            3 ./inti bench/mem.inti
baris "mem4M Python 3.12"     1 python3 bench/mem.py

# startup: program kosong
printf 'exit 0\n' > /tmp/kosong.inti
baris "startup Inti (exit 0)" 10 ./inti /tmp/kosong.inti
baris "startup Python (pass)" 10 python3 -c pass
rm -f /tmp/kosong.inti
