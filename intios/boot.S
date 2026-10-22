/* boot.S — boot sector IntiOS (512 byte, BIOS klasik)
 *
 * Alur nyata (bukan simulasi):
 *   1. BIOS (SeaBIOS) memuat sektor ini ke 0x7C00 dan melompat ke _boot
 *      jika signature 0xAA55 ada di 0x7DFE.
 *   2. Init COM1 (UART 16550, 115200 8N1) -> marker serial untuk debugging.
 *   3. Baca image kernel dari disk via BIOS int 13h AH=42h (extended read,
 *      LBA) — 512 sektor (256 KB) ke buffer 0x10000, 8 potongan x 64 sektor.
 *   4. Aktifkan A20 (port 0x92).
 *   5. Bangun GDT di memori rendah (0x500) -> masuk protected mode 32-bit.
 *   6. Salin image kernel 0x10000 -> 0x100000 (kernel ter-link di 0x100000).
 *   7. Bangun tabel halaman identity-map 1 GB, halaman 2 MB
 *      (PML4@0x1000, PDPT@0x2000, PD@0x3000; entri P|RW|US|PS = 0x87).
 *   8. Aktifkan EFER.LME + CR0.PG|PAE -> lompat ke long mode 64-bit.
 *   9. Lompat ke kernel di 0x100000 (kstart64).
 *
 * Dipakai: gcc -c (gas AT&T), ld -Ttext=0x7C00 -e _boot, objcopy -O binary.
 */

    .code16
    .text
    .globl _boot

/* ---------------- titik masuk (BIOS melompat ke 0:0x7C00 = offset 0) ------ */
_boot:
    jmp start
    nop

/* ---------------- DAP (Disk Address Packet) int 13h AH=42h ---------------- */
dap:
    .word 0x0010            /* ukuran paket = 16                  */
dap_cnt:
    .word 0x0040            /* jumlah sektor per potongan = 64    */
dap_off:
    .word 0x0000            /* offset buffer = 0                  */
dap_seg:
    .word 0x1000            /* segmen buffer (0x1000:0 = 0x10000) */
dap_lba:
    .quad 1                 /* LBA awal (di-patch per potongan)   */

boot_drive:
    .byte 0

start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7C00, %sp
    mov %dl, boot_drive     /* simpan nomor drive boot (0x80) */

    /* ---- init COM1: 115200 baud, 8N1, FIFO, DTR|RTS|OUT2 ---- */
    mov $0x3FB, %dx         /* LCR                     */
    mov $0x80, %al          /* DLAB=1                  */
    out %al, %dx
    mov $0x3F8, %dx         /* DLL                     */
    mov $0x01, %al          /* divisor = 1 -> 115200   */
    out %al, %dx
    mov $0x3F9, %dx         /* DLM                     */
    xor %al, %al
    out %al, %dx
    mov $0x3FB, %dx         /* LCR                     */
    mov $0x03, %al          /* 8N1, DLAB=0             */
    out %al, %dx
    mov $0x3FA, %dx         /* FCR                     */
    mov $0xC7, %al          /* FIFO enable + clear     */
    out %al, %dx
    mov $0x3FC, %dx         /* MCR                     */
    mov $0x0B, %al
    out %al, %dx

    mov $'i', %al           /* marker serial: UART siap */
    call sputc

    /* ---- baca 512 sektor (8 potongan x 64) -> 0x10000..0x4FFFF ---- */
    xor %bp, %bp            /* bp = nomor potongan 0..7 */
load_chunk:
    /* LBA = 1 + bp*64 */
    mov %bp, %ax
    mov $6, %cl
    shl %cl, %ax            /* ax = bp*64              */
    inc %ax                 /* ax = 1 + bp*64          */
    mov %ax, dap_lba        /* word rendah; byte tinggi sudah 0 */

    /* segmen = 0x1000 + bp*0x800 (64 sektor = 0x8000 B = 0x800 paragraf) */
    mov %bp, %ax
    mov $11, %cl
    shl %cl, %ax            /* ax = bp*0x800           */
    add $0x1000, %ax
    mov %ax, dap_seg

    mov $dap, %si
    mov boot_drive, %dl
    mov $0x42, %ah          /* extended read           */
    int $0x13
    jnc load_ok
    mov $'E', %al           /* gagal baca disk          */
    call sputc
