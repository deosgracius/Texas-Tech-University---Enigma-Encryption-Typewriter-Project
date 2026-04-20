#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "ti/devices/msp432e4/driverlib/driverlib.h"

/* ── System globals ─────────────────────────────────────────────────────── */
extern uint32_t          g_ui32SysClock;
extern volatile uint32_t g_tickMs;

typedef enum { MODE_PASSTHROUGH=0, MODE_ENCRYPT, MODE_DECRYPT } OperationMode;
extern volatile OperationMode g_currentMode;
extern bool g_typing_started;

/* ── PD7 NMI unlock ─────────────────────────────────────────────────────── */
#define GPIO_LOCK_OFFSET   0x520u
#define GPIO_CR_OFFSET     0x524u
#define GPIO_LOCK_KEY_VAL  0x4C4F434Bu

/* ══════════════════════════════════════════════════════════════════════════
 * SCAN INPUTS — active row-drive, col WPU
 * COLUMNS: col0=PE4  col1=PC4  col2=PC5  col3=PC6
 *           col4=PE5  col5=PD3  col6=PC7  col7=PB2
 * ROWS:    row0=PE0  row1=PE1  row2=PE2  row3=PE3
 *           row4=PD7* row5=PM1  row6=PM4  row7=PM5
 *           *PD7=NMI, unlock required
 * ══════════════════════════════════════════════════════════════════════════ */
#define SCAN_COL0_PIN    GPIO_PIN_4    /* PE4 */
#define SCAN_COL4_PIN    GPIO_PIN_5    /* PE5 */
#define SCAN_ROW0_PIN    GPIO_PIN_0    /* PE0 */
#define SCAN_ROW1_PIN    GPIO_PIN_1    /* PE1 */
#define SCAN_ROW2_PIN    GPIO_PIN_2    /* PE2 */
#define SCAN_ROW3_PIN    GPIO_PIN_3    /* PE3 */
#define SCAN_E_MASK      (GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5)

#define SCAN_COL1_PIN    GPIO_PIN_4    /* PC4 */
#define SCAN_COL2_PIN    GPIO_PIN_5    /* PC5 */
#define SCAN_COL3_PIN    GPIO_PIN_6    /* PC6 */
#define SCAN_COL6_PIN    GPIO_PIN_7    /* PC7 */
#define SCAN_C_MASK      (GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7)

#define SCAN_COL5_PIN    GPIO_PIN_3    /* PD3 */
#define SCAN_ROW5_PIN    GPIO_PIN_1    /* PM1 */
#define SCAN_ROW4_PIN    GPIO_PIN_7    /* PD7 — NMI */
#define SCAN_D_MASK      (GPIO_PIN_3|GPIO_PIN_7)

#define SCAN_COL7_PIN    GPIO_PIN_2    /* PB2 */
#define SCAN_B_MASK      (GPIO_PIN_2)

#define SCAN_ROW6_PIN    GPIO_PIN_4    /* PM4 */
#define SCAN_ROW7_PIN    GPIO_PIN_5    /* PM5 */
#define SCAN_M_MASK      (GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5)

/* ── ENABLE — PB3, always HIGH ──────────────────────────────────────────── */
#define EN_PIN           GPIO_PIN_3
#define EN_BASE          GPIO_PORTB_BASE

/* ══════════════════════════════════════════════════════════════════════════
 * CBA OUTPUTS — to CD4051 muxes
 *   Col CBA: PK0=C(bit2)  PK1=B(bit1)  PK2=A(bit0)
 *   Row CBA: PK3=C(bit2)  PA4=B(bit1)  PA5=A(bit0)
 * ══════════════════════════════════════════════════════════════════════════ */
#define OUT_COL_C_PIN    GPIO_PIN_0    /* PK0 */
#define OUT_COL_B_PIN    GPIO_PIN_1    /* PK1 */
#define OUT_COL_A_PIN    GPIO_PIN_2    /* PK2 */
#define OUT_ROW_C_PIN    GPIO_PIN_3    /* PK3 */
#define OUT_K_MASK       (GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3)
#define OUT_K_BASE       GPIO_PORTK_BASE

#define OUT_ROW_B_PIN    GPIO_PIN_4    /* PA4 */
#define OUT_ROW_A_PIN    GPIO_PIN_5    /* PA5 */
#define OUT_A_MASK       (GPIO_PIN_4|GPIO_PIN_5)
#define OUT_A_BASE       GPIO_PORTA_BASE

/* ══════════════════════════════════════════════════════════════════════════
 * KEY MAP [row][col] — 0-based indices from active scan
 * Space  = KEYS[1][7]  (image row 2, col 8)
 * Return = KEYS[1][6]  (image row 2, col 7)
 * ══════════════════════════════════════════════════════════════════════════ */
static const char KEYS[8][8] = {
    { '\0','\0','\0','\0','\0','\0','\0','\0' },  /* row 0 */
    { '\0','\0','\0','\0','\0','\0','\r',' '  },  /* row 1 — Return(c6) Space(c7) */
    { '\0','\0', 'L', 'D', 'J', 'G', 'A','\0' },  /* row 2 */
    {  'B', 'M', 'I', 'P', 'Y', 'R', 'W','\0' },  /* row 3 */
    {  'C', 'X', 'K', 'S', 'H', 'F', 'Z','\0' },  /* row 4 */
    {  'V', 'N', 'U', 'O', 'T', 'E', 'Q','\0' },  /* row 5 */
    { '\0','\0','\0','\0','\0','\0','\0','\0' },  /* row 6 */
    { '\0','\0','\0','\0','\0','\0','\0','\0' }   /* row 7 */
};

