#!/usr/bin/env bash
# run_bench_native.sh — benchmark compiler native vs interpreter vs C vs Python
# Metode sama dengan run_bench.sh: best-of-N waktu dinding (fork+wait4),
# puncak RSS (KB). Program Inti yang SAMA dijalankan lewat dua backend.
cd "$(dirname "$0")/.." || exit 1
[ -x ./inti ] && [ -x ./inti-cc ] || { echo "jalankan 'make' dulu"; exit 1; }

cc -O2 -o bench/sum_c bench/sum.c
cc -O2 -o bench/fib_c bench/fib.c
cc -O2 -o bench/mem_c bench/mem.c

for p in sum fib mem; do ./inti-cc bench/$p.inti -o bench/${p}_native || exit 1; done

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
    printf "| %s | %.4f | %s | %s |\n" "$label" "$w" "$r" "$o"
}

echo "Mesin: $(uname -m), $(nproc) inti CPU, kernel $(uname -r)"
echo "Metode: best-of-N, waktu dinding detik; RSS = puncak memori proses (KB)"
echo "Ukuran: runtime inti = $(stat -c %s ./inti) B; binary native = $(stat -c %s bench/sum_native) B; sum_c = $(stat -c %s bench/sum_c) B"
echo
echo "| Program | Waktu (s) | RSS (KB) | Keluaran |"
echo "|---|---|---|---|"
baris "sum 100M  C gcc -O2"        3 bench/sum_c
baris "sum 100M  Inti NATIVE"      3 bench/sum_native
baris "sum 100M  Inti interpreter" 3 ./inti bench/sum.inti
baris "sum 100M  Python 3.12"      1 python3 bench/sum.py
baris "fib27     C gcc -O2"        3 bench/fib_c
baris "fib27     Inti NATIVE"      3 bench/fib_native
baris "fib27     Inti interpreter" 3 ./inti bench/fib.inti
baris "fib27     Python 3.12"      1 python3 bench/fib.py
baris "mem 4MiB  C gcc -O2"        3 bench/mem_c
baris "mem 4MiB  Inti NATIVE"      3 bench/mem_native
baris "mem 4MiB  Inti interpreter" 3 ./inti bench/mem.inti
baris "mem 4MiB  Python 3.12"      1 python3 bench/mem.py
printf 'exit 0\n' > /tmp/kosong.inti
./inti-cc /tmp/kosong.inti -o /tmp/kosong_bin
baris "startup   Inti NATIVE"      10 /tmp/kosong_bin
baris "startup   Inti interpreter" 10 ./inti /tmp/kosong.inti
baris "startup   Python (pass)"    10 python3 -c pass
rm -f /tmp/kosong.inti /tmp/kosong_bin
