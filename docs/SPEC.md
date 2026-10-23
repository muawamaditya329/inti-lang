# Spesifikasi Bahasa Inti v0.3

> **Dua backend, satu semantik.** Sejak v0.2 setiap program Inti dapat
> dijalankan lewat interpreter (`inti file.inti`) ATAU dikompilasi menjadi
> binary native (`inti-cc file.inti -o program`) — ELF64 statis tanpa libc.
> Semua semantik pada dokumen ini berlaku identik untuk keduanya dan
> dipastikan oleh test suite yang sama (25/25 + 8/8 ekuivalensi).
> Rincian compiler: `docs/COMPILER.md`.

Dokumen ini adalah referensi lengkap sintaks dan semantik Inti. Inti adalah
bahasa tingkat rendah bergaya assembly: **satu baris = satu instruksi**,
operan dipisah koma, tanpa ekspresi bersarang (aritmetika dilakukan instruksi
per instruksi, seperti mesin sungguhan).

## 1. Lexik

- **Komentar**: `#` sampai akhir baris.
- **Baris**: pemisah pernyataan. Satu instruksi per baris, boleh `label: instruksi`.
- **Angka**: desimal `42`, heksa `0xFF`, biner `0b1010`, negatif `-5`.
- **Karakter**: `'A'`, `'\n'`, `'\t'`, `'\\'`, `'\0'` — bernilai kode byte-nya.
- **String** (hanya di `data`): `"teks\n"` dengan escape `\n \t \r \\ \" \0 #`.
- **Pengenal**: `[A-Za-z_][A-Za-z0-9_]*`.

## 2. Struktur program

```
# kode tingkat atas = badan fungsi implisit __main__
set x, 5

fn nama, param1, param2 {
    ...instruksi...
    ret nilai        # opsional; jatuh ke akhir = ret 0
}

data pesan, "Halo\n"  # label data: byte masuk arena, nama = alamat
```

- Eksekusi dimulai dari baris teratas; `ret` di tingkat atas = keluar dengan
  kode balik tersebut; jatuh ke akhir = keluar 0.
- Fungsi boleh didefinisikan di mana saja (boleh setelah dipanggil —
  resolusi dilakukan setelah seluruh file dibaca).
- `fn` hanya boleh di tingkat atas (tidak di dalam blok/fungsi lain).

## 3. Operan

| Bentuk | Contoh | Arti |
|---|---|---|
| literal | `42`, `0xFF`, `'A'`, `-5` | konstanta int64 |
| variabel | `x` | slot lokal fungsi saat ini |
| ofset variabel | `buf+8`, `buf-1` | nilai `buf` ± konstanta |
| label data | `pesan`, `pesan+4` | alamat arena ± konstanta |
| pointer | `@buf`, `@buf+4`, `@0` | dikirim sebagai **pointer nyata** ke syscall |
| buang | `_` | sebagai tujuan: hasil dibuang |

- Variabel tidak bertipe (selalu int64) dan **lokal per fungsi**; parameter
  adalah variabel pertama.
- Prioritas nama: variabel lokal > konstanta bawaan > label data.
- Konstanta bawaan: `SYS_read=0, SYS_write=1, SYS_open=2, SYS_close=3,
  SYS_stat=4, SYS_lseek=8, SYS_mmap=9, SYS_brk=12, SYS_getpid=39,
  SYS_kill=62, SYS_getuid=102, SYS_exit=60, SYS_clock_gettime=228,
  STDIN=0, STDOUT=1, STDERR=2, true=1, false=0`.
- Membaca variabel sebelum didefinisikan = error kompilasi.

## 4. Instruksi

### 4.1 Pergerakan & aritmetika (semua int64, aritmetika modular)

```
set d, s              # d = s
add d, s   |  add d, a, b   # d += s  |  d = a + b   (bentuk 2 & 3 operand)
sub, mul, div, mod, and, or, xor, shl, shr, sar   # pola sama
neg d   |  neg d, s    # d = -s
not d   |  not d, s    # d = ~s (komplemen bitwise)
```

Semantik penting:
- `div`/`mod` dengan nol = **error runtime**.
- `shr` = geser kanan **logis** (bit tanda ikut bergeser); `sar` = aritmetika
  (tanda dipertahankan). Jumlah geser dimask 6 bit (0..63), seperti x86.
- `div INT64_MIN / -1` didefinisikan aman (hasil = INT64_MIN / 0).

### 4.2 Kontrol alur terstruktur

```
if kondisi { ... } else { ... }        # else-if berantai didukung
while kondisi { ... }                  # break / continue
```

Kondisi: `a < b`, `a <= b`, `a > b`, `a >= b`, `a == b`, `a != b`, atau
operand tunggal (benar bila ≠ 0). Tidak ada `&&`/`||` — gabungkan dengan
if bersarang atau lompatan mentah (gaya assembly).

`break`/`continue` merujuk `while` terdalam; di luar loop = error kompilasi.

### 4.3 Lompatan mentah & label (gaya assembly murni)

