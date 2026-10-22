#!/usr/bin/env bash
# build.sh — bangun image disk IntIOS dari nol
#   1. program .inti -> .ibin (inti-cc --target=intios, flat binary)
#   2. kernel C + asm (gcc/gas/ld/objcopy — tanpa GRUB, tanpa linker script ELF user)
#   3. boot sector 512B + image disk 256KB
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)
mkdir -p build

KCFLAGS="-c -O2 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
-mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=small -Wall -Wextra"

echo "== [1/4] kompilasi program user -> .ibin =="
"$ROOT/inti-cc" --target=intios "$ROOT/examples/hello.inti"    -o build/hello.ibin
"$ROOT/inti-cc" --target=intios "$ROOT/examples/primes.inti"   -o build/primes.ibin
"$ROOT/inti-cc" --target=intios "$ROOT/examples/fib.inti"      -o build/fib.ibin
"$ROOT/inti-cc" --target=intios "$ROOT/examples/fizzbuzz.inti" -o build/fizzbuzz.ibin
"$ROOT/inti-cc" --target=intios "$ROOT/examples/memory.inti"   -o build/memory.ibin
"$ROOT/inti-cc" --target=intios "$ROOT/examples/echo.inti"     -o build/echo.ibin

echo "== [2/4] rakit kernel =="
gcc $KCFLAGS -o build/kmain.o   kmain.c
gcc $KCFLAGS -o build/console.o console.c
gcc $KCFLAGS -o build/input.o   input.c
gcc $KCFLAGS -o build/cpu.o     cpu.c
gcc $KCFLAGS -o build/sys.o     sys.c
gcc $KCFLAGS -o build/shell.o   shell.c
gcc -c -o build/kernel.o kernel.S
gcc -c -o build/sys_asm.o sys.S
gcc -c -o build/prog.o    prog.S

ld -T kernel.ld -nostdlib -o build/kernel.elf \
    build/kernel.o build/kmain.o build/console.o build/input.o \
    build/cpu.o build/sys.o build/shell.o build/sys_asm.o build/prog.o
objcopy -O binary build/kernel.elf build/kernel.bin
echo "   kernel.bin: $(stat -c%s build/kernel.bin) byte"

echo "== [3/4] rakit boot sector =="
gcc -c -o build/boot.o boot.S
ld -Ttext=0x7C00 -e _boot -nostdlib -o build/boot.elf build/boot.o
objcopy -O binary build/boot.elf build/boot.bin
SZ=$(stat -c%s build/boot.bin)
[ "$SZ" -eq 512 ] || { echo "GAGAL: boot.bin = $SZ byte (harus 512)"; exit 1; }
echo "   boot.bin: 512 byte, signature $(xxd -p -s 510 -l 2 build/boot.bin 2>/dev/null || od -An -tx1 -j 510 -N 2 build/boot.bin | tr -d ' ')"

echo "== [4/4] susun image disk =="
cat build/boot.bin build/kernel.bin > intios.img
truncate -s 262144 intios.img
echo "SELESAI: intios/intios.img ($(stat -c%s intios.img) byte)"
