// main.c - Kernel entry point and interactive shell
//
// Called from boot_entry.asm after low-level setup.
//
// Comments use only standard ASCII characters.

#include "cpu/cpu.h"
#include "cpu/isr.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/krnl_mm.h"
#include "mem/vma.h"
#include "fs/fs.h"
#include "task/sched.h"
#include "boot_info.h"
#include "../Drivers/pic.h"
#include "../Drivers/pit.h"
#include "../Drivers/keyboard.h"
#include "../Drivers/console.h"
#include "../Drivers/vga.h"
#include "../Drivers/mouse.h"

// Defined by the linker script (link.ld)
extern char bss_end;

// Forward declaration of the command struct
typedef struct {
    const char *name;
    const char *description;
    void (*handler)(int argc, char **argv);
} shell_cmd_t;

// Local string comparison helper
static int k_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static inline int atoi(const char *str)
{
    int ret = 0;
    while (*str >= '0' && *str <= '9')
    {
        ret = ret * 10 + (*str - '0');
        str++;
    }
    return ret;
}

static size_t k_strlen(const char *str)
{
    size_t len = 0;
    while (str[len] != '\0')
    {
        len++;
    }
    return len;
}

// Local helper to print aligned text
static void print_padded_string(const char *str, int width)
{
    int len = 0;
    while (str[len] != '\0')
    {
        kconsole_putchar(str[len]);
        len++;
    }
    while (len < width)
    {
        kconsole_putchar(' ');
        len++;
    }
}

// Local helper to print aligned integers
static void print_padded_uint(uint64_t val, int width)
{
    uint64_t temp = val;
    int digits = 0;
    if (temp == 0)
    {
        digits = 1;
    }
    else
    {
        while (temp > 0)
        {
            digits++;
            temp /= 10;
        }
    }
    kprintf("%u", (unsigned int)val);
    while (digits < width)
    {
        kconsole_putchar(' ');
        digits++;
    }
}

// Local helper to print interrupt vector hex prefix
static void print_vector_hex(int vector)
{
    kconsole_puts("0x");
    if (vector < 16)
    {
        kconsole_putchar('0');
    }
    kprintf("%x", vector);
}

// ---------------------------------------------------------------------------
// Shell Command Handlers
// ---------------------------------------------------------------------------

static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_fastfetch(int argc, char **argv);
static void cmd_ticks(int argc, char **argv);
static void cmd_pmm_mem(int argc, char **argv);
static void cmd_kmalloc(int argc, char **argv);
static void cmd_vmm_mem(int argc, char **argv);
static void cmd_interrupts(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_mkdir(int argc, char **argv);
static void cmd_touch(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_rm(int argc, char **argv);
static void cmd_cd(int argc, char **argv);
static void cmd_pwd(int argc, char **argv);
static void cmd_hello(int argc, char **argv);
static void cmd_halt(int argc, char **argv);
static void cmd_create_thread(int argc, char **argv);
static void cmd_yield(int argc, char **argv);
static void cmd_ps(int argc, char **argv);
static void cmd_check_threads(int argc, char **argv);
static void cmd_thread_sleep(int argc, char **argv);
static void cmd_kill_thread(int argc, char **argv);


// Global shell commands list
static shell_cmd_t g_commands[] = {
    {"help",       "Show this help menu",            cmd_help},
    {"clear",      "Clear the screen",               cmd_clear},
    {"fastfetch",  "Show system info",               cmd_fastfetch},
    {"ticks",      "Show PIT tick counter",          cmd_ticks},
    {"pmm_mem",    "Show physical memory usage",     cmd_pmm_mem},
    {"kmalloc",    "Test page and heap allocators",  cmd_kmalloc},
    {"vmm_mem",    "Show virtual memory usage",      cmd_vmm_mem},
    {"interrupts", "Show active interrupt counters", cmd_interrupts},
    {"ls",         "List files in a directory",      cmd_ls},
    {"cat",        "Display file contents",          cmd_cat},
    {"mkdir",      "Create a directory",             cmd_mkdir},
    {"touch",      "Create an empty file",           cmd_touch},
    {"write",      "Overwrite a file with text",     cmd_write},
    {"rm",         "Remove a file or directory",     cmd_rm},
    {"cd",         "Change current directory",       cmd_cd},
    {"pwd",        "Print current directory",        cmd_pwd},
    {"hello",      "Print hello message",            cmd_hello},
    {"halt",       "Halt the CPU",                   cmd_halt},
    {"create",     "Create a new thread",            cmd_create_thread},
    {"yield",      "Yield CPU to scheduler",         cmd_yield},
    {"ps",         "List all threads",               cmd_ps},
    {"check_threads", "Check if threads are running", cmd_check_threads},
    {"thread_sleep", "Sleep for N ms",               cmd_thread_sleep},
    {"kill_thread", "Kill a thread by name",          cmd_kill_thread},
};
#define NUM_COMMANDS (sizeof(g_commands) / sizeof(g_commands[0]))
 
static void cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintln("Available Commands:");
    for (size_t i = 0; i < NUM_COMMANDS; i++)
    {
        kprintf("  ");
        print_padded_string(g_commands[i].name, 12);
        kprintf(" - %s\n", g_commands[i].description);
    }
}
 
static void cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    kconsole_clear();
    kconsole_banner(" Kernel Shell ");
}

