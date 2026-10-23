# Inti — bahasa tingkat rendah untuk komunikasi langsung dengan hardware

[![CI](https://github.com/muawamaditya329/inti-lang/actions/workflows/ci.yml/badge.svg)](https://github.com/muawamaditya329/inti-lang/actions/workflows/ci.yml)

**Inti** adalah bahasa pemrograman bergaya assembly untuk Linux x86-64. Filosofinya:
*apa yang Anda tulis itulah yang dieksekusi* — tanpa garbage collector, tanpa boxing
angka, tanpa alokasi memori tersembunyi, tanpa dependensi. Program Inti berbicara
langsung dengan kernel lewat **syscall Linux asli** dan mengelola **memori nyata**
(arena beralamat) dengan tangan sendiri.

Inti punya **dua backend** dengan semantik identik dan test suite yang sama:

| Backend | Perintah | Hasil |
|---|---|---|
| Interpreter | `./inti program.inti` | langsung jalan, 43 KB |
| **Compiler native** | `./inti-cc program.inti -o program` | ELF64 statis **4,1 KB tanpa libc** — hanya syscall |

```
Halo dari bahasa Inti!
42
```

## Mengapa "lebih cepat dan lebih ringan"?

- **Tidak ada yang tersembunyi.** Satu instruksi = satu operasi mesin logis.
  Tidak ada GC yang berjalan tiba-tiba, tidak ada objek yang dibuat diam-diam.
- **Angka = int64 mentah.** Register VM adalah array `int64_t` polos —
  persis seperti kata mesin.
- **Compiler native (Fase 1–2 selesai).** `inti-cc` menerjemahkan bytecode ke
  kode mesin x86-64 dan menulis ELF64 langsung — tanpa assembler, tanpa
  linker, tanpa libc. Runtime (alokator memori, format angka, error) adalah
  **kode mesin yang diemisikan** ke setiap binary.

### Hasil benchmark nyata (bukan simulasi)

Diukur pada x86_64, 2 core, kernel 5.10, metode best-of-N via `fork+wait4`
(lihat `bench/run_bench.sh` dan `bench/run_bench_native.sh`). Semua program
menghasilkan keluaran yang identik di C, Inti native, Inti interpreter, dan Python.

| Program | C (gcc -O2) | **Inti NATIVE** | Inti (interpreter) | Python 3.12 |
|---|---|---|---|---|
| sum 1..100.000.000 | 0,001 s * | **0,231 s** | 1,439 s | 12,554 s |
| fib(27) rekursif | 0,001 s | **0,004 s** | 0,015 s | 0,039 s |
| isi+jumlah 4 MiB byte | 0,003 s | **0,035 s** | 0,223 s | 0,935 s |
| startup program kosong | — | **0,9 ms** | 1,0 ms | 12,0 ms |
| ukuran binary | 16 KB | **4,1 KB** | 43 KB | — |

- Native **6,2–6,4× lebih cepat** dari interpreter Inti sendiri; fib(27)
  hanya **2,7× di bawah gcc -O2**.
- Terhadap Python: native **11–54× lebih cepat** (sum 54×, fib 11×, mem 27×).
- \* GCC mengubah loop penjumlahan menjadi rumus aljabar — bukan perbandingan
  loop yang sehat; dicatat jujur sejak Fase 0.
- Audit freestanding terbukti otomatis: `ldd` → "not a dynamic executable",
  `readelf -l` tanpa INTERP, `file` → statically linked (`make audit`).

## Mulai dalam 30 detik

```bash
make                        # bangun inti (interpreter) + inti-cc (compiler native)
./inti examples/hello.inti  # jalankan lewat interpreter
./inti-cc examples/hello.inti -o hello   # kompilasi ke binary native
./hello                     # jalankan TANPA interpreter — 4,1 KB, tanpa libc
echo "halo" | ./inti examples/echo.inti   # syscall read/write asli
make test                   # 25+25 test + 8 ekuivalensi + audit freestanding: semua LULUS
make audit                  # bukti binary native tanpa libc (ldd/readelf/file)
make bench && make bench-native   # benchmark: interpreter, native, C, Python
```

Opsi compiler: `--arena-mb N` (arena dibakar ke binary, bawaan 64) dan
`--stack-mb N` (stack mmap untuk rekursi dalam, bawaan 16).

## Bahasanya sekilas

```asm
# fib.inti — fibonacci rekursif
fn fib, n {
    cmp n, 2          # bandingkan -> flag
    jl kecil          # lompat jika n < 2
    sub n, 1
    call a, fib, n    # a = fib(n-1)
    sub n, 1
    call b, fib, n    # b = fib(n-2)
    add r, a, b
    ret r
kecil:
    ret n
}
call v, fib, 10
printi v              # 55
```

Memori dan syscall — komunikasi hardware sejati:

```asm
set buf, 0
alloc buf, 4096                 # minta blok memori nyata
sys n, SYS_read, STDIN, @buf, 4096   # syscall read(2) — @buf = pointer nyata
sys w, SYS_write, STDOUT, @buf, n    # syscall write(2)
```

## Isi proyek

| Jalur | Isi |
|---|---|
| `src/` | interpreter C (lex/parse/vm/main) **+ backend compiler** (encode/runtime/codegen/elfw/inticc) |
| `examples/` | 7 program contoh (fib, primes, echo syscall, clock_gettime...) — jalan di kedua backend |
| `tests/` | 25 kasus uji nyata + suite native + ekuivalensi + audit freestanding |
| `bench/` | benchmark identik dalam 3 bahasa + varian native + harness pengukuran |
| `docs/SPEC.md` | spesifikasi lengkap bahasa |
| `docs/TUTORIAL.md` | tutorial 10 menit |
| `docs/COMPILER.md` | desain compiler native: ABI, pemetaan bytecode→x86-64, ELF, runtime mesin |
| `docs/ROADMAP-OS.md` | peta jalan menuju mini-OS IntiOS dan realitas "kernel 100% Inti" |

## Batasan versi 0.2 (jujur)

- Kode native belum dioptimalkan agresif (tanpa register allocation penuh,
  tanpa SSA): nilai hidup di memori, bukan register — sudah 6× di atas
  interpreter dan 2,7× di bawah gcc -O2 untuk fib; ruang optimasi masih luas.
- Pesan error runtime native lebih ringkas daripada interpreter (fragmen
  kunci tetap sama sehingga test `.err` cocok di kedua backend).
- Stack overflow pada binary native = SIGSEGV dari kernel (interpreter
  memberi pesan ramah); ukuran stack diatur `--stack-mb`.
- Tipe data: int64 saja (byte ditangani lewat load/storeb).
- Tidak ada string tingkat tinggi, struct, atau modul — memori dan byte
  dikelola eksplisit, sesuai semangat low-level.
- Satu arena memori per proses (bawaan 64 MB, dibakar saat kompilasi via
  `--arena-mb`).

## Lisensi

Proyek contoh bebas dipakai dan dimodifikasi untuk keperluan Anda.
