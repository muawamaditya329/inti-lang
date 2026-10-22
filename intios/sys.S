/* sys.S — jalur masuk syscall & transisi ring untuk IntiOS
 *
 * SYSCALL/SYSRET nyata:
 *   - kernel memasang IA32_LSTAR = sys_entry, IA32_STAR, EFER.SCE (cpu.c)
 *   - program user (blob Inti) memakai instruksi `syscall` dengan nomor
 *     yang SAMA dengan Linux: read=0, write=1, exit=60
 *   - SYSCALL: RCX=RIP user, R11=RFLAGS user, RSP TIDAK diubah
 *
 * enter_user(): dari kernel (CPL0) -> CPL3 via iretq (rip 0x400000).
 * user_die()  : dipanggil dispatcher saat exit — pulih frame enter_user
 *               sehingga shell berjalan lagi seolah tidak terjadi apa-apa.
 * exc_*       : stub exception (#DE #UD #GP #PF) -> lapor ke serial ->
 *               kembali ke shell.
 */
    .code64
    .text

/* ---------------- state tersimpan (.bss) ---------------- */
    .bss
    .align 8
    .globl g_u_rsp, g_u_rcx, g_u_r11, g_saved_rsp, g_exit_code
g_u_rsp:     .quad 0        /* RSP user saat syscall       */
g_u_rcx:     .quad 0        /* RIP user (untuk SYSRET)     */
g_u_r11:     .quad 0        /* RFLAGS user (untuk SYSRET)  */
g_saved_rsp: .quad 0        /* frame enter_user            */
g_exit_code: .skip 8        /* kode exit program terakhir  */
    .text

/* ---------------- SYSCALL entry (LSTAR) ----------------
 * Di sini masih di RSP user; simpan dulu sebelum pindah stack. */
    .globl sys_entry
sys_entry:
    mov %rsp, g_u_rsp(%rip)
    mov %rcx, g_u_rcx(%rip)
    mov %r11, g_u_r11(%rip)
    mov $0x1FF000, %esp          /* stack syscall kernel */

    push %r9                     /* args user: rdi,rsi,rdx,r10,r8,r9 */
    push %r8
    push %r10
    push %rdx
    push %rsi
    push %rdi
    push %rbx                    /* callee-saved wajib dijaga */
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rax, %rdi               /* arg0 = nomor syscall  */
    lea 48(%rsp), %rsi           /* arg1 = array 6 arg    */
    call sys_dispatch            /* C: int64 sys_dispatch(n, args) */

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    pop %rdi
    pop %rsi
    pop %rdx
    pop %r10
    pop %r8
    pop %r9

    mov g_u_rsp(%rip), %rsp      /* kembali ke stack user */
    mov g_u_rcx(%rip), %rcx
    mov g_u_r11(%rip), %r11
    sysretq

/* ---------------- masuk user: void enter_user(u64 rip, u64 base, u64 size)
 * ABI program IntiOS: RDI = basis arena, RSI = ukuran arena. ---------- */
    .globl enter_user
enter_user:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, g_saved_rsp(%rip)

    mov %rdi, %r8                /* r8 = rip user         */
    mov %rsi, %r9                /* r9 = basis arena      */
    mov %rdx, %r10               /* r10 = ukuran arena    */

    xor %eax, %eax
    xor %ecx, %ecx
    xor %edx, %edx
    xor %ebx, %ebx
    xor %ebp, %ebp
    xor %esi, %esi
    xor %r11d, %r11d
    xor %r12d, %r12d
    xor %r13d, %r13d
    xor %r14d, %r14d
    xor %r15d, %r15d
    mov %r9, %rdi                /* RDI = basis arena     */
    mov %r10, %rsi               /* RSI = ukuran arena    */

    push $0x23                   /* SS user               */
    push $0x1000000              /* RSP user (USTACK_TOP) */
    push $0x202                  /* RFLAGS: IF=1 (PIC dimask penuh) */
    push $0x1B                   /* CS user (0x18|RPL3)   */
    push %r8                     /* RIP user              */
    iretq

/* ---------------- keluar dari user (exit / exception) ----------------
 * Pulih semua register yang disimpan enter_user, kembali ke pemanggil
 * enter_user di shell dengan RAX=0. Kode exit dibaca dari g_exit_code. */
    .globl user_die
user_die:
    mov g_saved_rsp(%rip), %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    xor %eax, %eax
    ret

/* ---------------- exception stubs ----------------
 * Frame CPL3 -> CPL0 via IDT gate: SS,RSP,RFLAGS,CS,RIP (+err utk vektor
 * tertentu). Stub menambah [vektor] di atas dan (dummy) [error code]
 * sehingga layout seragam: [rsp]=vektor, [rsp+8]=err, [rsp+16]=RIP user.
 * exc_report(rdi=rsp) membaca itu, mencetak laporan, lalu user_die(). */
.macro EXC_VEC nm vec has_err
    .globl exc_\nm
exc_\nm:
    .if \has_err
        push $\vec               /* [rsp]=vektor, err asli CPU di [rsp+8] */
    .else
        push $0                  /* dummy error code        */
        push $\vec               /* vektor                  */
    .endif
    mov %rsp, %rdi
    call exc_report              /* tidak pernah kembali    */
    add $16, %rsp
    iretq
.endm

EXC_VEC de  0  0                /* #DE 0: divide error (tanpa err)  */
EXC_VEC ud  6  0                /* #UD 6: opcode tak sah            */
EXC_VEC gp  13 1                /* #GP 13: proteksi (dengan err)    */
EXC_VEC pf  14 1                /* #PF 14: page fault (dengan err)  */