```
nama_label:                  # definisi (boleh: nama: instruksi)
jmp nama_label
cmp a, b                     # set flag pembanding
je  L   jne  L               # lompat jika sama / tidak sama
jl  L   jle  L               # lebih kecil / kecil-sama (bertanda)
jg  L   jge  L               # lebih besar / besar-sama (bertanda)
```

`cmp` menyimpan kedua operan sebagai flag; instruksi `j*` membandingkan
flag terakhir. Label berlaku per fungsi; ganda = error; tak dikenal = error.

### 4.4 Fungsi

```
fn tambah, a, b { add r, a, b; ret r }
call x, tambah, 3, 4         # x = 7
call _, tambah, 3, 4         # nilai balik dibuang
```

- Jumlah argumen **diverifikasi saat kompilasi**.
- Rekursi didukung; kedalaman dibatasi `--frames` (frame) dan `--stack-mb`
  (tumpukan nilai). Rekursi 100.000 level lolos pada pengaturan bawaan.
- `ret` tanpa nilai mengembalikan 0.

### 4.5 Memori (arena nyata)

```
alloc d, byte        # d = alamat blok; arena = memori host sungguhan
free alamat          # kembalikan blok (first-fit reuse; tanpa penggabungan)
load d, alamat       # baca 8 byte (int64, little-endian)
store alamat, v      # tulis 8 byte
loadb d, alamat      # baca 1 byte (0..255)
storeb alamat, v     # tulis 1 byte (v & 0xFF)
```

- Alamat adalah **offset ke arena** — memori nyata di host, dibatasi ketat:
  akses di luar arena = error runtime (tidak ada segfault diam-diam).
- `store/load` memakai memcpy → tidak butuh alamat sejajar.
- Seksi `data` menempati alamat 0..N; `alloc` dimulai setelahnya.
- Alokator: header 16 byte per blok, first-fit, tanpa split/coalesce.
  `free` bukan GC — Anda yang memutuskan, persis seperti C.
- Kapasitas bawaan 64 MB; ubah dengan `inti --arena-mb 256 prog.inti`.

### 4.6 Syscall Linux asli & keluaran

```
sys d, NR, a1, ..., a6      # syscall(2) sungguhan; argumen hilang = 0
printi x                    # cetak int64 (tanpa newline)
prints alamat, byte         # tulis byte arena ke stdout
printc kode                 # tulis 1 karakter
slen d, labeldata           # panjang byte label data
exit kode                   # akhiri proses dengan kode keluar
```

- `NR` = nomor syscall x86-64 (pakai konstanta `SYS_*`).
- Argumen **berawalan `@`** dikonversi menjadi pointer nyata
  (`arena + alamat`) — inilah jalur komunikasi langsung dengan kernel:
  `sys n, SYS_write, STDOUT, @buf, len`.
- Batas maksimal 6 argumen sesuai ABI syscall.
- `printi/prints/printc` menulis langsung (tanpa buffer).

### 4.7 Data

```
data nama, "byte\n"     # deklarasikan byte; nama = alamat konstanta
data nama, N            # cadangkan N byte nol (gaya BSS), N = 0..16 MiB
```

Boleh di mana saja (termasuk di fungsi); referensi maju didukung. Bentuk
angka (v0.3) dipakai untuk buffer kerja berukuran tetap — byte-nya nol
dan mutable lewat loadb/storeb seperti string.

## 5. Diagnostik

Error kompilasi dan runtime dicetak ke stderr dengan nomor baris, lalu
proses keluar dengan kode 1. Contoh: `pembagian dengan nol`,
`label 'x' tidak dikenal`, `akses memori di luar arena`,
`fungsi 'f' butuh 2 argumen, diberi 3`.

## 6. Opsi CLI interpreter

```
inti [opsi] file.inti
  --arena-mb N    ukuran arena (MB, bawaan 64)
  --stack-mb N    tumpukan nilai (MB, bawaan 16)
  --frames N      kapasitas frame pemanggilan (bawaan 262144)
  -h / -v         bantuan / versi
```

### Opsi CLI compiler native

```
inti-cc [opsi] file.inti -o keluaran
  -o FILE         nama binary ELF64 keluaran (wajib)
  --arena-mb N    arena memori dibakar ke binary (MB, bawaan 64)
  --stack-mb N    ukuran stack mmap untuk rekursi dalam (MB, bawaan 16)
  -h / -v         bantuan / versi
```

Error kompilasi dilaporkan `inti-cc` dengan format dan nomor baris yang
sama dengan interpreter (kode di parse.c dibagi). Error runtime pada
binary native ditangani runtime mesin dengan fragmen pesan yang sama
("pembagian dengan nol", "di luar arena", dst.).

## 7. Semantik yang perlu diingat

- Semua angka int64 beraritmetika modular (overflow melipat, tanpa error) —
  perilaku kata mesin.
- Tidak ada tipe boolean: 0 = salah, selain 0 = benar.
- Tidak ada nilai NULL: `alloc` selalu berhasil atau error; `free` ganda
  akan menandai blok lagi (aman dari magic-check, tapi jangan andalkan).
- Urutan instruksi = urutan eksekusi. Tidak ada optimasi, tidak ada kejutan.
