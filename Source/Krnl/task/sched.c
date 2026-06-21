// sched.c - StilauSched: preemptive Round-Robin + cooperative scheduler
//
// Architecture overview
// =====================
// Every thread has a private kernel stack.  When a context switch is
// triggered (by a PIT tick via irq0_tick_handler, or voluntarily via
// yield_handler), the ISR assembly stub has already pushed all general-
// purpose registers with PUSH_ALL onto the *current* stack.  The C handler
// receives that RSP as its first argument, saves it into the current TCB,
// picks the next READY thread, and returns the new thread's RSP.  The stub
// then pops all registers with POP_ALL and executes IRETQ, which naturally
// restores RIP, CS, RFLAGS.
//
// Thread initial stack layout (built by thread_create)
// =====================================================
//   [high addr]
//     &thread_exit   <- return address  (if entry ever ret-s instead of exiting)
//     [IRETQ frame]
//       rip          <- entry point
//       cs           <- 0x08  (kernel code segment)
//       rflags       <- 0x202 (interrupts enabled, IF=1)
//       rsp          <- points just above this frame on the stack
//       ss           <- 0x10  (kernel data segment)
//     [PUSH_ALL frame — 15 regs, all zeroed except RDI = arg]
//       r15 .. r8, rbp, rdi, rsi, rdx, rcx, rbx, rax
//   [low addr]  <- initial saved RSP (stored in TCB)

#include "sched.h"
#include "../mem/krnl_mm.h"
#include "../../Drivers/console.h"
#include "../../Drivers/pit.h"
#include "../../Drivers/pic.h"

// ---------------------------------------------------------------------------
// Local helpers (no stdlib in freestanding kernel)
// ---------------------------------------------------------------------------

static void s_memset(void *dst, int val, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)val;
}

static inline int s_strncmp(const char *s1, const char *s2, size_t n)
{
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    return (n == 0) ? 0 : (*(const unsigned char *)s1 - *(const unsigned char *)s2);
}

