// ahci.h
//
// AHCI driver for x86_64
// Part of StilauOS Kernel

#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../Krnl/mem/pmm.h"
#include "../Krnl/mem/vmm.h"

#define PCI_BAR5_SIZE 4096

#define AHCI_REG_CMD           0x00
#define AHCI_CMD_STOP          0x00
#define AHCI_CMD_START         0x01
#define AHCI_CMD_RESET         0x02
#define AHCI_REG_PI            0x04
#define AHCI_REG_PORT_IMASK    0x08
#define AHCI_REG_PORT_ISRC     0x0C
#define AHCI_REG_CMD_PORT      0x10
#define AHCI_REG_SCTL          0x20
#define AHCI_REG_SSTS          0x24
#define AHCI_REG_ssts          0x0c
#define AHCI_REG_serr          0x10
#define AHCI_REG_SACT          0x14
#define AHCI_REG_CI            0x18

// Port registers
#define AHCI_PORT_CMD       0x00
#define AHCI_PORT_SSTS      0x04
#define AHCI_PORT_IE      0x08
#define AHCI_PORT_CMD_PORT_IMASK 0x00
#define AHCI_REG_CMD_PORT_ISRC 0x04
#define AHCI_REG_CMD_PORT_CMD 0x08

// standard error codes
typedef enum {
    AHCI_SUCCESS = 0,
    AHCI_ERR_TIMEOUT,
    AHCI_ERR_DEVICE_NOT_FOUND,
    AHCI_ERR_INVALID_PORT,
    AHCI_ERR_DMA_FAILURE
} ahci_status_t;

// detected device types
typedef enum {
    AHCI_DEV_SATA = 0,
    AHCI_DEV_SATAPI,
    AHCI_DEV_SEMB,
    AHCI_DEV_PM,
    AHCI_DEV_UNKNOWN
} ahci_device_type_t;

// Modern representation of an AHCI device
typedef struct {
    uint8_t  port_idx;
    uint8_t  device_type;
    uint64_t sector_count;
    uint32_t sector_size;
    bool     supports_ncq;      // Native Command Queuing
    char     model_number[40];
    void*    private_data;      // For internal driver context
    uintptr_t base_addr;
    uintptr_t phys_addr;
    uintptr_t virt_addr;
    uintptr_t map_virt_addr;
    uintptr_t map_phys_addr;
    uint8_t   port_index;
    uint16_t  vendor_id;
    uint16_t  device_id;
    void* cmd_list_virt;
    void* cmd_list_phys;
    void* rfis_virt;
    void* rfis_phys;
} ahci_device_t;

typedef struct
{
    uintptr_t phys_addr;
    void*     virt_addr;
    size_t    size;
} dma_buffer_t;

typedef struct __attribute__((packed)) {
    uint32_t dba;  // Data Base Address
    uint32_t dbau; // Data Base Address Upper
    uint32_t rsvd;
    uint32_t dbc:22; // Data Byte Count (0-based)
    uint32_t rsvd1:9;
    uint32_t i:1;    // Interrupt on completion
} ahci_prd_t;

typedef struct __attribute__((packed)) {
    uint8_t cfis[64];
    uint8_t atapi_cmd[16];
    uint8_t reserved[48];
    ahci_prd_t prdt[1];
} ahci_cmd_table_t;

typedef struct
{
    uint64_t ctba;
    uint64_t ctbau;
    uint32_t prdtl;
    uint32_t rsvd1;
    uint32_t prpb;
    uint32_t prpbu;
    uint64_t rsvd2;
    uint64_t rsvd3;
    uint64_t w;
}ahci_cmd_header_t;

// Initialization and scan interface
ahci_status_t ahci_init(uint64_t pci_bar5_phys_addr);
size_t        ahci_get_device_count(void);
ahci_device_t* ahci_get_device(uint8_t port_idx);

// Asynchronous operations
// Buffer must be allocated via ahci_dma_alloc()
ahci_status_t ahci_read(ahci_device_t* dev, uint64_t lba, uint32_t count, void* buffer);
ahci_status_t ahci_write(ahci_device_t* dev, uint64_t lba, uint32_t count, const void* buffer);

// DMA memory management (crucial for avoiding corruption)
void* ahci_dma_alloc(size_t size, uint64_t* physical_addr);
void  ahci_dma_free(void* virtual_addr, size_t size);

#endif // AHCI_H