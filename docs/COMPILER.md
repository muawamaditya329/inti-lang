# COMPILER.md — Compiler Native Inti → x86-64 (Fase 1–2)

Dokumen teknis backend compiler `inti-cc`: bagaimana bytecode Inti diubah
menjadi kode mesin x86-64, bagaimana ELF64 ditulis langsung tanpa
assembler/linker eksternal, dan mengapa binary hasilnya berdiri sendiri
(tanpa libc, hanya syscall).

## 1. Gambaran alur

```
                 DIPAKAI BERSAMA (satu semantik, satu test suite)
  file.inti ──► lex.c ──► parse.c ──► bytecode (Prog/Func/Ins)
                                        │
                     ┌──────────────────┴──────────────────┐
                     ▼                                     ▼
              vm.c (interpreter)                 codegen.c (Fase 1)
              switch-dispatch                    bytecode → kode mesin
                                                     │
                                              runtime.c  (runtime = kode mesin)
                                                     │
                                              elfw.c     (ELF64 statis, Fase 2)
                                                     ▼
                                         binary native 4–5 KB, tanpa libc
```

Kunci desain: **parser dan semantik dibagi dengan interpreter**. Compiler
tidak mendefinisikan ulang bahasa; ia menerjemahkan bytecode yang sama
yang dijalankan VM. Konsekuensinya, test suite yang sama (25 kasus)
dipakai untuk kedua backend — dan keduanya lulus 25/25.

## 2. ABI Inti (konvensi kode hasilkan)

| Elemen          | Keputusan                                                            |
|-----------------|----------------------------------------------------------------------|
| Frame pointer   | `RBP` — dipakai semua fungsi hasilkan, dijaga runtime                 |
| Slot variabel   | `[rbp - S + 8k]`, `S = align16(nslots × 8)`, slot 0 = param pertama    |
| Argumen         | di-push terbalik saat call; `arg_k = [rbp + 16 + 8k]`, disalin ke slot oleh callee |
| Nilai balik     | `RAX`; pemanggil menyimpannya ke slot tujuan `call` bila bukan `_`      |
| Scratch         | `RAX, RCX, RDX, RSI, RDI, R8–R11` (semua caller-saved)                 |
| Alignment       | `rsp ≡ 0 (mod 16)` pada setiap titik `call`                            |
| Pasangan cmp    | `fa`/`fb` (di VM = variabel global) → **dua sel global RW**; peephole memakai flag CPU bila `jcc` tepat setelah `cmp` |
| Rekursi         | `_start` melakukan `mmap` stack sendiri (`--stack-mb`, bawaan 16 MB) — 100.000 level rekursi teruji |

Karena nilai selalu hidup di memori (stack), tidak ada liveness analysis:
setiap instruksi memuat operand ke register, menghitung, menyimpan kembali.
Sederhana, benar, dan cukup cepat (lihat §6).

## 3. Pemetaan bytecode → x86-64 (inti)

| Bytecode                  | Kode mesin (bentuk tipikal)                                     |
|---------------------------|------------------------------------------------------------------|
| `set a, 5`                | `mov qword [rbp-S], imm32` (peephole konstanta)                   |
| `set a, b`                | `mov rax,[rbp-Sb]; mov [rbp-Sa],rax`                             |
| `add i, 1` (2-operand)    | `add qword [rbp-Si], 1` (peephole imm)                           |
| `mul c, a, b` (3-operand) | `mov rax,..; mov rcx,..; imul rax,rcx; mov [..],rax`             |
| `div/mod`                 | cek nol inline + kasus `INT64_MIN/-1` inline + `cqo; idiv rcx`   |
| `shl/shr/sar`             | `rcx` = jumlah; `shl/shr/sar rax, cl` (mask &63 = sama dgn VM)   |
| `cmp a,b`                 | muat a,b → simpan `[g_fa],[g_fb]` → `cmp rax,rcx`                |
| `je/jne/...` (setelah cmp)| `jcc rel32` langsung (peephole flag)                             |
| `jcc` (jauh dari cmp)     | `mov rax,[g_fa]; cmp rax,[g_fb]; jcc rel32`                      |
| `call f, x`               | push arg terbalik + pad 16-align; `call rel32`; simpan `RAX`     |
| `ret v`                   | `RAX = v`; `mov rsp,rbp; pop rbp; ret` (di `__main__` → exit)    |
| `alloc/free`              | `call __rt_alloc/__rt_free` (alokator mesin, §4)                 |
| `load/store/loadb/...`    | cek batas inline (`cmp` vs `cap-8`/`cap-1`) + akses langsung     |
| `prints`                  | cek batas (dua cabang: len>cap, off>cap-len) + `syscall write`   |
| `printi`                  | `call __rt_printi` (format desimal oleh runtime mesin)           |
| `sys n, NR, @buf, ...`    | argumen → `RDI,RSI,RDX,R10,R8,R9`; operand `@` = `base+off` hasil cek batas; `RAX = NR; syscall` |
| `exit v`                  | `RDI = v & 0xff; RAX=60; syscall`                                |

Semua cabang internal = `rel32`; semua alamat global/string = `disp32`
absolut lewat SIB `base=101` (mode 64-bit: ModRM `rm=101` murni adalah
**RIP-relative** — jebakan klasik yang sudah diuji dan diperbaiki).

