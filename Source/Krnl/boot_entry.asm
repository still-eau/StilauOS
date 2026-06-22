; boot_entry.asm
; Point d'entrée noyau 64 bits.
; Reçoit le pointeur boot_info dans RDI depuis le bootloader.

[BITS 64]

global _start
extern kernel_main

section .text

_start:
    mov rsp, stack_top
    call zero_bss

    ; Zero registers
    xor rbp, rbp
    xor rbx, rbx
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    call zero_bss

    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

; === zero_bss : Clean .bss ===
zero_bss:
    lea rdi, [rel bss_start]
    lea rcx, [rel bss_end]
    sub rcx, rdi
    xor al, al
    rep stosb
    ret

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

global bss_start
global bss_end
bss_start:
    resb 4096
bss_end: