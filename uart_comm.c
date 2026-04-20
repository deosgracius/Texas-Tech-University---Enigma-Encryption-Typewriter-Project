#include "uart_comm.h"

void uart_init(void) {
    MAP_SysCtlPeripheralEnable(UART_PERIPH);
    MAP_SysCtlPeripheralEnable(UART_GPIO_PERIPH);
    while (!MAP_SysCtlPeripheralReady(UART_PERIPH));
    while (!MAP_SysCtlPeripheralReady(UART_GPIO_PERIPH));
    MAP_GPIOPinConfigure(GPIO_PA0_U0RX);
    MAP_GPIOPinConfigure(GPIO_PA1_U0TX);
    MAP_GPIOPinTypeUART(UART_GPIO_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    MAP_UARTConfigSetExpClk(UART_BASE, g_ui32SysClock, UART_BAUD,
                            UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
    MAP_UARTEnable(UART_BASE);
    MAP_IntEnable(INT_UART0);
    MAP_UARTIntEnable(UART_BASE, UART_INT_RX | UART_INT_RT);
}

void uart_putchar(char c) { MAP_UARTCharPut(UART_BASE, c); }

void uart_puts(const char *str) {
    while (*str) {
        if (*str == '\n') uart_putchar('\r');
        uart_putchar(*str++);
    }
}

void uart_printf(const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    uart_puts(buffer);
}

void UART0_IRQHandler(void) {
    uint32_t status = MAP_UARTIntStatus(UART_BASE, true);
    MAP_UARTIntClear(UART_BASE, status);
    while (MAP_UARTCharsAvail(UART_BASE)) {
        char c = (char)MAP_UARTCharGet(UART_BASE);
        uart_rx_callback(c);
    }
}
