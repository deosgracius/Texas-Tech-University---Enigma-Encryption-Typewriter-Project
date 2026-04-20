#ifndef UART_COMM_H_
#define UART_COMM_H_

#include "config.h"

void uart_init(void);
void uart_putchar(char c);
void uart_puts(const char *str);
void uart_printf(const char *format, ...);
extern void uart_rx_callback(char c);

#endif
