// sched.h - StilauSched preemptive / cooperative thread scheduler
//
// Context layout on the kernel stack (bottom = highest address):
//
//   [CPU hardware frame, pushed automatically on interrupt]
//     rip, cs, rflags, rsp_user (only on privilege change), ss
//
//   [Software context, pushed by PUSH_ALL macro before any ISR dispatch]
//     r15, r14, r13, r12, r11, r10, r9, r8
//     rbp, rdi, rsi, rdx, rcx, rbx, rax    <- RSP points here
//
// When a context switch occurs, RSP of the outgoing thread is saved in its
// TCB and the incoming thread's RSP is loaded.  IRETQ then restores RIP,
// CS, RFLAGS, and (on ring transitions) RSP/SS automatically.

#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Scheduler tunables
// ---------------------------------------------------------------------------

#define SCHED_MAX_THREADS    128       // Maximum simultaneous threads
#define SCHED_STACK_PAGES     4       // Pages per thread kernel stack (4 * 4096 = 16 KB)
#define SCHED_TIME_SLICE     10       // Default ticks per thread quantum (10 ms at 1000 Hz)
#define SCHED_THREAD_NAME_MAX 32      // Max characters in a thread name

// ---------------------------------------------------------------------------
// Thread States
// ---------------------------------------------------------------------------

typedef enum {
    THREAD_DEAD    = 0,   // Slot is free / thread has exited
    THREAD_READY   = 1,   // Ready to run, waiting for CPU
    THREAD_RUNNING = 2,   // Currently executing on CPU
    THREAD_SLEEPING = 3,  // Waiting for a timer to expire
    THREAD_BLOCKED = 4,   // Blocked on I/O or synchronisation primitive
} thread_state_t;

// ---------------------------------------------------------------------------
// Thread Control Block (TCB)
// ---------------------------------------------------------------------------

typedef struct thread {
    uint64_t         rsp;                          // Saved stack pointer (when not running)
    uint8_t         *stack_base;                   // Pointer to bottom of stack allocation
    size_t           stack_size;                   // Stack size in bytes
    thread_state_t   state;                        // Current scheduling state
    uint64_t         sleep_until;                  // Tick count when sleep expires
    uint32_t         id;                           // Unique thread ID (1-based; 0 = unused)
    uint32_t         ticks_used;                   // Ticks consumed this quantum
    uint32_t         priority;                     // Static priority (0 = highest, 255 = lowest)
    char             name[SCHED_THREAD_NAME_MAX];  // Human-readable name for debugging
} thread_t;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Called once in kernel_main AFTER krnl_mm_init and pit_init.
// Creates the idle thread and registers the current execution context as
// the "main" kernel thread.
void sched_init(void);

// Returns true if the scheduler has been initialised.
bool sched_is_running(void);

// Returns the number of currently active (non-dead) threads.
int sched_thread_count(void);

// Create a new kernel thread.
//   entry   - function to execute; receives `arg` in RDI.
//   arg     - opaque argument passed to entry.
//   name    - descriptive string for debugging.
//   priority - static priority (0-255; lower value = higher priority).
// Returns the thread ID (>0) or 0 on failure.
uint32_t thread_create(void (*entry)(void *), void *arg,
                       const char *name, uint32_t priority);

void idle_thread(void *arg);

// Cooperatively yield the CPU to the next READY thread.
// May be called from any thread at any time with interrupts enabled.
void thread_yield(void);

// Put the calling thread to sleep for approximately `ms` milliseconds.
void thread_sleep(const char *name, uint32_t ms);

// Terminate the calling thread.  Never returns.
void thread_exit(const char *name);

// Get a pointer to the thread at the given slot (0-based index).
// Returns NULL if the slot is unused.
const thread_t *sched_get_thread(int slot);

// Returns the TCB of the currently executing thread.
thread_t *sched_current(void);

// ---------------------------------------------------------------------------
// Internal entry points — called from assembly stubs.
// Do NOT call these directly from C code.
// ---------------------------------------------------------------------------

// Called from the irq0 assembly stub every PIT tick.
// Receives the interrupted RSP and must return the next RSP to restore.
uint64_t irq0_tick_handler(uint64_t current_rsp);

// Called from the isr_yield assembly stub (INT 0x81).
// Receives the current RSP and returns the next RSP.
uint64_t yield_handler(uint64_t current_rsp);

// Returns the ID of the current thread.
uint64_t scheduler_current_thread_id(void);

#endif // SCHED_H