fail_hlt:
    hlt
    jmp fail_hlt
load_ok:
    mov $'.', %al
    call sputc
    inc %bp
    cmp $8, %bp
    jb load_chunk

    mov $'\r', %al
    call sputc
    mov $'\n', %al
    call sputc
    mov $'k', %al           /* marker: kernel termuat   */
    call sputc

    /* ---- A20 (port cepat 0x92) ---- */
    in $0x92, %al
    or $0x02, %al
    and $0xFE, %al
    out %al, $0x92

    /* ---- GDT di memori rendah 0x500: null,kcode(0x08),kdata(0x10) ---- */
    movl $0x0000FFFF, 0x508     /* kcode: base=0, limit=0xFFFFF, 0xAF9A */
    movl $0x00AF9A00, 0x50C
    movl $0x0000FFFF, 0x510     /* kdata: 0xCF92                        */
    movl $0x00CF9200, 0x514
    movw $0x17, 0x518           /* GDTR.limit = 23                      */
    movl $0x00000500, 0x51A     /* GDTR.base = 0x500                    */
    lgdt 0x518

    /* ---- masuk protected mode ---- */
    mov %cr0, %eax
    or $0x01, %al
    mov %eax, %cr0
    ljmp $0x08, $pm32

/* ================= protected mode 32-bit ================= */
    .code32
pm32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov $0x7C00, %esp

    mov $'p', %al           /* marker: pmode            */
    call sputc32

    /* ---- salin image kernel 0x10000 -> 0x100000 (256 KB = 0x8000 dword) ---- */
    mov $0x10000, %esi
    mov $0x100000, %edi
    mov $0x8000, %ecx
    cld
    rep movsl

    /* ---- tabel halaman: identity 1 GB, halaman 2 MB, P|RW|US|PS ---- */
    mov $0x1000, %edi       /* nolkan PML4+PDPT+PD (0x1000..0x3FFF) */
    xor %eax, %eax
    mov $0xC00, %ecx
    rep stosl
    movl $0x2003, 0x1000    /* PML4[0] -> PDPT                      */
    movl $0x3003, 0x2000    /* PDPT[0] -> PD                        */
    mov $0x3000, %edi
    mov $0x87, %eax         /* P|RW|US|PS, alamat 0                 */
    mov $0x200, %ecx        /* 512 entri x 2 MB = 1 GB              */
1:
    movl %eax, (%edi)
    add $0x200000, %eax
    add $4, %edi
    loop 1b
    mov $0x1000, %eax
    mov %eax, %cr3

    /* ---- EFER.LME lalu CR0.PG|PAE ---- */
    mov $0xC0000080, %ecx   /* IA32_EFER          */
    rdmsr
    or $0x100, %eax         /* LME                */
    wrmsr
    mov %cr0, %eax
    or $0x80010000, %eax    /* PG | PAE           */
    mov %eax, %cr0
    ljmp $0x08, $lm64

/* ================= long mode 64-bit ================= */
    .code64
lm64:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov $0x200000, %esp     /* stack sementara (kernel juga pakai) */
    mov $'l', %al           /* marker: long mode    */
    call sputc64
    mov $0x100000, %rax
    jmp *%rax               /* -> kstart64          */
fail64:
    hlt
    jmp fail64

/* ---------------- serial putc (16-bit) ----------------
 * AL = karakter; tunggu THR kosong (LSR bit 5) lalu kirim. */
sputc:
    mov %al, %bl
    mov $0x3FD, %dx         /* LSR                  */
1:
    in %dx, %al
    test $0x20, %al
    jz 1b
    mov %bl, %al
    mov $0x3F8, %dx
    out %al, %dx
    ret

/* ---------------- serial putc (32-bit) ---------------- */
sputc32:
    mov %al, %bl
    mov $0x3FD, %edx
2:
    in %dx, %al
    test $0x20, %al
    jz 2b
    mov %bl, %al
    mov $0x3F8, %edx
    out %al, %dx
    ret

/* ---------------- serial putc (64-bit) ---------------- */
sputc64:
    mov %al, %bl
    mov $0x3FD, %edx
3:
    in %dx, %al
    test $0x20, %al
    jz 3b
    mov %bl, %al
    mov $0x3F8, %edx
    out %al, %dx
    ret

/* ---------------- padding + signature ---------------- */
    . = _boot + 510
    .word 0xAA55
