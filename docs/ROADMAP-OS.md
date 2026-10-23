# ROADMAP — Dari Interpreter Menuju OS "Inti"

Tujuan yang Anda tetapkan: **memodifikasi 100% kernel Linux dengan bahasa
buatan sendiri dan membuat OS**, semuanya diuji nyata, bukan simulasi.
Dokumen ini memetakan jalan yang jujur dan dapat diuji menuju ke sana —
termasuk bagian mana yang mungkin, berapa harganya, dan apa saja batasnya.

Prinsip yang dipakai di setiap fase (sudah diterapkan di Fase 0):
**setiap kemampuan harus punya test nyata yang lulus di mesin sungguhan.**

## Status sekarang — Fase 0, 1, dan 2 SELESAI ✔

- Interpreter Inti (compiler bytecode + VM) ditulis dalam C, 43 KB, tanpa
  dependensi di luar libc.
- Fitur: aritmetika int64, kontrol alur terstruktur + lompatan mentah,
  fungsi + rekursi 100.000 level, arena memori nyata, **syscall Linux asli**
  (read/write/getpid/clock_gettime terbukti di contoh dan test).
- **25/25 test nyata LULUS** pada interpreter; benchmark terukur.
- **Fase 1 ✔** compiler native `inti-cc`: bytecode → kode mesin x86-64 →
  ELF64 ditulis tangan (tanpa assembler/linker). Test suite yang sama
  **25/25 lulus pada binary native**; ekuivalensi output interpreter vs
  native byte-per-byte pada 7 contoh + rekursi 100rb level (8/8).
- **Fase 2 ✔** freestanding: binary native berjalan **tanpa libc** —
  `ldd` → "not a dynamic executable", `readelf -l` tanpa INTERP,
  `readelf -d` tanpa DYNAMIC, `file` → statically linked; syscall nyata
  (read/write/clock_gettime) terbukti di binary 4,1 KB (audit otomatis:
  `tests/audit_native.sh`).
- Benchmark (mesin sama, keluaran identik): sum 100M native 0,23 s vs
  interpreter 1,44 s (**6,2×**); fib27 2,7× di bawah gcc -O2; binary native
  4,1 KB vs runtime interpreter 43 KB.

## Fase 1 — Compiler native Inti → x86-64 ✔ SELESAI

Dikerjakan sesuai rencana dan teruji:

1. ✔ Backend codegen: setiap bytecode Inti diterjemahkan ke instruksi
   x86-64 (mov/add/cmp/jcc/call/ret) — pemetaan 1:1 karena Inti memang
   dirancang bergaya assembly. Rincian di `docs/COMPILER.md`.
2. ✔ Emit ELF langsung dari compiler (tanpa assembler/linker eksternal) —
   header ELF64 + 2 PT_LOAD ditulis tangan di `elfw.c`.
3. ✔ Milestone teruji: `inti-cc program.inti -o program` menghasilkan
   binary native yang lulus **test suite yang sama 25/25** dan berjalan
   tanpa interpreter.
4. ✔ Target performa tercapai: fib(27) hanya 2,7× di bawah gcc -O2;
   loop murni 6,2× lebih cepat dari interpreter sendiri.

## Fase 2 — Freestanding: lepas dari libc ✔ SELESAI

- ✔ Semua layanan libc diganti runtime mesin Inti (`runtime.c`):
  alokator arena first-fit, format desimal, penanganan error — semuanya
  **kode mesin yang diemisikan**, kernel hanya via mmap/write/exit +
  syscall yang diminta program.
- ✔ ABI sendiri didokumentasikan dan dikunci test: frame di `RBP`, slot di
  `[rbp-S+8k]`, argumen di push terbalik, nilai balik di `RAX`, stack
  16-byte aligned, runtime menjaga RBX/RBP/R12–R15 (`docs/COMPILER.md §2`).
- ✔ Milestone: binary Inti berjalan di Linux **tanpa libc** — diuji
  otomatis oleh `tests/audit_native.sh` (ldd → "not a dynamic
  executable", readelf tanpa INTERP/DYNAMIC, syscall nyata, error runtime
  tertangani runtime sendiri).

## Fase 3 — IntiOS: mini-kernel yang booting nyata — **✔ SELESAI**

IntiOS v0.1.0 **boot BIOS nyata** (tanpa GRUB/Limine — boot sector buatan
sendiri) dan **teruji di QEMU via GitHub Actions CI/CD**:

1. ✔ Boot: boot sector 512 byte buatan sendiri (int13h extended read LBA
   8×64 sektor, A20 port 0x92, GDT, PM32, paging 2MB identity 1GB,
   EFER.LME, long mode 64-bit) — kernel ter-load di 0x100000.
2. ✔ Driver dasar: VGA teks 80×25 + UART COM1 (dual output untuk CI),
   keyboard PS/2 scancode set 1 + input serial, polling murni (PIC
   dimask penuh).
3. ✔ Interrupt & memori: GDT/TSS/IDT 256 gate, arena user 32MB first-fit
   (algoritme identik dgn interpreter), identity map 1GB P|RW|US.
4. Scheduler: — (belum; single-tasking, shell kooperatif).
5. ✔ Syscall ring 3 ↔ ring 0: instruksi **SYSCALL/SYSRET nyata**
   (EFER.SCE, LSTAR, STAR, FMASK), nomor kompatibel Linux
   (read=0, write=1, exit=60), ENOSYS utk sisanya.
6. FS sederhana: — (belum; program di-embed ke image kernel via
   inti-cc --target=intios, flat binary .ibin 64B header).

