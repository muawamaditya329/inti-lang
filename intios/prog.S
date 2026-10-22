/* prog.S — program Inti ter-embed di image kernel
 *
 * Blob .ibin dihasilkan inti-cc --target=intios (build.sh menjalankannya
 * SEBELUM file ini dirakit). Kernel menyalin blob ke 0x400000 lalu masuk
 * CPL3 lewat enter_user().
 */
    .section .rodata

    .globl prog_primes, prog_primes_end
prog_primes:   .incbin "build/primes.ibin"
prog_primes_end:

    .globl prog_fib, prog_fib_end
prog_fib:      .incbin "build/fib.ibin"
prog_fib_end:

    .globl prog_hello, prog_hello_end
prog_hello:    .incbin "build/hello.ibin"
prog_hello_end:

    .globl prog_fizzbuzz, prog_fizzbuzz_end
prog_fizzbuzz: .incbin "build/fizzbuzz.ibin"
prog_fizzbuzz_end:

    .globl prog_memory, prog_memory_end
prog_memory:   .incbin "build/memory.ibin"
prog_memory_end:

    .globl prog_echo, prog_echo_end
prog_echo:     .incbin "build/echo.ibin"
prog_echo_end:

/* ---------------- tabel program ---------------- */
    .globl prog_table
prog_table:
    .quad nm_hello
    .quad prog_hello
    .quad prog_hello_end - prog_hello

    .quad nm_primes
    .quad prog_primes
    .quad prog_primes_end - prog_primes

    .quad nm_fib
    .quad prog_fib
    .quad prog_fib_end - prog_fib

    .quad nm_fizzbuzz
    .quad prog_fizzbuzz
    .quad prog_fizzbuzz_end - prog_fizzbuzz

    .quad nm_memory
    .quad prog_memory
    .quad prog_memory_end - prog_memory

    .quad nm_echo
    .quad prog_echo
    .quad prog_echo_end - prog_echo

    .quad 0                       /* penanda akhir */

nm_hello:    .asciz "hello"
nm_primes:   .asciz "primes"
nm_fib:      .asciz "fib"
nm_fizzbuzz: .asciz "fizzbuzz"
nm_memory:   .asciz "memory"
nm_echo:     .asciz "echo"
