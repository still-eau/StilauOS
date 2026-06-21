// mouse.c - PS/2 mouse driver with IntelliMouse (scroll wheel) support
//
// Communicates with PS/2 mouse via 8042 Keyboard Controller.
// Uses IRQ 12 (vector 44) for interrupts.

#include "mouse.h"
#include "pic.h"
#include "io.h"
#include "vga.h"
#include "../Krnl/cpu/isr.h"

static volatile uint8_t mouse_device_id = 0;
static uint8_t packet[4];
static int packet_byte_index = 0;

static void mouse_wait(int type)
{
    uint64_t timeout = 100000;
    if (type == 0) // wait for data to be readable
    {
        while (timeout--)
        {
            if ((inb(0x64) & 1) == 1)
            {
                return;
            }
        }
    }
    else // wait for data to be writable
    {
        while (timeout--)
        {
            if ((inb(0x64) & 2) == 0)
            {
                return;
            }
        }
    }
}

static void mouse_write(uint8_t data)
{
    // Tell controller we are writing to auxiliary device
    mouse_wait(1);
    outb(0x64, 0xD4);
    // Write data
    mouse_wait(1);
    outb(0x60, data);
}

static uint8_t mouse_read(void)
{
    mouse_wait(0);
    return inb(0x60);
}

void mouse_init(void)
{
    // Disable interrupts to prevent PIT / Keyboard races during 8042 controller configuration
    __asm__ volatile ("cli");

    // Enable auxiliary device
    mouse_wait(1);
    outb(0x64, 0xA8);

    // Read Command Byte, set bit 1 (Enable IRQ 12), clear bit 5 (Enable Mouse Clock)
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    uint8_t command_byte = inb(0x60);
    command_byte |= 0x02;     // enable IRQ 12
    command_byte &= ~0x20;    // clear bit 5 to enable mouse clock
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, command_byte);

    // Set Default Settings
    mouse_write(0xF6);
    mouse_read(); // ACK (0xFA)

    // IntelliMouse Activation Sequence:
    // Write 0xF3 (Set Sample Rate), ACK, then write rate value.
    // Rates sequence: 200, 100, 80.
    
    // Set Sample Rate = 200
    mouse_write(0xF3);
    mouse_read(); // ACK
    mouse_write(200);
    mouse_read(); // ACK

    // Set Sample Rate = 100
    mouse_write(0xF3);
    mouse_read(); // ACK
    mouse_write(100);
    mouse_read(); // ACK

    // Set Sample Rate = 80
    mouse_write(0xF3);
    mouse_read(); // ACK
    mouse_write(80);
    mouse_read(); // ACK

    // Get Device ID
    mouse_write(0xF2);
    mouse_read(); // ACK
    mouse_device_id = mouse_read();

    // Install IRQ12 handler (must be done before enabling data reporting so we can receive packets)
    extern void irq12(void);
    irq_install(12, irq12);

    // Enable Data Reporting
    mouse_write(0xF4);
    mouse_read(); // ACK

    // Re-enable interrupts
    __asm__ volatile ("sti");
}

static void process_mouse_packet(void)
{
    if (mouse_device_id == 3)
    {
        // packet[3] contains Z movement (scroll wheel) in bits 0-3.
        // It is a signed 4-bit value that we sign-extend to signed 8-bit integer.
        int8_t scroll = (int8_t)(packet[3] & 0x0F);
        if (scroll & 0x08)
        {
            scroll |= 0xF0; // sign-extend 4-bit to 8-bit
        }

        if (scroll != 0)
        {
            // scroll positive is wheel rolled UP, which means scroll viewport UP (negative delta)
            // scroll negative is wheel rolled DOWN, which means scroll viewport DOWN (positive delta)
            vga_scroll_viewport(-scroll);
        }
    }
}

void irq12_handler(void)
{
    // Update our debug interrupt statistics counter
    isr_increment_counter(44);

    uint8_t status = inb(0x64);
    // Ensure there is data in the output buffer
    if (status & 1)
    {
        uint8_t data = inb(0x60);

        // Only process if it is mouse data (status bit 5 is set)
        if (status & 0x20)
        {
            // Synchronize packet stream
            if (packet_byte_index == 0 && !(data & 0x08))
            {
                // Out of sync! First byte must have bit 3 set to 1. Discard.
                pic_send_eoi(12);
                return;
            }

            packet[packet_byte_index++] = data;

            int expected_size = (mouse_device_id == 3) ? 4 : 3;
            if (packet_byte_index >= expected_size)
            {
                packet_byte_index = 0;
                process_mouse_packet();
            }
        }
    }
    pic_send_eoi(12);
}