## 4. Runtime mesin (tanpa libc)

Rutinitas berikut **diemisikan sebagai kode mesin** ke setiap binary —
bukan objek terpisah, bukan libc:

| Rutinitas       | Tugas                                                                 |
|-----------------|------------------------------------------------------------------------|
| `_start`        | mmap stack (16 MB) + mmap arena (64 MB) + salin image data + `call __main__` + `exit(rax&0xff)` |
| `__rt_alloc`    | alokator first-fit ber-header {size,used,magic="INTI"} — **algoritma sama persis dengan vm.c** |
| `__rt_free`     | cek header + `used=0` (paritas: bebas di-free dua kali, sama dgn VM)   |
| `__rt_printi`   | int64 → desimal (negatif & INT64_MIN benar) + `write(1,...)`          |
| `__rt_fmt`      | pembantu konversi desimal                                              |
| `__rt_die`      | `inti: error (baris N): <pesan>` ke stderr + `exit(1)`                |
| `__rt_oob`      | pesan akses di luar arena (`load/store/loadb/storeb/prints/sys@`)     |

Kernel hanya disentuh lewat 4 syscall: **mmap(9), write(1), exit(60)** dan
syscall yang diminta program sendiri via `sys` (read/open/clock_gettime/...).
Pesan error runtime memakai fragmen yang sama dengan interpreter sehingga
test `.err` cocok di kedua backend.

## 5. Layout ELF64 (ditulis tangan oleh elfw.c)

```
offset 0        ELF header (64 B)  + 2 program header (112 B)
offset 0xB0     kode mesin: _start, runtime, fungsi user, string rodata, image data
offset page     .data: 8 global (fa, fb, base, cap, top, dlen, cap-8, cap-1)

PT_LOAD #1  R+X  offset 0          vaddr 0x400000            file+teks+rodata
PT_LOAD #2  R+W  offset page       vaddr 0x400000+page       64 byte global
```

Tidak ada section header, tidak ada INTERP, tidak ada DYNAMIC.
`file` → "statically linked"; `ldd` → "not a dynamic executable";
`readelf -l` → tanpa INTERP. Ukuran binary hello: **4.160 byte**.

## 6. Benchmark nyata (mesin sama, best-of-N, keluaran identik 4 backend)

| Program    | C gcc -O2 | Inti NATIVE | Inti interpreter | Python 3.12 |
|------------|-----------|-------------|------------------|-------------|
| sum 100M   | 0,001 s\* | **0,231 s** | 1,439 s          | 12,554 s    |
| fib(27)    | 0,001 s   | **0,004 s** | 0,015 s          | 0,039 s     |
| mem 4 MiB  | 0,003 s   | **0,035 s** | 0,223 s          | 0,935 s     |
| startup    | —         | **0,9 ms**  | 1,0 ms           | 12,0 ms     |
| binary     | 16 KB     | **4,1 KB**  | 43 KB            | —           |

- Native **6,2–6,4× lebih cepat** dari interpreter sendiri; fib (banyak call)
  hanya 2,7× di bawah gcc -O2 — sesuai target Fase 1 (faktor 2–3×).
- \*gcc -O2 mengganti loop sum dengan rumus aljabar (bukan perbandingan
  sehat) — dicatat jujur sejak Fase 0.

## 7. Perbedaan jujur native vs interpreter

| Aspek                 | Interpreter                          | Native                              |
|-----------------------|--------------------------------------|-------------------------------------|
| Pesan error runtime   | lengkap dengan angka alamat          | fragmen sama, format lebih ringkas  |
| Batas rekursi         | `--frames` + `--stack-mb` diatur VM  | dibatasi ukuran stack mmap (`--stack-mb`) |
| Stack overflow        | pesan jelas dari VM                  | SIGSEGV dari kernel                 |
| `@angka` di luar `sys`| perilaku patologis (dibaca sbg slot) | disamakan perilakunya               |
| Semantik lainnya      | —                                    | identik (25/25 + 8/8 ekuivalensi)   |

## 8. Cara memverifikasi sendiri

```bash
make                    # bangun inti (interpreter) dan inti-cc (compiler)
make test               # 25/25 interpreter + 25/25 native + 8/8 ekuivalensi + audit
make audit              # bukti freestanding: ldd/readelf/file + syscall nyata
make bench-native       # tabel benchmark 4 backend
./inti-cc examples/hello.inti -o hello && ./hello
objdump -D -b binary -m i386:x86-64 -M intel --adjust-vma=0x400000 hello   # inspeksi kode mesin
```

## 9. Peta berkas backend

| Berkas          | Isi                                                          | Baris |
|-----------------|--------------------------------------------------------------|-------|
| `src/cg.h`      | antarmuka backend, ABI global, label runtime                 | ±120  |
| `src/encode.c`  | encoder x86-64 (REX/ModRM/SIB, label+fixup rel32/abs32/glob) | ±390  |
| `src/runtime.c` | emisi runtime mesin (alloc/free/printi/die/oob)              | ±260  |
| `src/codegen.c` | translasi bytecode→mesin, peephole, `_start`, orkestrasi     | ±610  |
| `src/elfw.c`    | penulis ELF64 (2 PT_LOAD, chmod 0755)                        | ±90   |
| `src/inticc.c`  | CLI `inti-cc`                                                | ±80   |
