#!/usr/bin/env bash
# test_qemu.sh — boot nyata IntiOS di QEMU + verifikasi output serial
#
# Semua asersi diuji terhadap log serial NYATA hasil boot QEMU.
# Paritas program diuji byte-per-byte: output kernel vs interpreter Inti.
# QEMU keluar via ACPI S5 (port 0x604) -> exit code 0.
set -uo pipefail
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)

QEMU=${QEMU:-qemu-system-x86_64}
LOG=build/serial.log
ERR=build/qemu.err

if ! command -v "$QEMU" >/dev/null; then
    echo "SKIP: $QEMU tidak tersedia"; exit 77
fi
[ -f intios.img ] || { echo "GAGAL: intios.img tidak ada — jalankan build.sh"; exit 1; }

# ---------------- input serial (deterministik) ----------------
# '\n' awal: SeaBIOS POST memakai COM1 untuk tes loopback dan bisa
# menghabiskan byte pertama yang tertahan di FIFO — baris kosong ini
# opsional bagi shell (diabaikan), jadi sisa input tetap utuh.
printf '\nhelp\nlist\nrun hello\nrun primes\nrun fib\nrun fizzbuzz\nrun memory\nrun echo\nsalam dari serial Inti\nbaris kedua\n\x04run tidakada\nmem\nperintah-lama\nshutdown\n' > build/input.txt

echo "== boot QEMU (timeout 120s, maks 3 percobaan) =="
# Catatan: QEMU 8.x MTTCG punya race internal yang sesekali membuat QEMU
# abort dgn asersi qemu_mutex_lock_iothread_impl (bug QEMU, bukan guest).
# Mitigasi: thread=single + retry hanya jika pesan asersi itu muncul.
boots=0
while [ "$boots" -lt 3 ]; do
    boots=$((boots+1))
    timeout 120 "$QEMU" -m 256 -M pc -accel tcg,thread=single \
        -drive file=intios.img,format=raw,if=ide \
        -boot c -display none -monitor none -serial stdio -no-reboot \
        -d cpu_reset \
        < build/input.txt > "$LOG" 2> "$ERR"
    RC=$?
    echo "qemu exit code: $RC (percobaan $boots)"
    if [ "$RC" -ne 134 ] || ! grep -q "qemu_mutex_lock_iothread_impl" "$ERR" 2>/dev/null; then
        break                                     # bukan race MTTCG: jangan diulang
    fi
    echo "  QEMU kena bug race MTTCG (asersi BQL) — dicoba lagi..."
done

echo "== qemu.err (40 baris terakhir) =="
tail -40 "$ERR" 2>/dev/null || true

echo "== ringkasan log serial (30 baris akhir) =="
tail -30 "$LOG"

PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "  [OK]   $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  [GAGAL] $1"; }
check(){ grep -qF "$2" "$LOG" && ok "$1" || bad "$1"; }

echo "== asersi boot =="
check "marker boot sector (uart+disk+8 potongan+pmode+salin+halaman+LME+PAE+PG+long)" "01234567kpctmagl5"
check "kernel kmain jalan"                             "[intios] kmain"
check "GDT/TSS/IDT/MSR terpasang"                      "[intios] GDT/TSS/IDT terpasang"
check "banner IntiOS"                                  "IntiOS 0.1.0"
check "prompt shell"                                   "inti> "

echo "== asersi program =="
extract() {  # extract <nama> -> build/out_<nama>.txt
    sed -n "/=== $1: mulai/,/=== $1: selesai/p" "$LOG" | sed '1d;$d' > "build/out_$1.txt"
}
for P in hello primes fib fizzbuzz memory echo; do extract "$P"; done

# normalisasi: buang baris kosong di AKHIR file (shell menambah \n sebelum
# marker "selesai" — bukan bagian output program)
norm() { awk '{l[NR]=$0} END{while(NR>0 && l[NR]=="") NR--; for(i=1;i<=NR;i++) print l[i]}' "$1"; }

# paritas vs interpreter (sumber kebenaran semantik)
"$ROOT/inti" "$ROOT/examples/hello.inti"    > build/exp_hello.txt    2>/dev/null
"$ROOT/inti" "$ROOT/examples/primes.inti"   > build/exp_primes.txt   2>/dev/null
"$ROOT/inti" "$ROOT/examples/fib.inti"      > build/exp_fib.txt      2>/dev/null
"$ROOT/inti" "$ROOT/examples/fizzbuzz.inti" > build/exp_fizzbuzz.txt 2>/dev/null
"$ROOT/inti" "$ROOT/examples/memory.inti"   > build/exp_memory.txt   2>/dev/null
printf 'salam dari serial Inti\nbaris kedua\n' | "$ROOT/inti" "$ROOT/examples/echo.inti" > build/exp_echo.txt 2>/dev/null
for P in hello primes fib fizzbuzz memory echo; do
    if diff <(norm "build/exp_$P.txt") <(norm "build/out_$P.txt") >/dev/null 2>&1; then
        ok "paritas '$P' identik byte-per-byte dengan interpreter"
    else
        bad "paritas '$P' BERBEDA"
        echo "     --- harapan ---";  head -5 "build/exp_$P.txt"  | sed 's/^/     /'
        echo "     --- nyata ---";    head -5 "build/out_$P.txt"  | sed 's/^/     /'
    fi
done

check "6 program selesai exit=0"       "=== echo: selesai (exit=0) ==="
NEXIT=$(grep -c "selesai (exit=0)" "$LOG" || true)
[ "$NEXIT" -ge 6 ] && ok "jumlah exit=0: $NEXIT" || bad "jumlah exit=0 hanya $NEXIT"

check "program tidak dikenal ditolak"  "tidak ada program 'tidakada'"
check "perintah asing ditolak"         "perintah tidak dikenal: 'perintah-lama'"
check "peta memori tampil"             "0x01000000 (32 MB)"
check "echo EOF (Ctrl-D) bekerja"      "baris kedua"

echo "== asersi shutdown =="
check "marker ACPI S5"                 "mematikan mesin (ACPI S5"
if [ "$RC" -eq 0 ]; then ok "QEMU keluar sendiri via ACPI (exit=0)"
else bad "QEMU tidak keluar via ACPI (exit=$RC, harap 0)"; fi

echo ""
echo "Hasil IntiOS QEMU: $PASS lulus, $FAIL gagal"
if [ "$FAIL" -eq 0 ] && [ "$PASS" -ge 12 ]; then
    echo "BOOT NYATA DI QEMU: SEMUA ASERSI LULUS"
    exit 0
fi
exit 1
