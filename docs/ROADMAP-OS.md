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

## Fase 3 — IntiOS: mini-kernel yang booting nyata

Mulai menulis OS di atas hardware emulasi yang **mengeksekusi kode asli**
(QEMU = eksekusi nyata, bukan simulasi logika; pengujian akhir di hardware
USB-boot):

1. Boot: pakai bootloader siap pakai (Limine/GRUB Multiboot2); kernel Inti
   masuk long mode 64-bit.
2. Driver dasar: framebuffer VGA/VBE (cetak karakter), keyboard PS/2
   (scancode → buffer), timer PIT (tick).
3. Interrupt & memori: IDT, GDT, handler IRQ; alokator frame fisik +
   paging; malloc kernel dari Fase 2.
4. Scheduler: kooperatif dulu, lalu preemptive (quantum dari PIT).
5. Syscall antara ring 3 ↔ ring 0 (int 0x80 atau syscall/sysret).
6. FS sederhana: FAT16 read-only → ramfs read-write.

Milestone teruji: boot QEMU, mengetik di layar, dua proses bergantian,
proses userspace memanggil syscall IntiOS. Setiap milestone punya skrip
uji otomatis (QEMU + serial log dibandingkan dengan ekspektasi).

Estimasi: 6–18 bulan bagian tersulit sekaligus paling mendidik.

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
