; isr_stubs.asm - Assembly stubs for all CPU exceptions and hardware IRQs
;
; Pattern for exceptions WITHOUT error code pushed by CPU:
;   push 0          (dummy error code to keep stack frame uniform)
;   call C handler
;   add rsp, 8     (clean dummy)
;   iretq
;
; Pattern for exceptions WITH error code pushed by CPU:
;   (error code already on stack)
;   call C handler with error_code in rdi
;   add rsp, 8     (discard error code)
;   iretq
;
; All stubs save/restore the full GP register set (System V AMD64 ABI).
;
; ============================================================================
; irq0 / isr_yield use a CONTEXT-SWITCHING pattern:
;   1. PUSH_ALL to save all GP registers onto the CURRENT thread stack.
;   2. Pass RSP (= current context pointer) in RDI to the C handler.
;   3. The C handler saves that RSP in the TCB, picks the next thread,
;      and returns the next thread's RSP in RAX.
;   4. Load RAX into RSP — we are now on the NEW thread's stack.
;   5. POP_ALL restores its registers.
;   6. IRETQ restores RIP, CS, RFLAGS from the new thread's saved frame.
; ============================================================================

[BITS 64]

; ============================================================================
; Macro helpers
; ============================================================================

; Save all caller- and callee-saved GP registers (except RSP which the CPU
; handles automatically as part of the interrupt frame).
%macro PUSH_ALL 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_ALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

; Stub for exceptions WITHOUT an error code
; Arg1: stub name (exported), Arg2: C handler name
%macro ISR_NO_ERR 2
global %1
extern %2
%1:
    PUSH_ALL
    call %2
    POP_ALL
    iretq
%endmacro

; Stub for exceptions WITH an error code pushed by the CPU.
; The error code is already on the stack above the iret frame.
; We pass it in rdi (first arg, SysV AMD64) and clean it after the call.
%macro ISR_ERR 2
global %1
extern %2
%1:
    PUSH_ALL
    ; error code is at [rsp + 15*8] (above the 15 pushed regs)
    mov rdi, [rsp + 15*8]
    call %2
    POP_ALL
    add rsp, 8      ; discard error code
    iretq
%endmacro

; IRQ stub: acknowledge PIC, call C handler, iretq
%macro IRQ_STUB 2
global %1
extern %2
%1:
    PUSH_ALL
    call %2
    POP_ALL
    iretq
%endmacro

; ============================================================================
; CPU Exception stubs (vectors 0 - 30)
; ============================================================================

section .text

ISR_NO_ERR  isr_div_error,      isr_div_error_handler
ISR_NO_ERR  isr_debug,          isr_debug_handler
ISR_NO_ERR  isr_nmi,            isr_nmi_handler
ISR_NO_ERR  isr_breakpoint,     isr_breakpoint_handler
ISR_NO_ERR  isr_overflow,       isr_overflow_handler
ISR_NO_ERR  isr_bound_range,    isr_bound_range_handler
ISR_NO_ERR  isr_invalid_opcode, isr_invalid_opcode_handler
ISR_NO_ERR  isr_device_na,      isr_device_na_handler
ISR_ERR     isr_double_fault,   isr_double_fault_handler
ISR_NO_ERR  isr_scheduler,      isr_scheduler_handler
ISR_NO_ERR  isr_fs,             isr_fs_handler
ISR_NO_ERR  isr_default,        isr_default_handler
ISR_ERR     isr_invalid_tss,    isr_invalid_tss_handler
ISR_ERR     isr_segment_np,     isr_segment_np_handler
ISR_ERR     isr_stack_fault,    isr_stack_fault_handler
ISR_ERR     isr_gpf,            isr_gpf_handler
ISR_ERR     isr_page_fault,     isr_page_fault_handler
ISR_NO_ERR  isr_fpu,            isr_fpu_handler
ISR_ERR     isr_alignment,      isr_alignment_handler
ISR_NO_ERR  isr_machine_check,  isr_machine_check_handler
ISR_NO_ERR  isr_simd_fpu,       isr_simd_fpu_handler
ISR_NO_ERR  isr_virtualization, isr_virtualization_handler
ISR_ERR     isr_security,       isr_security_handler

; ============================================================================
; Hardware IRQ stubs (vectors 32-47, remapped PIC)
; ============================================================================

IRQ_STUB    irq1,   irq1_handler    ; vector 33 - Keyboard
IRQ_STUB    irq12,  irq12_handler   ; vector 44 - PS/2 Mouse

; ============================================================================
; IRQ0 - PIT Timer (context-switching stub)
;
; irq0_tick_handler(uint64_t current_rsp) -> uint64_t next_rsp
;   - Called with the current RSP (pointing to PUSH_ALL context) in RDI.
;   - Returns the RSP of the thread that should run next in RAX.
; ============================================================================

global irq0
extern irq0_tick_handler

irq0:
    PUSH_ALL                ; save all GP regs onto CURRENT thread's stack
    mov  rdi, rsp           ; pass current RSP as argument to C handler
    call irq0_tick_handler  ; returns next RSP in RAX
    mov  rsp, rax           ; switch to the new thread's stack
    POP_ALL                 ; restore the new thread's GP registers
    iretq                   ; restore RIP, CS, RFLAGS (and RSP if ring change)

; ============================================================================
; INT 0x81 - Cooperative yield (context-switching stub)
;
; yield_handler(uint64_t current_rsp) -> uint64_t next_rsp
; ============================================================================

global isr_yield
extern yield_handler

isr_yield:
    PUSH_ALL
    mov  rdi, rsp
    call yield_handler
    mov  rsp, rax
    POP_ALL
    iretq