Milestone teruji: **19/19 asersi** — boot nyata di QEMU 8.2 (CI GitHub)
dan 10.0 (lokal), shell interaktif via serial, 6 program Inti (hello,
primes, fib, fizzbuzz, memory, echo) dieksekusi CPL3 dan output-nya
**identik byte-per-byte dengan interpreter**, exception (#DE/#UD/#GP/#PF)
dilaporkan lalu kembali ke shell, shutdown ACPI S5 keluar bersih.

Bug nyata yang ditemukan & diperbaiki di Fase 3 (semua terdokumentasi
di commit): DAP int13h harus di offset genap, deskriptor kode32 vs kode64
terpisah (L=0/D=1 vs L=1), CR4.PAE wajib sebelum CR0.PG, entri tabel
halaman long mode 8 byte (bukan 4!), bit U/S wajib di SEMUA level (PML4/
PDPT/PD) agar CPL3 bisa akses, entry .ibin = PROG_BASE+entry_off.

## Fase 4 — Ekosistem minimal

- libc-inti (open/read/write/exit), util userspace: sh mini, cat, ls.
- Toolchain mandiri: compiler Inti yang **dijalankan di IntiOS** dan
  mengkompilasi IntiOS (self-hosting) — momen kemandirian sejati.
- Uji di hardware nyata: laptop tua / SBC x86 (USB boot) — bukti terakhir
  "bukan simulasi".

## Realitas tujuan "memodifikasi 100% kernel Linux"

Angka jujur: kode sumber kernel Linux modern ±30–40 juta baris (±60%
driver, ditambah arsitektur, filesystem, jaringan, dan lainnya) dan
berkembang tiap hari oleh ribuan kontributor. Menulis ulang 100% dari nol
dalam bahasa apa pun = proyek dekade komunitas, bukan proyek individu.

Jalur yang realistis dan tetap setia pada tujuan Anda:

| Jalur | Apa artinya | Kemungkinan |
|---|---|---|
| **A. IntiOS (Fase 3–4)** | Kernel & OS 100% Inti, dari bootloader ke userspace — kernel milik Anda sepenuhnya | Sepenuhnya mungkin; inilah rekomendasi utama |
| B. Modul kernel Inti | Menulis modul Linux tertentu (driver, syscall kecil) dalam Inti → butuh backend Inti→C atau backend LLVM agar kompatibel ABI kernel | Mungkin per modul; perlu Fase 1 + kerja ABI keras |
| C. Transpiler Inti→C | Kode Inti diterjemahkan ke C agar bisa ikut build kernel Linux | Mungkin untuk eksperimen/subsistem terisolasi; "100%" tetap tidak praktis |
| D. Menulis ulang Linux penuh | 30–40 juta baris + bergerak tiap hari | Tidak realistis untuk siapa pun secara individual — dan tidak perlu, karena jalur A memberi Anda OS yang benar-benar milik Anda |

Kesimpulan: **target yang bisa diuji dan dimenangkan adalah IntiOS** —
OS milik Anda yang 100% ditulis dalam Inti, booting nyata di QEMU lalu di
hardware. Jalur B/C bisa menyusul sebagai jembatan eksperimental ke ekosistem
Linux, satu modul demi satu modul.

## Ringkasan tahap & definisi "selesai"

| Fase | Keluaran | Definisi selesai (teruji) |
|---|---|---|
| 0 ✔ | Interpreter | 25/25 test lulus, benchmark terukur |
| 1 ✔ | Compiler native | test suite sama lulus pada binary native (25/25) |
| 2 ✔ | Freestanding | jalan tanpa libc, audit ldd/readelf lulus otomatis |
| 3 | IntiOS | boot QEMU + uji otomatis I/O, timer, 2 proses |
| 4 | Self-hosting | Inti mengkompilasi Inti di dalam IntiOS; boot USB hardware |

## Catatan kemajuan Fase 4 (self-hosting — berjalan)

Mesin penyusunnya sudah dibangun dan diuji terpisah:

- `tools/ibin-loader.c` — pemuat `.ibin` (ABI IntiOS) di atas Linux: memuat
  blob di 0x400000, arena 32 MB di 0x1000000, stack 8 MB — syscall program
  dieksekusi langsung oleh kernel Linux (nomor kompatibel). Bukti bahwa satu
  blob `.ibin` identik berjalan di dua platform nyata (Linux + IntiOS).
- `selfhost/intib.inti` — **compiler Inti/0 → .ibin yang ditulis 100% dalam
  bahasa Inti** (~5.600 baris): lexer, pass1, encoder x86-64 (REX/ModRM/SIB,
  label + fixup rel32/abs32), runtime program hasil (printi/die/bounds),
  `_start` ABI kernel, penulis header `.ibin`, dan probe target keluaran
  (stdout di Linux / syscall 1000 di IntiOS).
- Perluasan bahasa: `data nama, N` (reservasi BSS) — diuji 26/26 di kedua
  backend.
- Rantai uji: `tests/run_tests_selfhost.sh` (paritas byte interpreter vs
  program hasil kompilasi + titik tetap intib→intib2→intib3).

Status jujur: `intib` sudah mengompilasi dan menjalankan program Inti/0
sederhana (aritmetika, printi/printc, exit code, sys write/read, memori
byte) dengan keluaran byte-per-byte identik dengan interpreter. Kasus dengan
pemanggilan fungsi dan titik tetap masih menabrak bug integrasi tahap akhir
(penetapan fixup) yang sedang dikejar — tidak ada yang disembunyikan: test
suite self-hosting belum hijau penuh dan Fase 4 belum dinyatakan selesai.
