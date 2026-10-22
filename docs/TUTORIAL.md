# Tutorial Inti dalam 10 Menit

Target: dari nol sampai menulis program yang berbicara langsung dengan
kernel Linux. Semua contoh bisa dijalankan — bukan simulasi.

## 0. Pasang (1 menit)

```bash
cd inti-lang
make                 # butuh cc/gcc; menghasilkan binary ./inti (±43 KB)
./inti --version     # inti v0.1.0
```

## 1. Halo dunia (dan cara Inti bekerja)

```asm
data msg, "Halo dunia!\n"     # byte masuk arena memori, msg = alamatnya
slen n, msg                   # n = panjang string (13)
prints msg, n                 # tulis n byte dari alamat msg ke stdout
exit 0
```

Jalankan: `./inti hello.inti`. Perhatikan: **tidak ada fungsi print ajaib** —
`prints` akhirnya memanggil `write(2)` kernel dengan pointer memori nyata.

## 2. Variabel dan aritmetika — tanpa ekspresi!

Inti adalah bahasa instruksi: satu baris satu operasi. Tidak ada `x = a*b+1`.
Tulis seperti mesin:

```asm
set a, 6
set b, 7
mul c, a, b       # bentuk 3-operand: c = a * b
add c, 1          # bentuk 2-operand: c = c + 1
printi c          # 43
```

Operator: `add sub mul div mod` (matematika), `and or xor shl shr sar`
(bitwise), `neg not`. Semua int64; `div`/`mod` nol = error runtime.

## 3. Kontrol alur: dua gaya

**Gaya terstruktur** — untuk logika sehari-hari:

```asm
set i, 1
while i <= 5 {
    if i == 3 {
        add i, 1
        continue
    }
    printi i          # 1 2 4 5
    printc 32         # spasi
    add i, 1
}
```

**Gaya assembly** — flag + lompatan, label boleh maju/mundur:

```asm
set x, 10
ulang:
    printi x
    printc 10
    sub x, 1
    cmp x, 0
    jg ulang          # lompat selama x > 0
```

`cmp a, b` menyetel flag; `je jne jl jle jg jge` melompat berdasar flag
(miram signed, seperti `cmp` x86).

## 4. Fungsi dan rekursi

```asm
fn faktorial, n {
    set hasil, 1
    ulang:
    mul hasil, n
    sub n, 1
    cmp n, 1
    jg ulang
    ret hasil
}
call f, faktorial, 5
printi f              # 120
```

- Parameter = variabel lokal pertama; `call tujuan, nama, arg1, arg2`.
- Jumlah argumen dicek saat kompilasi — salah jumlah = tidak jalan.
- `ret` tanpa nilai mengembalikan 0; `call _, f, x` membuang hasil.
- Rekursi jalan (lihat `examples/fib.inti`), hingga 100.000+ kedalaman.

## 5. Memori: Anda yang pegang kendali

```asm
set arr, 0
alloc arr, 80         # 10 elemen x 8 byte, alamat nyata di arena

set i, 0
isi:
    set al, arr       # alamat = arr + i*8 (hitung manual, gaya assembly)
    mul off, i, 8
    add al, off
    mul v, i, i
    store al, v       # tulis int64
    add i, 1
    cmp i, 10
    jl isi
```

- `store/load` = 8 byte; `storeb/loadb` = 1 byte (untuk teks/byte mentah).
- Ofset konstanta bisa ringkas: `storeb buf+3, 65`.
- `free alamat` mengembalikan blok (reuse first-fit). Tidak ada GC.
- Akses di luar arena = error jelas, bukan segfault misterius.

## 6. Syscall: bicara langsung dengan kernel

```asm
# echo mini: baca stdin, tulis balik ke stdout
set buf, 0
alloc buf, 4096
lagi:
    sys n, SYS_read, STDIN, @buf, 4096   # read(0, buf, 4096) ASLI
    cmp n, 0
    jle selesai
    sys w, SYS_write, STDOUT, @buf, n    # write(1, buf, n) ASLI
    jmp lagi
selesai:
exit 0
```

Coba: `echo "tes" | ./inti echo.inti`. Tanda `@` mengubah alamat arena
menjadi **pointer nyata** untuk kernel. Nomor syscall = nomor x86-64 Linux
(`SYS_write=1`, `SYS_getpid=39`, `SYS_clock_gettime=228`, ...).

Contoh lain `examples/time.inti` membaca jam sistem lewat
`clock_gettime` + struct di memori yang Anda siapkan sendiri.

## 7. Disiplin proyek

```bash
make test      # 25 test nyata (output, exit code, pesan error)
make bench     # benchmark vs C & Python, hasil nyata di layar
```

## 8. Latihan

1. Tambahkan `examples/avg.inti`: baca deretan angka? (Mulai dari array
   konstan di `data`, hitung rata-rata dengan `div`.)
2. Tulis `rev.inti`: balik string dari argv? (v0.1 belum punya argv —
   ganti: balik isi buffer dari `data`.)
3. Buat syscall `SYS_open`+`SYS_read` membaca file nyata, cetak isinya
   (petunjuk: `data path, "README.md\0"`, `@path`).
4. Bandingkan waktu `bench/sum.inti` di mesin Anda dengan tabel README.

Kalau latihan nomor 3 berhasil, Anda sudah melakukan hal yang sama dengan
program C tingkat rendah — hanya tanpa libc. Selamat, itu jalur menuju
[ROADMAP-OS.md](ROADMAP-OS.md).