/* ══════════════════════════════════════════════════════════════════════════
 * OUTPUT CBA TABLES — verified from typewriter measurement
 * CBA binary → index: 000=0 001=1 010=2 011=3 100=4 101=5 110=6 111=7
 * ══════════════════════════════════════════════════════════════════════════ */
static const uint8_t OUTPUT_COL[26] = {
 /* A  B  C  D  E  F  G  H  I  J  K  L  M */
    6, 0, 0, 3, 5, 5, 5, 4, 2, 4, 2, 2, 1,
 /* N  O  P  Q  R  S  T  U  V  W  X  Y  Z */
    1, 3, 3, 6, 5, 3, 4, 2, 0, 6, 1, 4, 6
};
static const uint8_t OUTPUT_ROW[26] = {
 /* A  B  C  D  E  F  G  H  I  J  K  L  M */
    2, 3, 4, 2, 5, 4, 2, 4, 3, 2, 4, 2, 3,
 /* N  O  P  Q  R  S  T  U  V  W  X  Y  Z */
    5, 5, 3, 5, 3, 4, 5, 5, 5, 3, 4, 3, 4
};

/* ── Special key injection CBA ──────────────────────────────────────────── */
#define OUTPUT_COL_SPACE    7    /* CBA 111 */
#define OUTPUT_ROW_SPACE    1    /* CBA 001 */
#define OUTPUT_COL_RETURN   6    /* CBA 110 */
#define OUTPUT_ROW_RETURN   1    /* CBA 001 */

/* ── Control panel buttons (active-LOW, WPU) ───────────────────────────── */
#define BTN_MODE_PORT    GPIO_PORTD_BASE
#define BTN_MODE_PIN     GPIO_PIN_2
#define BTN_SELECT_PORT  GPIO_PORTQ_BASE
#define BTN_SELECT_PIN   GPIO_PIN_0
#define BTN_UP_PORT      GPIO_PORTP_BASE
#define BTN_UP_PIN       GPIO_PIN_4
#define BTN_DOWN_PORT    GPIO_PORTN_BASE
#define BTN_DOWN_PIN     GPIO_PIN_5
#define BTN_ENTER_PORT   GPIO_PORTN_BASE
#define BTN_ENTER_PIN    GPIO_PIN_4
#define DEBOUNCE_MS      200

/* ── Control panel LEDs ─────────────────────────────────────────────────── */
#define LED_GREEN_PORT   GPIO_PORTB_BASE
#define LED_GREEN_PIN    GPIO_PIN_4    /* PB4 — green */
#define LED_RED_PORT     GPIO_PORTB_BASE
#define LED_RED_PIN      GPIO_PIN_5    /* PB5 — red   */

/* ── Keypress LED — PM0 (J8 pin 4) ─────────────────────────────────────── */
#define KP_LED_BASE      GPIO_PORTM_BASE
#define KP_LED_PIN       GPIO_PIN_0

/* ── LCD UART  PP1  9600 RS-232 ─────────────────────────────────────────── */
#define LCD_UART_BASE    UART6_BASE
#define LCD_UART_PERIPH  SYSCTL_PERIPH_UART6
#define LCD_GPIO_PERIPH  SYSCTL_PERIPH_GPIOP
#define LCD_GPIO_BASE    GPIO_PORTP_BASE
#define LCD_TX_PIN       GPIO_PIN_1
#define LCD_BAUD         9600
#define LCD_CMD_PREFIX   0xFE
#define LCD_CMD_CLEAR    0x51
#define LCD_CMD_HOME     0x46
#define LCD_CMD_CURSOR_POS  0x45   /* used by LCD.c */
#define LCD_CMD_DISPLAY_ON  0x41   /* used by LCD.c */
#define LCD_CMD_DISPLAY_OFF 0x42   /* used by LCD.c */
#define LCD_CMD_BACKLIGHT   0x53   /* used by LCD.c */
#define LCD_CMD_CONTRAST    0x52

/* ── Debug UART  PA0/PA1  115200 ────────────────────────────────────────── */
/* These names must match exactly what uart_comm.c references               */
#define UART_BASE        UART0_BASE
#define UART_PERIPH      SYSCTL_PERIPH_UART0
#define UART_GPIO_PERIPH SYSCTL_PERIPH_GPIOA
#define UART_GPIO_BASE   GPIO_PORTA_BASE
#define UART_BAUD        115200

/* ── Timing ─────────────────────────────────────────────────────────────── */
#define REST_MS          3000
#define BOOT_CHECK_MS    5000

/* ── Enigma constants ───────────────────────────────────────────────────── */
#define ALPHABET_SIZE        26
#define NUM_ROTORS           3
#define MAX_PLUGBOARD_PAIRS  13
#define PLUGBOARD_LETTERS    26

#endif /* CONFIG_H_ */
