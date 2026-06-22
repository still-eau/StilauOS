; bootloader x86_64
;
; Sequence:
;   1. [Real mode]     Print "Booting..."
;   2. [Real mode]     Read kernel sectors from disk → staging at 0x7E00
;   3. [Real mode]     Enable A20 + enter protected mode
;   4. [Protected 32]  Copy 0x7E00 → 0x100000 (before paging)
;   5. [Protected 32]  Setup page tables, enable long mode
;   6. [Long mode 64]  Jump to kernel at 0x100000

[BITS 16]
[ORG 0x7C00]

; Number of kernel sectors to read.
; Image is padded to KERNEL_SECTORS+1 sectors in the build script.
KERNEL_SECTORS equ 127          ; read 127 sectors = 63.5 KB

; Staging buffer in real mode: 0x07E0:0x0000 = physical 0x7E00
; (right after the MBR, safe and well below 640 KB)
STAGE_SEG equ 0x07E0
STAGE_OFF equ 0x0000

; ===
; Entry point
; ===

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; stack grows down from MBR load address

    mov fs, ax
    mov gs, ax

    ; BIOS puts the boot drive number in DL — save it before anything touches it
    mov [boot_drive], dl

    sti

    mov si, msg_boot
    call print_str

    ; Load the kernel sectors from disk to the staging area
    call load_kernel

    ; Enable A20 line via fast gate (port 0x92)
    call enable_a20

    ; Enter protected mode
    lgdt [gdt32_desc]

    cli
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    jmp 0x08:pm32_entry


; ===
; print_str — print null-terminated string via BIOS INT 10h / AH=0Eh
; SI = pointer to string
; ===

print_str:
    mov ah, 0x0E
.lp:
    lodsb
    test al, al
    jz   .done
    int  0x10
    jmp  .lp
.done:
    ret


; ===
; enable_a20 — fast A20 enable via System Control Port A (0x92)
; ===

enable_a20:
    in  al, 0x92
    or  al, 2
    and al, 0xFE        ; bit 0 = 0: avoid triple-fault reset
    out 0x92, al
    ret


; ===
; load_kernel — read KERNEL_SECTORS sectors from disk to staging area
;
; Uses INT 13h / AH=42h (LBA Extended Read) with a Disk Address Packet.
; The DAP buffer is 0x07E0:0x0000 = physical 0x7E00.
;
; LBA 1 = first sector after the MBR.
; ===

load_kernel:
    ; --- Fill the Disk Address Packet ---
    mov byte  [dap],      0x10  ; DAP size = 16 bytes
    mov byte  [dap + 1],  0x00  ; reserved
    mov word  [dap + 2],  KERNEL_SECTORS
    mov word  [dap + 4],  STAGE_OFF   ; buffer offset
    mov word  [dap + 6],  STAGE_SEG   ; buffer segment
    mov dword [dap + 8],  1           ; LBA low  (start at sector 1)
    mov dword [dap + 12], 0           ; LBA high

    ; --- Call Extended Read ---
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13

    jc .error
    ret

.error:
    mov si, msg_err
    call print_str
    cli
    hlt


; ===
; Data (inside the 512-byte MBR)
; ===

boot_drive  db 0

; Disk Address Packet (16 bytes, filled at runtime)
align 4
dap:
    times 16 db 0

msg_boot db "Booting...", 0x0D, 0x0A, 0
msg_err  db "Disk read error!", 0x0D, 0x0A, 0


; ===
; GDT 32-bit: flat code + data (base 0, limit 4 GB)
; ===

gdt32:
gdt32_null:     dq 0
gdt32_code:     dw 0xFFFF, 0x0000, 0x9A00, 0x00CF    ; present, ring0, code, 32-bit
gdt32_data:     dw 0xFFFF, 0x0000, 0x9200, 0x00CF    ; present, ring0, data, 32-bit
gdt32_end:

gdt32_desc:
    dw gdt32_end - gdt32 - 1
    dd gdt32


; ===========================================================================
; 32-BIT PROTECTED MODE
; ===========================================================================

[BITS 32]

pm32_entry:
    ; Reload segment registers with the flat data selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000            ; temporary stack below VGA

    ; -------------------------------------------------------------------
    ; Copy the kernel from staging (0x7E00) to its link address (0x100000)
    ; Paging is still OFF at this point — we access physical memory directly.
    ; -------------------------------------------------------------------
    cld
    mov esi, 0x7E00
    mov edi, 0x100000
    mov ecx, (KERNEL_SECTORS * 512) / 4    ; copy in 32-bit dwords
    rep movsd

    ; Build minimal page tables for long mode (identity map 0–1 GB)
    call setup_paging

    ; Load the 64-bit GDT
    lgdt [gdt64_desc]

    ; Enable PAE (Physical Address Extension) — required for long mode
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; Set LME (Long Mode Enable) in IA32_EFER MSR
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; Enable paging → activates long mode (PG=1 with LME=1)
    mov eax, cr0
    or  eax, (1 << 31)
    mov cr0, eax

    ; Far jump to reload CS with the 64-bit code selector (0x08)
    jmp 0x08:lm64_entry


; ===
; setup_paging — identity-map the first 1 GB using 2 MB huge pages
;
; PML4  @ 0x1000
; PDPT  @ 0x2000
; PD    @ 0x3000
; ===

setup_paging:
    ; Zero 0x1000 – 0x4FFF (4 × 4 KB tables)
    mov edi, 0x1000
    xor eax, eax
    mov ecx, 0x3000 / 4
    rep stosd

    ; PML4[0] → PDPT at 0x2000 (present + writable)
    mov dword [0x1000], 0x2003

    ; PDPT[0] → PD at 0x3000 (present + writable)
    mov dword [0x2000], 0x3003

    ; PD: 512 entries, each a 2 MB huge page (PS=1, W=1, P=1 → 0x83)
    mov edi, 0x3000
    mov eax, 0x83               ; bits: huge page | writable | present
    mov ecx, 512
.fill:
    mov [edi], eax
    add eax, 0x200000           ; next 2 MB frame
    add edi, 8
    cmp eax, 0x40000000         ; stop after 1 GB (512 × 2 MB)
    jl  .fill

    ; Point CR3 to the PML4
    mov eax, 0x1000
    mov cr3, eax
    ret


; ===
; GDT 64-bit: code (L=1, D=0) + data
; ===

gdt64:
gdt64_null:     dq 0
gdt64_code:     dw 0, 0, 0x9A00, 0x0020     ; P=1, DPL=0, code, L=1, D=0
gdt64_data:     dw 0, 0, 0x9200, 0x0000     ; P=1, DPL=0, data
gdt64_end:

gdt64_desc:
    dw gdt64_end - gdt64 - 1
    dd gdt64
    dd 0                        ; upper 32 bits of base (0 — low memory)


; ===========================================================================
; 64-BIT LONG MODE
; ===========================================================================

[BITS 64]

lm64_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000            ; stack for kernel entry

    ; Jump to the kernel entry point now safely copied to 0x100000
    call 0x100000

.hang:
    cli
    hlt
    jmp .hang


; ===========================================================================
; MBR padding and boot signature
; ===========================================================================

times 510 - ($ - $$) db 0
dw 0xAA55