static void cmd_fastfetch(int argc, char **argv)
{
    (void)argc; (void)argv;
    kconsole_clear();
    kconsole_banner(" Kernel Shell ");

    // Print Memory Info
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    kprintln("Memory (RAM)");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kprintf("  Total:  %d MB\n", ((pmm_get_total_pages()) * PAGE_SIZE) / 1024 / 1024);
    kprintf("  Free:   %d MB\n", (pmm_get_free_pages() * PAGE_SIZE) / 1024 / 1024);
    kprintf("  Used:   %d MB\n", (pmm_get_used_pages() * PAGE_SIZE) / 1024 / 1024);

    // Print Uptime
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    kprintln("Uptime");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kprintf("  Current Tick: %d\n", (unsigned int)pit_get_ticks());

    // Print OS Info
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    kprintln("OS");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kprintf("  Kernel: StilauOS\n");
    kprintf("  Version: 0.1\n");
}
 
static void cmd_ticks(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("Ticks: %u\n", (unsigned int)pit_get_ticks());
}
 
static void cmd_pmm_mem(int argc, char **argv)
{
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintln("Physical Memory (PMM)");
    kprintln("---------------------");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    pmm_print_info();
}
 
static void cmd_vmm_mem(int argc, char **argv)
{
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintln("Virtual Memory (VMM)");
    kprintln("---------------------");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vmm_print_info();
}
 
static void cmd_hello(int argc, char **argv)
{
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    kprintln("Bonjour depuis le noyau 64 bits !");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}
 
static void cmd_halt(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintln("Halting CPU...");
    cpu_cli();
    for (;;) cpu_halt();
}
 
static void cmd_interrupts(int argc, char **argv)
{
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintln("Vector   Type        Description                      Count");
    kprintln("---------------------------------------------------------------");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
 
    for (int i = 0; i < 48; i++)
    {
        if (i > 20 && i < 32 && i != 30)
        {
            continue;
        }
 
        uint64_t count = isr_get_count(i);
        const char *desc = isr_get_description(i);
        const char *type = (i < 32) ? "Exception" : "IRQ";
 
        if (i >= 32 && count > 0)
        {
            vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        }
        else if (i >= 32)
        {
            vga_set_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        }
        else
        {
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
 
        print_vector_hex(i);
        kconsole_puts("     ");
        print_padded_string(type, 12);
        print_padded_string(desc, 32);
        print_padded_uint(count, 10);
        kconsole_putchar('\n');
    }
 
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintln("---------------------------------------------------------------");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}
 
static void cmd_kmalloc(int argc, char **argv)
{
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintln("Kernel Allocator Test Suite");
    kprintln("---------------------------");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
 
    // 1. Page-level Allocator Test
    kprintln("1. Testing Page-level Allocator (krnl_mm_alloc_pages)...");
    size_t pages_to_alloc = 2;
    void *p_pages = krnl_mm_alloc_pages(pages_to_alloc);
    if (p_pages != NULL)
    {
        if (((uint64_t)p_pages % 4096) != 0)
        {
            kprintln("  [FAIL] Page allocation is not page-aligned!");
        }
        else
        {
            kprintf("  [OK] Allocated %d page(s) at virtual address: %p\n", (int)pages_to_alloc, p_pages);
            uint64_t phys = krnl_mm_page_to_phys(p_pages);
            kprintf("  [OK] Physical address: 0x%p\n", (void *)phys);
 
            char *test_ptr = (char *)p_pages;
            test_ptr[0] = 'H';
            test_ptr[1] = 'e';
            test_ptr[2] = 'l';
            test_ptr[3] = 'l';
            test_ptr[4] = 'o';
            test_ptr[5] = '\0';
            kprintf("  [OK] Write/Read check: '%s'\n", test_ptr);
 
            krnl_mm_free_pages(p_pages, pages_to_alloc);
            kprintln("  [OK] Page range freed successfully.");
        }
    }
    else
    {
        kprintln("  [FAIL] Failed to allocate pages!");
    }
 
    // 2. Byte-level Heap Allocator Test
    kprintln("\n2. Testing Byte-level Heap Allocator (kmalloc/kfree)...");
    void *b1 = kmalloc(64);
    void *b2 = kmalloc(128);
    void *b3 = kmalloc(256);
 
    if (b1 && b2 && b3)
    {
        kprintf("  [OK] Allocated 64 bytes at %p\n", b1);
        kprintf("  [OK] Allocated 128 bytes at %p\n", b2);
        kprintf("  [OK] Allocated 256 bytes at %p\n", b3);
 
        if (((uint64_t)b1 & 15) != 0 || ((uint64_t)b2 & 15) != 0 || ((uint64_t)b3 & 15) != 0)
        {
            kprintln("  [WARN] Allocations are not 16-byte aligned!");
        }
        else
        {
            kprintln("  [OK] Allocations are properly 16-byte aligned.");
        }
 
        char *str1 = (char *)b1;
        char *str2 = (char *)b2;
 
        str1[0] = 'A'; str1[1] = 'B'; str1[2] = 'C'; str1[3] = '\0';
        str2[0] = 'X'; str2[1] = 'Y'; str2[2] = 'Z'; str2[3] = '\0';
 
        kprintf("  [OK] Data block 1: '%s'\n", str1);
        kprintf("  [OK] Data block 2: '%s'\n", str2);
 
        kprintln("  [OK] Freeing allocations...");
        kfree(b1);
        kfree(b2);
        kfree(b3);
 
        void *b_combined = kmalloc(400);
        if (b_combined)
        {
            kprintf("  [OK] Coalescing test: Allocated combined 400 bytes at %p\n", b_combined);
            kfree(b_combined);
        }
        else
        {
            kprintln("  [FAIL] Failed to allocate combined block after free!");
        }
    }
    else
    {
        kprintln("  [FAIL] Byte allocation failed!");
        if (b1) kfree(b1);
        if (b2) kfree(b2);
        if (b3) kfree(b3);
    }
}

// Dummy thread function (every new thread runs this)
static void thread_loop(void *arg)
{
    const char *name = (const char *)arg;
    uint64_t id = scheduler_current_thread_id();

    while (1)
    {
        // Use the current thread's stack for kprintf
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        kprintf("Thread %lu (%s) active\n", (unsigned long)id, name);
        pit_sleep_ms(500);  // busy-wait 500ms using PIT (not schedule()!)
    }
}

// ---------------------------------------------------------------------------
// Scheduler commands
// ---------------------------------------------------------------------------

static void cmd_create_thread(int argc, char **argv)
{
    if (argc != 2)
    {
        kprintln("Usage: create <thread_name>");
        return;
    }

    const char *name = argv[1];
    if (k_strlen(name) >= 16)  // match TCB_NAME_MAX
    {
        kprintln("Thread name is too long.");
        return;
    }

    kprintf("Creating thread '%s'...\n", name);
    if (thread_create(thread_loop, (void *)name, name, 0))  // 0 priority
    {
        kprintf("  [OK] Thread '%s' created and started.\n", name);
    }
    else
    {
        kprintf("  [FAIL] Failed to create thread.\n");
    }
}

static void cmd_yield(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintln("Yielding CPU to scheduler...");
    // This will call the C scheduler entry point which then calls the real
    // IRQ0 handler and lets the scheduler pick a new thread.
    __asm__ volatile("int $0x81");
}

static void cmd_kill_thread(int argc, char **argv)
{
    if (argc != 2)
    {
        kprintln("Usage: kill <thread_name>");
        return;
    }

    const char *name = argv[1];
    if (k_strlen(name) >= 16)  // match TCB_NAME_MAX
    {
        kprintln("Thread name is too long.");
        return;
    }

    kprintf("Killing thread '%s'...\n", name);
    thread_exit(name);
    {
        kprintf("  [FAIL] Failed to kill thread.\n");
    }
}

static void cmd_ps(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintln("Listing threads...");
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
    {
        const thread_t *t = sched_get_thread(i);
        if (t != NULL)
        {
            kprintf("  [OK] Thread '%s' created and started.\n", t->name);
        }
        else
        {
            kprintf("  [FAIL] Failed to create thread.\n");
        }
    }
}

// Ask threads name and check via sched_is_running() and prints the status in color red/green based on the result
static void cmd_check_threads(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintln("Checking threads...");
    for (int i = 0; i < SCHED_MAX_THREADS; i++)
    {
        const thread_t *t = sched_get_thread(i);
        if (t != NULL)
        {
            if (sched_is_running())
            {
                vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
                kprintf("  [OK] Thread '%s' is running.\n", t->name);
            }
            else
            {
                vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
                kprintf("  [FAIL] Thread '%s' is not running.\n", t->name);
            }
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
        else
        {
            kprintf("  [OK] Thread '%s' created and started.\n", t->name);
        }
    }
}

// ask the thread name and sleep for N ms
static void cmd_thread_sleep(int argc, char **argv)
{
    if (argc < 2)
    {
        kprintln("Usage: thread_sleep <thread_name> <ms>");
        return;
    }

    // Set the thread to sleep for N ms
    const char *name = argv[1];
    int ms = atoi(argv[2]);
    if (ms > 0)
    {
        thread_sleep(name, ms);
        kprintf("Thread %s slept for %d ms\n", name, ms);
    }
    else
    {
        kprintln("Failed to put thread to sleep\n");
    }
}

// ---------------------------------------------------------------------------
// StilauFS Command Handlers & Helpers
// ---------------------------------------------------------------------------

static fs_node_t *find_parent_and_name(const char *path, char *name_out)
{
    int last_slash = -1;
    int len = 0;
    while (path[len] != '\0')
    {
        if (path[len] == '/')
        {
            last_slash = len;
        }
        len++;
    }

    if (last_slash == -1)
    {
        int name_len = 0;
        while (path[name_len] != '\0' && name_len < FS_NAME_MAX - 1)
        {
            name_out[name_len] = path[name_len];
            name_len++;
        }
        name_out[name_len] = '\0';
        return fs_get_cwd();
    }
    else if (last_slash == 0)
    {
        int name_len = 0;
        while (path[1 + name_len] != '\0' && name_len < FS_NAME_MAX - 1)
        {
            name_out[name_len] = path[1 + name_len];
            name_len++;
        }
        name_out[name_len] = '\0';
        return fs_get_root();
    }
    else
    {
        char parent_path[FS_PATH_MAX];
        int i = 0;
        while (i < last_slash && i < FS_PATH_MAX - 1)
        {
            parent_path[i] = path[i];
            i++;
        }
        parent_path[i] = '\0';

        int name_len = 0;
        while (path[last_slash + 1 + name_len] != '\0' && name_len < FS_NAME_MAX - 1)
        {
            name_out[name_len] = path[last_slash + 1 + name_len];
            name_len++;
        }
        name_out[name_len] = '\0';

        return fs_find_node(parent_path);
    }
}

static void print_node_path(fs_node_t *node)
{
    if (node == fs_get_root())
    {
        kprintf("/");
        return;
    }
    if (node->parent && node->parent != fs_get_root())
    {
        print_node_path(node->parent);
    }
    kprintf("/%s", node->name);
}

static void cmd_ls(int argc, char **argv)
{
    const char *path = ".";
    if (argc > 1)
    {
        path = argv[1];
    }
    
    fs_dir_t *dir = fs_opendir(path);
    if (!dir)
    {
        kprintf("ls: cannot access '%s': No such directory\n", path);
        return;
    }

    dirent_t entry;
    while (fs_readdir(dir, &entry))
    {
        if (entry.type == FS_DIRECTORY)
        {
            vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            kprintf("%s/  ", entry.name);
        }
        else
        {
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            kprintf("%s (%u octets)  ", entry.name, entry.size);
        }
    }
    kprintf("\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    fs_closedir(dir);
}

static void cmd_cat(int argc, char **argv)
{
    if (argc < 2)
    {
        kprintf("Usage: cat <file>\n");
        return;
    }

    fs_file_t *file = fs_fopen(argv[1], "r");
    if (!file)
    {
        kprintf("cat: %s: No such file\n", argv[1]);
        return;
    }

    char buf[128];
    size_t bytes_read;
    while ((bytes_read = fs_read(file, buf, sizeof(buf) - 1)) > 0)
    {
        buf[bytes_read] = '\0';
        kprintf("%s", buf);
    }
    kprintf("\n");
    fs_fclose(file);
}

static void cmd_mkdir(int argc, char **argv)
{
    if (argc < 2)
    {
        kprintf("Usage: mkdir <directory>\n");
        return;
    }

    char name[FS_NAME_MAX];
    fs_node_t *parent = find_parent_and_name(argv[1], name);
    if (!parent)
    {
        kprintf("mkdir: cannot create directory '%s': Parent directory not found\n", argv[1]);
        return;
    }

    if (!fs_create_dir(parent, name))
    {
        kprintf("mkdir: cannot create directory '%s': Directory already exists or invalid name\n", argv[1]);
    }
}

static void cmd_touch(int argc, char **argv)
{
    if (argc < 2)
    {
        kprintf("Usage: touch <file>\n");
        return;
    }

    char name[FS_NAME_MAX];
    fs_node_t *parent = find_parent_and_name(argv[1], name);
    if (!parent)
    {
        kprintf("touch: cannot create file '%s': Parent directory not found\n", argv[1]);
        return;
    }

    fs_node_t *existing = fs_find_node(argv[1]);
    if (existing)
    {
        return;
    }

    if (!fs_create_file(parent, name))
    {
        kprintf("touch: cannot create file '%s': File already exists or invalid name\n", argv[1]);
    }
}

static void cmd_write(int argc, char **argv)
{
    if (argc < 3)
    {
        kprintf("Usage: write <file> <text>\n");
        return;
    }

    fs_file_t *file = fs_fopen(argv[1], "w");
    if (!file)
    {
        kprintf("write: cannot open '%s' for writing\n", argv[1]);
        return;
    }

    char text[1024];
    int offset = 0;
    for (int i = 2; i < argc; i++)
    {
        int len = 0;
        while (argv[i][len] != '\0')
        {
            if (offset < (int)sizeof(text) - 2)
            {
                text[offset++] = argv[i][len];
            }
            len++;
        }
        if (i < argc - 1 && offset < (int)sizeof(text) - 2)
        {
            text[offset++] = ' ';
        }
    }
    if (offset < (int)sizeof(text) - 2)
    {
        text[offset++] = '\n';
    }
    text[offset] = '\0';

    fs_write(file, text, offset);
    fs_fclose(file);
}

static void cmd_rm(int argc, char **argv)
{
    if (argc < 2)
    {
        kprintf("Usage: rm <file_or_dir>\n");
        return;
    }

    fs_node_t *node = fs_find_node(argv[1]);
    if (!node)
    {
        kprintf("rm: %s: No such file or directory\n", argv[1]);
        return;
    }

    if (node == fs_get_root())
    {
        kprintf("rm: cannot remove root directory\n");
        return;
    }

    if (fs_remove_node(node) != 0)
    {
        kprintf("rm: failed to remove '%s'\n", argv[1]);
    }
}

static void cmd_cd(int argc, char **argv)
{
    const char *path = "/";
    if (argc > 1)
    {
        path = argv[1];
    }

    fs_node_t *node = fs_find_node(path);
    if (!node)
    {
        kprintf("cd: %s: No such file or directory\n", path);
        return;
    }

    if (node->type != FS_DIRECTORY)
    {
        kprintf("cd: %s: Not a directory\n", path);
        return;
    }

    fs_set_cwd(node);
}

static void cmd_pwd(int argc, char **argv)
{
    (void)argc; (void)argv;
    fs_node_t *cwd = fs_get_cwd();
    print_node_path(cwd);
    kprintf("\n");
}

void shell_thread(void *arg)
{
    (void)arg;

    char line[256];

    while (1)
    {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        kconsole_puts("KernelConsole> ");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        int n = kconsole_readline(line, sizeof(line));
        if (n == 0)
        {
            continue;
        }

        // Simple tokenizer: split by space
        char *argv[16];
        int argc = 0;

        char *p = line;
        while (*p && argc < 16)
        {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            {
                *p++ = '\0';
            }
            if (!*p) break;
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            {
                p++;
            }
        }

        if (argc == 0) continue;

        // Command dispatch
        bool found = false;
        for (size_t i = 0; i < NUM_COMMANDS; i++)
        {
            if (k_strcmp(argv[0], g_commands[i].name) == 0)
            {
                g_commands[i].handler(argc, argv);
                found = true;
                break;
            }
        }

        if (!found)
        {
            kprintf("Unknown command: %s\n", argv[0]);
        }
    }
}
 
// ---------------------------------------------------------------------------
// kernel_main - kernel C entry point
// ---------------------------------------------------------------------------
 
void kernel_main(boot_info_t *boot_info)
{
    kconsole_init();
    kconsole_banner("StilauOS Kernel");
    kconsole_puts("[OK] Console initialized.\n");
    if (!boot_info || boot_info->magic != BOOT_MAGIC)
    {
        kconsole_puts("[WARN] Invalid boot_info magic\n");
    }
    else
    {
        kconsole_puts("[OK] Boot info validated.\n");
    }
    // ---------------- CPU + MEMORY ----------------
    cpu_init();
    pmm_init(boot_info, (uint64_t)&bss_end);
    kprintf("Free RAM: %u KB / %u KB\n",
            (uint32_t)(pmm_get_free_pages() * PAGE_SIZE / 1024),
            (uint32_t)(pmm_get_total_pages() * PAGE_SIZE / 1024));
    kprintln("PMM initialized.");
    vmm_init();
    kprintln("VMM initialized.");

    krnl_mm_init();
    kprintln("Kernel Memory Manager initialized.");
    vma_init();
    kprintln("Virtual Memory Area initialized.");
    // ---------------- INTERRUPTS (NO ENABLE YET) ----------------
    pic_init();
    kprintln("PIC initialized.");
    // ---------------- SCHEDULER ----------------
    kconsole_puts("[..] Initializing scheduler...\n");
    sched_init();
    kconsole_puts("[OK] Scheduler initialized.\n");
    fs_init();
    kprintln("StilauFS initialized.");
    // ---------------- THREADS ----------------
    extern void shell_thread(void *arg);
    uint32_t shell_id = thread_create(shell_thread, NULL, "shell", 1);
    if (shell_id == 0)
    {
        kconsole_puts("[WARN] Failed to create shell thread.\n");
    }
    kconsole_puts("[OK] Multitasking enabled.\n");
    // ---------------- TIMER + IRQ ----------------
    kconsole_puts("[..] Initializing PIT...\n");
    pit_init(1000);
    kconsole_puts("[OK] PIT initialized.\n");
    extern void irq0(void);
    irq_install(0, irq0);
    keyboard_init();
    kprintln("Keyboard initialized.");
    mouse_init();
    kprintln("Mouse initialized.");
    // ---------------- ENABLE INTERRUPTS ----------------
    __asm__ volatile("sti");
    // kernel_main MUST NOT run as thread
    // instead, kernel becomes "idle execution context"
    for (;;)
        __asm__ volatile("hlt");
}