#ifndef LCD_H_
#define LCD_H_

#include <stdint.h>

#define LCD_LINE1   0x00
#define LCD_LINE2   0x40
#define LCD_LINE3   0x14
#define LCD_LINE4   0x54

void LCD_UART_Init(uint32_t sysClock);
void LCD_Init(void);
void LCD_On(void);
void LCD_Off(void);
void LCD_Clear(void);
void LCD_Home(void);
void LCD_SetContrast(uint8_t c);
void LCD_SetBrightness(uint8_t b);
void LCD_SetCursor(uint8_t pos);
void LCD_Print(const char *s);
void LCD_PrintPadded(const char *s, uint8_t width);

#endif
