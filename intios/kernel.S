/* kernel.S — titik masuk 64-bit kernel IntiOS
 *
 * Boot sector melompat ke sini (0x100000). Tugas:
 *   1. pasang kernel stack (0x200000, tumbuh ke bawah),
 *   2. nolkan .bss (loader flat binary tidak menjamin nol),
 *   3. panggil kmain() (C, ABI SysV).
 */
    .code64
    .section .text.start, "ax"
    .globl kstart64

kstart64:
    mov $0x200000, %rsp
    xor %ebp, %ebp

    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx
    shr $3, %rcx
    xor %eax, %eax
    rep stosq

    call kmain

1:  hlt
    jmp 1b
