# IntiOS — Mini-Kernel Bahasa Inti (Fase 3 ROADMAP-OS)

IntiOS adalah mini-kernel x86-64 yang **boot nyata dari BIOS** — bukan
simulasi, bukan program Linux. Image disk 256 KB berisi boot sector 512 byte,
kernel 64-bit, dan program-program Inti yang ter-embed. Diuji otomatis di
GitHub Actions dengan QEMU (`intios-test`), dan bisa dijalankan di mesin
x86-64 nyata melalui boot USB/disk (mode BIOS legacy).

```
make intios        # bangun intios/intios.img (butuh inti-cc, gcc, gas, ld)
make intios-test   # boot di QEMU + asersi serial + paritas byte-per-byte
```

## 1. Jalur boot nyata

| Tahap | Kode | Apa yang terjadi |
|---|---|---|
| 1 | BIOS (SeaBIOS) | Memuat sektor LBA-0 ke 0x7C00, lompat jika `0xAA55` |
| 2 | `boot.S` 16-bit | Init COM1 115200 8N1, marker serial `i` |
| 3 | `boot.S` | Baca 512 sektor via **int 13h AH=42h** (extended read LBA) ke 0x10000, marker `k` |
| 4 | `boot.S` | A20 (port 0x92), GDT di 0x500, **masuk protected mode**, marker `p` |
| 5 | `boot.S` 32-bit | Salin kernel 0x10000 → 0x100000 |
| 6 | `boot.S` | Paging: identity map 1 GB, halaman 2 MB (P\|RW\|**US**\|PS) |
| 7 | `boot.S` | `EFER.LME` + `CR0.PG\|PAE` → **long mode 64-bit**, marker `l` |
| 8 | `kernel.S` | Stack 0x200000, nolkan `.bss`, panggil `kmain()` |
| 9 | `kmain.c` | Konsol, GDT/TSS/IDT, MSR SYSCALL/SYSRET, shell |

Semua langkah diuji: marker `i k . . . . . . p l` tampil di log serial CI
sebelum banner kernel. Jika boot gagal di tengah, log berhenti di marker
terakhir yang tercapai — debugging nyata, bukan hitam-putam.

## 2. Peta memori (identity map, -m 256)

```
0x000000-0x000FFF  BIOS/IVT/BDA (mode nyata)
0x001000-0x003FFF  tabel halaman boot (PML4/PDPT/PD)
0x005000-0x0051F   GDT boot + GDTR
0x07C00-0x07DFF    boot sector
0x100000-0x11FFFF  kernel IntiOS (~32 KB)
0x1F0000-0x1FFFFF  kernel stack (kmain 0x200000 / RSP0 0x1F8000 / sys 0x1FF000)
0x400000-0x4FFFFF  program user (blob .ibin dimuat di sini, maks 1 MB)
0x0800000-0x0FFFFFF  stack user (8 MB, rsp = 0x1000000)
0x1000000-0x2FFFFFF  arena program user (32 MB, first-fit, sama dgn VM)
```

Semua halaman `P|RW|U/S|PS` — single address space, tanpa isolasi halaman
per-proses (dokumentasi jujur: itu pekerjaan Fase 4+). Proteksi program
dilakukan oleh **ring privilege**: kernel CPL0, program user CPL3.

## 3. SYSCALL/SYSRET nyata (nomor kompatibel Linux)

Program Inti hasil `inti-cc --target=intios` memanggil kernel dengan
instruksi `syscall` — persis seperti binary Fase 2 di Linux:

| Nomor | Nama | Perilaku IntiOS |
|---|---|---|
| 0 | read | fd 0: baris dari serial COM1 / keyboard PS/2 (blocking). EOF = Ctrl-D |
| 1 | write | fd 1/2 → konsol VGA + serial (byte-exact) |
| 60 | exit | kembali ke shell IntiOS (kode exit dilaporkan) |
| lain | — | `-ENOSYS` (-38), seperti Linux |

