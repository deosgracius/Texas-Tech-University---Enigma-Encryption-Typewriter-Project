/******************************************************************************
 * LCD.c  —  NHD-0420D3Z driver via UART6 RS-232 mode
 *
 * Config: R1=Open, R2=Open on LCD board (RS-232 mode, 9600 baud)
 * Wiring: PP1 (UART6 TX) → LCD J1 pin 1 (RX)
 *         GND            → LCD J1 pin 2
 *         5V             → LCD J1 pin 3
 *
 * Protocol (datasheet p.7):
 *   Commands : 0xFE [cmd]          two separate UART bytes
 *   With param: 0xFE [cmd] [param]  three bytes
 *   ASCII text: send byte directly, no prefix
 *
 * Bug fixes vs previous version:
 *   1. Removed (void)sysClock — parameter IS used in MAP_UARTConfigSetExpClk
 *   2. LCD_Init now calls LCD_Clear() which has the required 5ms post-delay
 *   3. Backlight command uses consistent lcd_cmd_param() helper
 *   4. Power-on delay increased to 500ms (PIC16F690 boot time)
 ******************************************************************************/

#include "LCD.h"
#include "config.h"
#include <stdarg.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
 * Delay — uses SysTick millisecond counter (must be running before use)
 * --------------------------------------------------------------------- */
static void delay_ms(uint32_t ms)
{
    uint32_t start = g_tickMs;
    while ((g_tickMs - start) < ms);
}

/* -----------------------------------------------------------------------
 * lcd_write_byte — send one UART byte to the LCD
 * 2ms delay after each byte (PIC16F690 needs time to process)
 * --------------------------------------------------------------------- */
static void lcd_write_byte(uint8_t b)
{
    MAP_UARTCharPut(LCD_UART_BASE, b);
    delay_ms(2);
}

/* -----------------------------------------------------------------------
 * lcd_cmd — send command prefix + command byte
 * --------------------------------------------------------------------- */
static void lcd_cmd(uint8_t cmd)
{
    lcd_write_byte(LCD_CMD_PREFIX);   /* 0xFE */
    lcd_write_byte(cmd);
}

/* -----------------------------------------------------------------------
 * lcd_cmd_param — send prefix + command + one parameter byte
 * --------------------------------------------------------------------- */
static void lcd_cmd_param(uint8_t cmd, uint8_t param)
{
    lcd_write_byte(LCD_CMD_PREFIX);
    lcd_write_byte(cmd);
    lcd_write_byte(param);
}

/* ============================================================================
 * PUBLIC API
 * ========================================================================== */

void LCD_UART_Init(uint32_t sysClock)
{
    /* Bug fix: removed (void)sysClock — it IS used in MAP_UARTConfigSetExpClk */
    MAP_SysCtlPeripheralEnable(LCD_UART_PERIPH);
    MAP_SysCtlPeripheralEnable(LCD_GPIO_PERIPH);
    while (!MAP_SysCtlPeripheralReady(LCD_UART_PERIPH));
    while (!MAP_SysCtlPeripheralReady(LCD_GPIO_PERIPH));

    MAP_GPIOPinConfigure(GPIO_PP1_U6TX);
    MAP_GPIOPinTypeUART(LCD_GPIO_BASE, LCD_TX_PIN);

    MAP_UARTConfigSetExpClk(LCD_UART_BASE, sysClock, LCD_BAUD,
        UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
    MAP_UARTEnable(LCD_UART_BASE);
}

void LCD_Init(void)
{
    /* PIC16F690 on this LCD needs up to 500ms to boot after power-on */
    delay_ms(500);

    lcd_cmd(LCD_CMD_DISPLAY_ON);          /* 0xFE 0x41 — display on       */
    lcd_cmd_param(LCD_CMD_CONTRAST, 40);  /* 0xFE 0x52 0x28 — contrast 40 */
    lcd_cmd_param(LCD_CMD_BACKLIGHT, 8);  /* 0xFE 0x53 0x08 — backlight 8 */

    /* Bug fix: use LCD_Clear() which has the required 5ms post-delay.
     * lcd_cmd(0x51) alone only gives 4ms (2ms+2ms) — too short for clear. */
    LCD_Clear();
}

void LCD_On(void)  { lcd_cmd(LCD_CMD_DISPLAY_ON); }
void LCD_Off(void) { lcd_cmd(LCD_CMD_DISPLAY_OFF); }

void LCD_Clear(void)
{
    lcd_cmd(LCD_CMD_CLEAR);
    delay_ms(8);    /* clear needs 5-10ms on PIC16F690 */
}

void LCD_Home(void)
{
    lcd_cmd(LCD_CMD_HOME);
    delay_ms(2);
}

void LCD_SetContrast(uint8_t c)   { lcd_cmd_param(LCD_CMD_CONTRAST,  c); }
void LCD_SetBrightness(uint8_t b) { lcd_cmd_param(LCD_CMD_BACKLIGHT, b); }

void LCD_SetCursor(uint8_t pos)
{
    /* pos: LCD_LINE1=0x00, LCD_LINE2=0x40, LCD_LINE3=0x14, LCD_LINE4=0x54 */
    lcd_cmd_param(LCD_CMD_CURSOR_POS, pos);
}

void LCD_Print(const char *s)
{
    while (*s) lcd_write_byte((uint8_t)*s++);
}

void LCD_PrintPadded(const char *s, uint8_t width)
{
    uint8_t i = 0;
    while (s[i] && i < width) lcd_write_byte((uint8_t)s[i++]);
    while (i++ < width)       lcd_write_byte(' ');
}
