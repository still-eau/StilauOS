// mouse.h - PS/2 mouse driver for x86_64

#ifndef MOUSE_H
#define MOUSE_H

void mouse_init(void);
void irq12_handler(void);

#endif // MOUSE_H