static void s_strncpy(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

// ---------------------------------------------------------------------------
// Global scheduler state
// ---------------------------------------------------------------------------

static thread_t   g_threads[SCHED_MAX_THREADS];  // TCB pool
static int        g_current    = 0;               // Index of running thread
static bool       g_running    = false;           // Scheduler initialised?
static uint32_t   g_next_id    = 1;              // Monotonically increasing TID
static uint32_t   g_ticks_this_slice = 0;        // Tick counter within current quantum

// ---------------------------------------------------------------------------
// Internal: allocate a free TCB slot
// ---------------------------------------------------------------------------

static int alloc_slot(void)
{
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
    {
        if (g_threads[i].state == THREAD_DEAD && g_threads[i].id == 0)
        {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Internal: pick the next READY thread (Round-Robin with priority bias)
//
// We scan from [current+1] wrapping around, and choose the first READY
// thread whose priority <= best so far.  An idle thread (lowest priority)
// always exists, so we never return -1 after sched_init.
// ---------------------------------------------------------------------------

static int pick_next(void)
{
    // Wake up any sleeping threads first.
    uint64_t now = pit_get_ticks();
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
    {
        if (g_threads[i].state == THREAD_SLEEPING &&
            g_threads[i].sleep_until <= now)
        {
            g_threads[i].state = THREAD_READY;
        }
    }

    // Simple Round-Robin: find next READY thread starting after current.
    int best     = -1;
    uint32_t best_prio = 256; // Sentinel higher than any priority

    for (int step = 1; step <= SCHED_MAX_THREADS; step++)
    {
        int idx = (g_current + step) % SCHED_MAX_THREADS;
        if (g_threads[idx].state == THREAD_READY &&
            g_threads[idx].priority <= best_prio)
        {
            best      = idx;
            best_prio = g_threads[idx].priority;
            break; // Pure round-robin: first ready wins
        }
    }

    // If nothing found (should not happen after init — idle thread is always ready)
    if (best == -1)
    {
        best = g_current; // Stay on current thread
    }

    return best;
}

// ---------------------------------------------------------------------------
// Internal: perform the actual context switch
// Returns the RSP of the next thread to execute.
// ---------------------------------------------------------------------------

static uint64_t do_switch(uint64_t current_rsp)
{
    // Save the current stack pointer.
    g_threads[g_current].rsp   = current_rsp;
    if (g_threads[g_current].state == THREAD_RUNNING)
    {
        g_threads[g_current].state = THREAD_READY;
    }

    // Pick next thread.
    int next = pick_next();

    g_threads[next].state    = THREAD_RUNNING;
    g_threads[next].ticks_used = 0;
    g_current = next;
    g_ticks_this_slice = 0;

    return g_threads[next].rsp;
}

// ---------------------------------------------------------------------------
// irq0_tick_handler — called from PIT IRQ0 stub every tick
// ---------------------------------------------------------------------------

uint64_t irq0_tick_handler(uint64_t current_rsp)
{
    irq0_pit_tick();          // increment tick_count + ISR counter in pit.c
    pic_send_eoi(0);          // acknowledge the PIC (IRQ line 0)

    if (!g_running)
    {
        return current_rsp;
    }

    g_threads[g_current].ticks_used++;
    g_ticks_this_slice++;

    // Only switch when the current thread's time slice expires.
    if (g_ticks_this_slice < SCHED_TIME_SLICE)
    {
        return current_rsp; // Stay on current thread
    }

    return do_switch(current_rsp);
}

// ---------------------------------------------------------------------------
// yield_handler — called from INT 0x81 stub
// ---------------------------------------------------------------------------

uint64_t yield_handler(uint64_t current_rsp)
{
    if (!g_running)
    {
        return current_rsp;
    }
    return do_switch(current_rsp);
}

// ---------------------------------------------------------------------------
// thread_exit — mark thread DEAD and yield
// ---------------------------------------------------------------------------

void thread_exit(const char *name)
{
    // Disable interrupts while we modify state, then re-enable with the yield.
    __asm__ volatile ("cli");

    // Find thread by name
    thread_t *t = NULL;
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
    {
        if (g_threads[i].state != THREAD_DEAD && g_threads[i].id != 0)
        {
            if (s_strncmp(g_threads[i].name, name, SCHED_THREAD_NAME_MAX) == 0)
            {
                t = &g_threads[i];
                break;
            }
        }
    }

    if (t)
    {
        t->state = THREAD_DEAD;
        t->id    = 0;
        kprintf("Thread '%s' killed.\n", name);
    }
    else
    {
        kprintf("Thread '%s' not found.\n", name);
    }
    
    __asm__ volatile ("sti");
    thread_yield();

    // Free the stack pages now: we cannot kfree the current stack while we
    // are still standing on it.  Instead, mark it for reclamation and let
    // the next thread scheduler clean it up.  Here we just yield; the
    // stack is no longer referenced after IRETQ loads the new RSP.
    //
    // NOTE: For safety we purposely do NOT free stack pages here — the
    // kernel stack allocator reserves them permanently per thread in this
    // simple implementation.  In a production kernel you would use a
    // "zombie" reaper or delayed free here.
    __asm__ volatile ("sti");

    // Cooperatively hand off the CPU; this thread will never be scheduled again.
    thread_yield();

    // Should never reach here.
    for (;;) { __asm__ volatile ("hlt"); }
}

// ---------------------------------------------------------------------------
// thread_yield — cooperative yield via soft interrupt
// ---------------------------------------------------------------------------

void thread_yield(void)
{
    if (!g_running) return;
    __asm__ volatile ("int $0x81");
}

// ---------------------------------------------------------------------------
// thread_sleep — sleep for approximately `ms` milliseconds
// ---------------------------------------------------------------------------

void thread_sleep(const char *name, uint32_t ms)
{
    if (!g_running) return;
    __asm__ volatile ("cli");

    // Find thread by name
    thread_t *t = NULL;
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
    {
        if (g_threads[i].state != THREAD_DEAD && g_threads[i].id != 0)
        {
            if (s_strncmp(g_threads[i].name, name, SCHED_THREAD_NAME_MAX) == 0)
            {
                t = &g_threads[i];
                break;
            }
        }
    }

    if (t)
    {
        uint64_t now = pit_get_ticks();
        t->sleep_until = now + (uint64_t)ms;
        t->state       = THREAD_SLEEPING;
        kprintf("Thread %s put to sleep for %d ms\n", name, ms);
    }
    else
    {
        kprintf("Thread %s not found\n", name);
    }
    
    __asm__ volatile ("sti");
    thread_yield();
}

// ---------------------------------------------------------------------------
// thread_create — spawn a new kernel thread
// ---------------------------------------------------------------------------

// Idle thread entry point.
void idle_thread(void *arg)
{
    (void)arg;
    for (;;)
    {
        __asm__ volatile ("hlt"); // Execute HLT: CPU sleeps until next IRQ.
    }
}

uint32_t thread_create(void (*entry)(void *), void *arg,
                       const char *name, uint32_t priority)
{
    __asm__ volatile ("cli");

    int slot = alloc_slot();
    if (slot < 0)
    {
        __asm__ volatile ("sti");
        return 0;
    }

    // Allocate a kernel stack.
    size_t stack_size = SCHED_STACK_PAGES * 4096;
    uint8_t *stack = (uint8_t *)krnl_mm_alloc_pages(SCHED_STACK_PAGES);
    if (!stack)
    {
        __asm__ volatile ("sti");
        return 0;
    }

    s_memset(stack, 0, stack_size);

    // Stack grows downward. Start at the top.
    uint64_t *sp = (uint64_t *)(stack + stack_size);

    // Push a fake return address that leads to thread_exit, so that if the
    // thread's entry function ever returns normally (falls off the end)
    // instead of calling thread_exit() itself, execution lands safely in
    // thread_exit() instead of jumping to whatever garbage value happens
    // to be sitting on the stack.
    //
    // No separate "alignment padding" word before this push: it served no
    // purpose (RSP % 16 == 8 here either way, as required by the SysV
    // x86-64 ABI at a function's first instruction, since `stack +
    // stack_size` is page-, hence 16-byte, aligned) and it does not appear
    // in this file's own stack-layout diagram above thread_create.
    sp--;
    *sp = (uint64_t)(uintptr_t)thread_exit;
    uint64_t *ret_addr_slot = sp;

    // Build the IRETQ hardware frame (pushed in reverse order of pop):
    //   ss, rsp, rflags, cs, rip
    // IMPORTANT: the RSP field below is the value the CPU will actually
    // load into RSP when IRETQ executes (x86-64 long mode always pops
    // SS:RSP, even without a privilege change). It must point at
    // ret_addr_slot -- one slot above the fake return address -- NOT at
    // some address computed from the current `sp` while still in the
    // middle of building this frame (that was the bug: it pointed at the
    // SS slot itself, leaving every thread's RSP off by 8 bytes the
    // instant it started running).
    sp--; *sp = (uint64_t)0x10;                           // SS  (kernel data)
    sp--; *sp = (uint64_t)(uintptr_t)ret_addr_slot;       // RSP (-> fake return addr slot)
    sp--; *sp = (uint64_t)0x202;                          // RFLAGS: IF=1, reserved=1
    sp--; *sp = (uint64_t)0x08;                           // CS  (kernel code)
    sp--; *sp = (uint64_t)(uintptr_t)entry;               // RIP (entry point)

    // Build PUSH_ALL software context (matches macro in isr_stubs.asm order).
    // PUSH_ALL: rax, rbx, rcx, rdx, rsi, rdi, rbp, r8..r15
    // POP_ALL reversal: r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax
    // Push in PUSH_ALL order (what the stub pushes before calling us):
    sp--; *sp = 0;                                         // rax
    sp--; *sp = 0;                                         // rbx
    sp--; *sp = 0;                                         // rcx
    sp--; *sp = 0;                                         // rdx
    sp--; *sp = 0;                                         // rsi
    sp--; *sp = (uint64_t)(uintptr_t)arg;                 // rdi  <- first arg to entry
    sp--; *sp = 0;                                         // rbp
    sp--; *sp = 0;                                         // r8
    sp--; *sp = 0;                                         // r9
    sp--; *sp = 0;                                         // r10
    sp--; *sp = 0;                                         // r11
    sp--; *sp = 0;                                         // r12
    sp--; *sp = 0;                                         // r13
    sp--; *sp = 0;                                         // r14
    sp--; *sp = 0;                                         // r15

    // Populate the TCB.
    thread_t *t   = &g_threads[slot];
    t->rsp        = (uint64_t)(uintptr_t)sp;
    t->stack_base = stack;
    t->stack_size = stack_size;
    t->state      = THREAD_READY;
    t->sleep_until = 0;
    t->id         = g_next_id++;
    t->ticks_used = 0;
    t->priority   = priority;
    s_strncpy(t->name, name, SCHED_THREAD_NAME_MAX);

    __asm__ volatile ("sti");
    return t->id;
}

// ---------------------------------------------------------------------------
// sched_init — initialise the scheduler and register the boot thread
// ---------------------------------------------------------------------------

void sched_init(void)
{
    s_memset(g_threads, 0, sizeof(g_threads));

    // Slot 0 = the current (boot / kernel-main) thread.
    g_threads[0].rsp        = 0;          // Will be filled on first context switch.
    g_threads[0].stack_base = NULL;       // Boot stack — not heap-allocated.
    g_threads[0].stack_size = 0;
    g_threads[0].state      = THREAD_RUNNING;
    g_threads[0].id         = g_next_id++;
    g_threads[0].priority   = 0;         // Highest priority — kernel main.
    s_strncpy(g_threads[0].name, "kernel_main", SCHED_THREAD_NAME_MAX);

    g_current          = 0;
    g_ticks_this_slice = 0;
    g_running          = true;

    uint32_t idle_id = thread_create(idle_thread, NULL, "idle", 255);
    if (idle_id == 0)
    {
        k_serial_puts("[SCHED] FATAL: failed to create idle thread.\n");
    }

    k_serial_puts("[SCHED] StilauSched initialized.\n");
}

// ---------------------------------------------------------------------------
// sched_is_running
// ---------------------------------------------------------------------------

bool sched_is_running(void)
{
    return g_running;
}

// ---------------------------------------------------------------------------
// sched_current
// ---------------------------------------------------------------------------

thread_t *sched_current(void)
{
    return &g_threads[g_current];
}

// ---------------------------------------------------------------------------
// sched_thread_count — number of live (non-dead) threads
// ---------------------------------------------------------------------------

int sched_thread_count(void)
{
    int n = 0;
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
    {
        if (g_threads[i].state != THREAD_DEAD || g_threads[i].id != 0)
        {
            n++;
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
// sched_get_thread — return pointer to TCB at slot, or NULL if empty
// ---------------------------------------------------------------------------

const thread_t *sched_get_thread(int slot)
{
    if (slot < 0 || slot >= SCHED_MAX_THREADS) return NULL;
    if (g_threads[slot].state == THREAD_DEAD && g_threads[slot].id == 0) return NULL;
    return &g_threads[slot];
}

// ---------------------------------------------------------------------------
// scheduler_current_thread_id - Returns the ID of the current thread.
// ---------------------------------------------------------------------------

uint64_t scheduler_current_thread_id(void)
{
    if (!g_running) return 0;
    return g_threads[g_current].id;
}