[BITS 16]
[ORG 0x7C00]

KERNEL_SECTORS equ 127
STAGE_SEG equ 0x07E0
STAGE_OFF equ 0x0000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    ; 1. Charger le noyau
    call load_kernel

    ; 2. Récupérer E820 (Stocké à 0x6000)
    mov di, 0x6028          ; Décalage après le header de boot_info_t
    xor ebx, ebx
    xor bp, bp              ; Compteur d'entrées
.get_mmap:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    jc .mmap_done
    add di, 24
    inc bp
    test ebx, ebx
    jnz .get_mmap
.mmap_done:
    mov [0x6020], bp        ; Sauvegarde mmap_count dans boot_info_t

    ; 3. Transition mode protégé
    call enable_a20
    lgdt [gdt32_desc]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm32_entry

load_kernel:
    mov byte [dap], 0x10
    mov word [dap+2], KERNEL_SECTORS
    mov word [dap+4], STAGE_OFF
    mov word [dap+6], STAGE_SEG
    mov dword [dap+8], 1
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jc .error
    ret
.error: hlt

enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

align 4
dap: times 16 db 0
boot_drive db 0

gdt32:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt32_end:
gdt32_desc: dw gdt32_end - gdt32 - 1
            dd gdt32

[BITS 32]
pm32_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000
    
    ; Copier noyau
    mov esi, 0x7E00
    mov edi, 0x100000
    mov ecx, (KERNEL_SECTORS * 512) / 4
    rep movsd

    call setup_paging
    lgdt [gdt64_desc]
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    jmp 0x08:lm64_entry

setup_paging:
    mov edi, 0x1000, 
    xor eax, eax, 
    mov ecx, 0x3000/4
    rep stosd
    mov dword [0x1000], 0x2003
    mov dword [0x2000], 0x3003
    mov edi, 0x3000, 
    mov eax, 0x83, 
    mov ecx, 512
.fill: 
    mov [edi], eax, 
    add eax, 0x200000, 
    add edi, 8, 
    loop .fill
    mov eax, 0x1000, 
    mov cr3, eax, 
    ret

[BITS 64]

    ; 4. Entrée 64-bit
gdt64:
    dq 0                ; NULL descriptor

    ; Code 64-bit
    dq 0x00CF9A000000FFFF

    ; Data 64-bit
    dq 0x00CF92000000FFFF
gdt64_end:

gdt64_desc:
    dw gdt64_end - gdt64 - 1
    dq gdt64

    ; 5. Fonction d'entrée 64-bit
lm64_entry:
    mov rdi, 0x6000         ; RDI pointe vers notre structure boot_info
    call 0x100000           ; Saut au noyau
.hang: hlt
jmp .hang

times 510 - ($ - $$) db 0
dw 0xAA55