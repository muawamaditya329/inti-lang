#!/usr/bin/env bash
# audit_native.sh — audit freestanding Fase 2: binary Inti native TANPA libc.
# Bukti nyata (bukan klaim):
#   1. ldd        -> "not a dynamic executable" (tidak menaut libc.so)
#   2. readelf -l -> tidak ada segmen INTERP (tanpa loader dinamis)
#   3. readelf -d -> tidak ada segmen DYNAMIC
#   4. file       -> statically linked
#   5. syscall nyata tetap bekerja: echo (read/write) + time (clock_gettime)
#   6. kesalahan runtime tetap tertangani runtime mesin sendiri (alloc penuh)
cd "$(dirname "$0")/.." || exit 1
INTICC=./inti-cc
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0
cek() { # $1 nama uji, $2 kondisi (0=ok)
    if [ "$2" -eq 0 ]; then echo "LULUS  $1"; else echo "GAGAL  $1"; fail=1; fi
}

"$INTICC" examples/echo.inti -o "$TMP/echo" || { echo "kompilasi gagal"; exit 1; }
"$INTICC" examples/time.inti -o "$TMP/time" || { echo "kompilasi gagal"; exit 1; }
"$INTICC" examples/memory.inti -o "$TMP/mem" || { echo "kompilasi gagal"; exit 1; }

echo "=== Ukuran binary native ==="
stat -c '%n %s byte' "$TMP/echo" "$TMP/time" "$TMP/mem"
echo

# 1. ldd
ldd "$TMP/echo" >"$TMP/ldd.out" 2>&1
grep -q "not a dynamic executable" "$TMP/ldd.out"
cek "ldd: not a dynamic executable" $?

# 2. readelf -l: tanpa INTERP
readelf -l "$TMP/echo" 2>/dev/null | grep -q INTERP
[ $? -ne 0 ]; cek "readelf -l: tanpa segmen INTERP" $?

# 3. readelf -d: tanpa DYNAMIC
readelf -d "$TMP/echo" 2>/dev/null | grep -q DYNAMIC
[ $? -ne 0 ]; cek "readelf -d: tanpa segmen DYNAMIC" $?

# 4. file: statically linked
file "$TMP/echo" | grep -q "statically linked"
cek "file: statically linked" $?

# 5. syscall nyata
printf "halo dunia inti\n" | "$TMP/echo" > "$TMP/echo.out"
printf "halo dunia inti\n" > "$TMP/echo.exp"
diff -q "$TMP/echo.out" "$TMP/echo.exp" >/dev/null
cek "syscall read/write nyata (echo)" $?

"$TMP/time" > "$TMP/time.out"
awk 'NF==3 && $1>1000000000 && $2>=0 && $2<1000000 && $3>0' "$TMP/time.out" >/dev/null
cek "syscall clock_gettime+getpid nyata (time)" $?

# 6. runtime alokator mesin: memori penuh harus tertangani runtime sendiri
printf 'set p, 0\nalloc p, 999999999999\n' > "$TMP/full.inti"
"$INTICC" "$TMP/full.inti" -o "$TMP/full" 2>/dev/null
"$TMP/full" >"$TMP/full.out" 2>"$TMP/full.err"; c=$?
grep -q "arena penuh" "$TMP/full.err" && [ "$c" -eq 1 ]
cek "runtime mesin: arena penuh tertangani (exit $c)" $?

# 7. pembagian nol pada binary native
printf 'set a, 5\nset b, 0\ndiv c, a, b\n' > "$TMP/dz.inti"
"$INTICC" "$TMP/dz.inti" -o "$TMP/dz" 2>/dev/null
"$TMP/dz" >"$TMP/dz.out" 2>"$TMP/dz.err"; c=$?
grep -q "pembagian dengan nol" "$TMP/dz.err" && [ "$c" -eq 1 ]
cek "runtime mesin: div-nol tertangani (exit $c)" $?

echo
[ "$fail" -eq 0 ] && echo "AUDIT FREESTANDING: SEMUA LULUS — binary Inti native berjalan tanpa libc" \
                  || echo "AUDIT FREESTANDING: ADA GAGAL"
exit $fail
