# mem.py — isi buffer 4 MiB byte-per-byte lalu jumlahkan (identik dengan mem.inti)
N = 4194304
buf = bytearray(N)
for i in range(N):
    buf[i] = (i * 7) & 255
s = 0
for i in range(N):
    s += buf[i]
print(s)
