; boot_entry.asm - first code executed by the kernel after the bootloader
; loaded in memory at 0x100000, called via call from long_mode_entry
; assembled with: nasm -f elf64 boot_entry.asm -o boot_entry.o

[BITS 64]

; export _start so that the linker can link with the rest of the kernel
global _start
extern kernel_main

; ===
; SECTION TEXT - executive code
; ===

section .text

_start:
    ; at this point we arrive from the bootloader, rsp points to 0x90000
    ; we replace it with a real stack at the top of the BSS
    mov rsp, stack_top

    ; reset general registers to start clean
    xor rbp, rbp
    xor rbx, rbx
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    ; clear the BSS first, C expects it to be zeroed
    call zero_bss

    ; pass boot info to the kernel: pointer to the boot_info struct
    lea rdi, [rel boot_info]
    call kernel_main

    ; kernel_main should never return, but just in case
.halt:
    cli
    hlt
    jmp .halt


; ===
; zero_bss - clear the entire BSS section
; ===
zero_bss:
    lea rdi, [rel bss_start]
    lea rcx, [rel bss_end]
    sub rcx, rdi            ; number of bytes to clear
    xor al, al
    rep stosb
    ret


; ===
; BOOT INFO - structure passed to the kernel to specify the boot state
; the bootloader could fill it, here we set default values
; ===

section .data

; each field is 8 bytes to stay aligned in 64-bit
boot_info:
    .magic          dq 0xB007B007        ; signature for the kernel to verify it was called correctly
    .mem_lower      dq 0x9FC00           ; available lower memory in bytes (typical value)
    .mem_upper      dq 0x7FE0000         ; available upper memory, to be replaced by actual detection
    .pml4_addr      dq 0x1000            ; address of the PML4 setup in the bootloader
    .vbe_mode_info  dq 0                 ; no graphics mode for now
    .cmdline        dq 0                 ; command line address, 0 if absent

    ; BIOS memory map region table (simplified E820 format)
    ; in practice the second stage bootloader fills it, here we set a dummy one
    .mmap_count     dq 2
    .pci_bar5       dq 0        ; <-- AJOUTE CETTE LIGNE ICI
    .mmap:
        ; region 0: conventional lower memory 0 -> 640 KB
        dq 0x0000000000000000   ; base
        dq 0x00000000000A0000   ; length
        dq 1                    ; type 1 = usable RAM

        ; region 1: upper memory starting at 1 MB
        dq 0x0000000000100000   ; base
        dq 0x0000000007F00000   ; length (~127 MB)
        dq 1


; ===
; SECTION BSS - uninitialized data and kernel stack
; ===

section .bss

; main kernel stack, 16 KB, aligned on 16 bytes as required by the ABI
align 16
stack_bottom:
    resb 16384
stack_top:

; expose these labels so zero_bss knows what to clear
global bss_start
global bss_end

bss_start:

; generic scratch zone, useful for early debug printfs
align 8
kernel_scratch:
    resb 4096

bss_end: