// AHCI driver for x86_64
// Part of StilauOS Kernel

#include "ahci.h"
#include "io.h"
#include "../Krnl/mem/pmm.h"
#include "../Krnl/mem/vmm.h"
#include "../Krnl/task/sched.h"
#include <stddef.h>
#include <stdint.h>

// global static variables for the driver
static dma_buffer_t*     controller_table_buffer = NULL;
static volatile uint32_t* abar_phys_reg         = NULL;
static volatile uint32_t* abar_virt_reg         = NULL;
static ahci_device_t     devices[32];
static size_t            device_count          = 0;

// Helper functions
static inline uint32_t read_reg32(volatile uint32_t* addr)
{
    return *addr;
}

static inline void write_reg32(volatile uint32_t* addr, uint32_t value)
{
    *addr = value;
}

// port access helper functions
static inline uint32_t read_port32(uintptr_t base_addr, uintptr_t offset)
{
    return read_reg32((volatile uint32_t*)(base_addr + offset));
}

static inline void write_port32(uintptr_t base_addr, uintptr_t offset, uint32_t value)
{
    write_reg32((volatile uint32_t*)(base_addr + offset), value);
}

// ahci dma alloc: allocate an page (4KB) 
void* ahci_dma_alloc(size_t size, uint64_t* physical_addr)
{
    // calculate number of pages needed
    size_t num_pages = (size + 4095) / 4096;

    // allocate physical pages
    void* phys = pmm_alloc_pages(num_pages);
    if (phys == NULL) return NULL;

    // map in virtual memory
    void* virt = vmm_map_page(vmm_get_kernel_pml4(), (uint64_t)phys, num_pages * 4096, PTE_PRESENT | PTE_RW);
    
    if (virt == NULL) {
        pmm_free_pages(phys, num_pages);
        return NULL;
    }

    *physical_addr = (uint64_t)phys;
    return virt;
}

void ahci_dma_free(void* addr, size_t size)
{
    size_t num_pages = (size + 4095) / 4096;
    uint64_t phys = (uint64_t)addr;
    
    // unmap and free pages
    vmm_unmap(vmm_get_kernel_pml4(), phys);
    pmm_free_pages((void*)phys, num_pages);
}

// reset controller
static void reset_controller(void)
{
    // stop controller
    write_reg32(abar_phys_reg + AHCI_REG_CMD, AHCI_CMD_STOP);
    io_wait();

    // wait for controller to stop
    while (read_reg32(abar_phys_reg + AHCI_REG_CMD) & AHCI_CMD_START) {
        io_wait();
    }

    // reset controller
    write_reg32(abar_phys_reg + AHCI_REG_CMD, AHCI_CMD_RESET);
    io_wait();

    // wait for controller to reset
    while (read_reg32(abar_phys_reg + AHCI_REG_CMD) & AHCI_CMD_RESET) {
        io_wait();
    }
}

// Identify all SATA ports and populate the devices array
static void identify_sata_ports(void)
{
    uint32_t ports_implemented = read_reg32(abar_phys_reg + AHCI_REG_PI);
    for (uint8_t i = 0; i < 32; i++) {
        if ((ports_implemented >> i) & 1) {
            devices[device_count].port_index = i;
            devices[device_count].base_addr = (uintptr_t)abar_virt_reg + 0x100 + (i * 0x80);
            devices[device_count].phys_addr = (uintptr_t)abar_phys_reg + 0x100 + (i * 0x80);
            device_count++;
        }
    }
}

// Check if the controller is an AHCI controller
static bool is_ahci_controller(uint16_t vendor_id, uint16_t device_id)
{
    if (vendor_id == 0x8086 && (device_id == 0x282b || device_id == 0x282a || device_id == 0x8d02)) {
        return true;
    }
    return false;
}

// Initialize AHCI controller
static bool ahci_init_controller(uint16_t vendor_id, uint16_t device_id)
{
    // Check if controller is an AHCI controller
    if (!is_ahci_controller(vendor_id, device_id)) {
        return false;
    }

    // Reset controller
    reset_controller();

    // Identify all SATA ports
    identify_sata_ports();

    return true;
}

// get device count
size_t ahci_get_device_count(void)
{
    return device_count;
}

// Get device AHCI
ahci_device_t *ahci_get_device(uint8_t port_index)
{
    for (size_t i; i < device_count; i++)
    {
        if (devices[i].port_index == port_index)
        {
            return &devices[i];
        }
    }
    return NULL;
}

// AHCI write command
ahci_status_t ahci_read(ahci_device_t* dev, uint64_t lba, uint32_t count, void* buffer) 
{
    // 1. get physical addr from buffer
    uint64_t phys_buffer = vmm_get_physical_address(vmm_get_kernel_pml4(), (uint64_t)buffer);
    
    // 2. get DMA table (must be physical and contiguous)
    uint64_t phys_cmd_table;
    ahci_cmd_table_t* cmd_table = (ahci_cmd_table_t*)ahci_dma_alloc(sizeof(ahci_cmd_table_t), &phys_cmd_table);

    // 3. fill command FIS (READ DMA EXT - Opcode 0x25)
    cmd_table->cfis[0] = 0x27; // FIS Type
    cmd_table->cfis[1] = 0x80; // C = 1 (Command)
    cmd_table->cfis[2] = 0x25; // READ DMA EXT
    
    // LBA (48 bits)
    cmd_table->cfis[4] = (uint8_t)lba;
    cmd_table->cfis[5] = (uint8_t)(lba >> 8);
    cmd_table->cfis[6] = (uint8_t)(lba >> 16);
    cmd_table->cfis[7] = 0x40; // LBA mode
    cmd_table->cfis[8] = (uint8_t)(lba >> 24);
    cmd_table->cfis[9] = (uint8_t)(lba >> 32);
    cmd_table->cfis[10] = (uint8_t)(lba >> 40);
    
    // Nombre de secteurs
    cmd_table->cfis[12] = (uint8_t)count;
    cmd_table->cfis[13] = (uint8_t)(count >> 8);

    // 4. fill prdt
    cmd_table->prdt[0].dba = (uint32_t)phys_buffer;
    cmd_table->prdt[0].dbau = (uint32_t)(phys_buffer >> 32);
    cmd_table->prdt[0].dbc = (count * 512) - 1; // 512 octets par secteur
    cmd_table->prdt[0].i = 1; // Interruption à la fin

    // 5. Send the command via the port specific
    ahci_cmd_header_t* cmd_list = (ahci_cmd_header_t*)dev->cmd_list_virt; 
    cmd_list[0].ctba = (uint32_t)phys_cmd_table;
    cmd_list[0].ctbau = (uint32_t)(phys_cmd_table >> 32);
    cmd_list[0].prdtl = 1;

    // 6. Send the command
    write_port32(dev->base_addr, AHCI_REG_CI, 1);

    // 7. Wait for the interruption (synchronous here for simplicity)
    while (!sched_is_interrupted())
    {
        if (read_port32(dev->base_addr, AHCI_REG_CI) & 1)
        {
            break;
        }
        io_wait();
    }

    // Clean up
    ahci_dma_free(cmd_table, sizeof(ahci_cmd_table_t));
    
    return AHCI_SUCCESS;
}

ahci_status_t ahci_write(ahci_device_t* dev, uint64_t lba, uint32_t count, const void* buffer) 
{
    // get physical addr from buffer
    uint64_t phys_buffer = vmm_get_physical_address(vmm_get_kernel_pml4(), (uint64_t)buffer);
    
    // allocate command table (DMA)
    uint64_t phys_cmd_table;
    ahci_cmd_table_t* cmd_table = (ahci_cmd_table_t*)ahci_dma_alloc(sizeof(ahci_cmd_table_t), &phys_cmd_table);
    if (!cmd_table) return AHCI_ERR_DMA_FAILURE;

    // fill command FIS (WRITE DMA EXT - Opcode 0x35)
    cmd_table->cfis[0] = 0x27; // FIS Type
    cmd_table->cfis[1] = 0x80; // C = 1 (Command)
    cmd_table->cfis[2] = 0x35; // WRITE DMA EXT
    
    // LBA (48 bits)
    cmd_table->cfis[4] = (uint8_t)lba;
    cmd_table->cfis[5] = (uint8_t)(lba >> 8);
    cmd_table->cfis[6] = (uint8_t)(lba >> 16);
    cmd_table->cfis[7] = 0x40; // LBA mode
    cmd_table->cfis[8] = (uint8_t)(lba >> 24);
    cmd_table->cfis[9] = (uint8_t)(lba >> 32);
    cmd_table->cfis[10] = (uint8_t)(lba >> 40);
    
    // number of sectors
    cmd_table->cfis[12] = (uint8_t)count;
    cmd_table->cfis[13] = (uint8_t)(count >> 8);

    // fill prdt
    cmd_table->prdt[0].dba = (uint32_t)(phys_buffer & 0xFFFFFFFF);
    cmd_table->prdt[0].dbau = (uint32_t)((phys_buffer >> 32) & 0xFFFFFFFF);
    cmd_table->prdt[0].dbc = (count * 512) - 1;
    cmd_table->prdt[0].i = 1; 

    // fill command header (Write bit 'w' = 1)
    ahci_cmd_header_t* cmd_list = (ahci_cmd_header_t*)dev->cmd_list_virt;
    cmd_list[0].w = 1;
    cmd_list[0].ctba = (uint32_t)(phys_cmd_table & 0xFFFFFFFF);
    cmd_list[0].ctbau = (uint32_t)((phys_cmd_table >> 32) & 0xFFFFFFFF);
    cmd_list[0].prdtl = 1;

    // Send the command
    write_port32(dev->base_addr, AHCI_REG_CI, 1);

    // wait for completion
    while (read_port32(dev->base_addr, AHCI_REG_CI) & 1) {
        io_wait();
    }

    // clean up
    ahci_dma_free(cmd_table, sizeof(ahci_cmd_table_t));
    
    return AHCI_SUCCESS;
}