Mekanisme: `EFER.SCE=1`, `IA32_LSTAR=sys_entry`, `STAR=(0x1B<<48)|(0x08<<32)`,
`FMASK=0`. `sys_entry` (sys.S) menyimpan RSP/RCX/R11 user, pindah ke stack
kernel 0x1FF000, memanggil `sys_dispatch()` (C), lalu `sysretq` memulihkan
semuanya. RBX/RBP/R12–R15 dijaga penuh (ABI Inti menjadikan RBP frame
pointer).

## 4. Eksekusi program user (CPL3)

1. Shell menyalin blob `.ibin` ke 0x400000 (header 64 B + kode + segmen RW).
2. `enter_user()` (sys.S) mem-push frame iretq: CS=0x1B, RSP=0x1000000,
   RFLAGS=0x202, RIP=0x400000 → `iretq` masuk CPL3.
3. ABI program: **RDI = basis arena, RSI = ukuran arena** (kernel siapkan),
   stack user sudah terpasang, register lain dinolkan.
4. Program berjalan kode mesin yang sama dengan backend ELF; `exit(60)`
   membuat kernel `user_die()` → memulihkan frame shell → shell melanjutkan.
5. Exception (#DE #UD #GP #PF) ditangkap IDT → dilapor ke serial → kembali
   ke shell dengan exit code 128+vektor. Program rusak TIDAK membekukan OS.

## 5. Format blob program (.ibin)

```
+0x00  magic "INTIPRG1" (u64)   +0x08 entry_off=64 (u32)
+0x0C code_size (u32)           +0x10 data_off (u32, align 4096)
+0x14 data_size=64 (u32)        +0x18 arena_mb (u32)
+0x1C stack_mb=0 (u32, kernel)  +0x20..63 nol
+0x40 kode mesin + rodata + image data (RIP-relative, dipindah utuh)
+data_off 64 byte global RW (dinolkan; alamat disp32 absolut ter-patch)
```

Kernel menyalin seluruh blob ke 0x400000: kode di 0x400064, global RW di
0x400000+data_off. Karena codegen memakai akses global absolut (disp32) dan
string via `mov r32, imm32`, alamat dihitung dengan basis yang sama —
perilaku identik dengan ELF Fase 2.

## 6. Perbedaan jujur vs Linux / backend ELF

| Aspek | Backend ELF (Fase 1-2) | IntiOS (Fase 3) |
|---|---|---|
| mmap stack/arena | `SYS_mmap` nyata | disediakan kernel (fix 8 MB / 32 MB) |
| clock_gettime, open, dst | syscall Linux asli | `-ENOSYS` (belum ada VFS/timer) |
| read | byte-stream pipa | line-buffered konsol (serial/PS2) |
| Isolasi memori | ASLR+NX+paginasi kernel | tanpa paginasi per-proses (CPL3 saja) |
| Multi-tasking | penjadwal kernel Linux | single-task: shell menjalankan 1 program |

Yang diuji CI secara byte-per-byte: `hello`, `primes`, `fib`, `fizzbuzz`,
`memory`, `echo` — output IntiOS **identik** dengan interpreter `inti`.
`time.inti` sengaja tidak di-embed (butuh clock_gettime — masuk akal untuk
Fase 4 ketika timer PIT/HPET ditambahkan).

## 7. Berjalan di QEMU / mesin nyata

```bash
# QEMU (CI memakai ini):
qemu-system-x86_64 -m 256 -M pc -drive file=intios/intios.img,format=raw,if=ide \
    -display none -serial stdio        # atau -serial file:log.txt

# Mesin nyata (BIOS legacy): tulis image ke USB dan boot.
#   dd if=intios/intios.img of=/dev/sdX bs=512   (HATI-HATI: hapus disk!)
# Keyboard PS/2/USB-lama dipoll langsung port 0x60 — USB murni UEFI
# mungkin tidak bekerja (butuh driver USB -> Fase 4).
```

Shell: `help`, `list`, `run <nama>`, `mem`, `clear`, `shutdown` (ACPI S5
port 0x604 — QEMU keluar dengan exit 0, diuji CI).
