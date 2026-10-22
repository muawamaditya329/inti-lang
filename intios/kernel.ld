/* kernel.ld — layout kernel IntiOS (ELF64 -> objcopy binary)
 *
 * Kernel dimuat boot sector di 0x100000. .text.start WAJIB paling awal
 * karena boot sector melompat langsung ke byte pertama image.
 */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kstart64)

SECTIONS
{
    . = 0x100000;

    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    . = ALIGN(16);
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(16);
    __bss_end = .;

    /DISCARD/ : {
        *(.eh_frame .eh_frame_hdr .note* .comment .gnu* .debug*)
    }
}
