/*******************************************************************************
 * main.c — Enigma MITM Interceptor — Complete Integrated System
 *
 * MITM scan and inject code is UNCHANGED from the verified working version.
 * Added:  Enigma engine, LCD display, control panel, mode-aware processing.
 *
 * Startup sequence:
 *   1. Clock + SysTick
 *   2. Startup blink (PB4 green + PB5 red × 3)
 *   3. Debug UART (PA0/PA1 115200)
 *   4. LCD init  → shows boot message for BOOT_CHECK_MS (5 s system check)
 *   5. Enigma init
 *   6. Control panel init
 *   7. MITM GPIO init (EN_PIN=PB3 goes HIGH here)
 *   8. Plugboard HW init
 *   9. LCD → PASSTHROUGH idle display
 *  10. Main loop
 *
 * Main loop:
 *   - Scan keyboard with verified active row-drive scan
 *   - process_key() selects output based on current mode:
 *       PASSTHROUGH → inject same letter
 *       ENCRYPT     → run Enigma, inject encrypted letter
 *       DECRYPT     → run Enigma, inject decrypted letter
 *   - Space and Return always pass through (no cipher transform)
 *   - control_panel_update() polls buttons, updates LCD, changes mode
 ******************************************************************************/

#include "config.h"
#include "enigma.h"
#include "LCD.h"
#include "uart_comm.h"
#include "control_panel.h"
#include "plugboardhw.h"

/* ── System globals ─────────────────────────────────────────────────────── */
uint32_t               g_ui32SysClock = 0;
volatile uint32_t      g_tickMs       = 0;
volatile OperationMode g_currentMode  = MODE_PASSTHROUGH;
bool                   g_typing_started = false;

static EnigmaState enigma;

/* ── Message buffer ─────────────────────────────────────────────────────── */
#define MSG_BUF_LEN 20
static char    msg_input [MSG_BUF_LEN+1];
static char    msg_output[MSG_BUF_LEN+1];
static uint8_t msg_len = 0;

static void msg_clear(void)
{ msg_len=0; msg_input[0]='\0'; msg_output[0]='\0'; }

static void msg_append(char in, char out)
{
    if(msg_len < MSG_BUF_LEN){
        msg_input[msg_len]=in; msg_output[msg_len]=out;
        msg_len++; msg_input[msg_len]='\0'; msg_output[msg_len]='\0';
    }
}

/* ── uart_rx_callback stub (required by uart_comm.c linker) ─────────────── */
void uart_rx_callback(char c) { (void)c; }

/* ── SysTick ─────────────────────────────────────────────────────────────── */
void SysTick_Handler(void) { g_tickMs++; }

static void delay_ms(uint32_t ms)
{ uint32_t s=g_tickMs; while((g_tickMs-s)<ms); }

static void delay_us(uint32_t us)
{ volatile uint32_t i; while(us--) for(i=0;i<20;i++){__asm(" nop");} }

/* ── Startup blink ───────────────────────────────────────────────────────── */
static void startup_blink(void)
{
    int i;
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB));
    MAP_GPIOPinTypeGPIOOutput(LED_GREEN_PORT, LED_GREEN_PIN);
    MAP_GPIOPinTypeGPIOOutput(LED_RED_PORT,   LED_RED_PIN);
    for(i=0;i<3;i++){
        MAP_GPIOPinWrite(LED_GREEN_PORT, LED_GREEN_PIN, LED_GREEN_PIN);
        MAP_GPIOPinWrite(LED_RED_PORT,   LED_RED_PIN,   LED_RED_PIN);
        delay_ms(200);
        MAP_GPIOPinWrite(LED_GREEN_PORT, LED_GREEN_PIN, 0);
        MAP_GPIOPinWrite(LED_RED_PORT,   LED_RED_PIN,   0);
        delay_ms(200);
    }
}

/* ── PD7 NMI unlock ─────────────────────────────────────────────────────── */
static void unlock_pd7(void)
{
    HWREG(GPIO_PORTD_BASE + GPIO_LOCK_OFFSET) = GPIO_LOCK_KEY_VAL;
    HWREG(GPIO_PORTD_BASE + GPIO_CR_OFFSET)  |= GPIO_PIN_7;
    HWREG(GPIO_PORTD_BASE + GPIO_LOCK_OFFSET) = 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * ROW PIN LOOKUP — unchanged from working MITM
 * ══════════════════════════════════════════════════════════════════════════ */
static const uint32_t ROW_BASE[8] = {
    GPIO_PORTE_BASE, GPIO_PORTE_BASE, GPIO_PORTE_BASE, GPIO_PORTE_BASE,
    GPIO_PORTD_BASE, GPIO_PORTM_BASE, GPIO_PORTM_BASE, GPIO_PORTM_BASE
};
static const uint8_t ROW_PIN[8] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,
    GPIO_PIN_7, GPIO_PIN_1, GPIO_PIN_4, GPIO_PIN_5
};

/* ── MITM GPIO init ──────────────────────────────────────────────────────── */
static void kb_mitm_gpio_init(void)
{
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOK);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOM);
    /* Port A and B already enabled by uart_init / startup_blink */
    while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC));
    while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD));
    while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE));
    while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOK));
    while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOM));

    unlock_pd7();

    /* Row pins — all input (high-Z) to start */
    MAP_GPIOPinTypeGPIOInput(GPIO_PORTE_BASE,
        SCAN_ROW0_PIN|SCAN_ROW1_PIN|SCAN_ROW2_PIN|SCAN_ROW3_PIN);
    MAP_GPIOPinTypeGPIOInput(GPIO_PORTD_BASE, SCAN_ROW4_PIN);
    MAP_GPIOPinTypeGPIOInput(GPIO_PORTM_BASE, SCAN_ROW5_PIN|SCAN_ROW6_PIN|SCAN_ROW7_PIN);

    /* Col pins — input + WPU (idle HIGH, pressed = LOW) */
    MAP_GPIOPinTypeGPIOInput(GPIO_PORTE_BASE, SCAN_COL0_PIN|SCAN_COL4_PIN);
    MAP_GPIOPadConfigSet(GPIO_PORTE_BASE, SCAN_COL0_PIN|SCAN_COL4_PIN,
                         GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    MAP_GPIOPinTypeGPIOInput(GPIO_PORTC_BASE, SCAN_C_MASK);
    MAP_GPIOPadConfigSet(GPIO_PORTC_BASE, SCAN_C_MASK,
                         GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    MAP_GPIOPinTypeGPIOInput(GPIO_PORTD_BASE, SCAN_COL5_PIN);
    MAP_GPIOPadConfigSet(GPIO_PORTD_BASE, SCAN_COL5_PIN,
                         GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
    MAP_GPIOPinTypeGPIOInput(GPIO_PORTB_BASE, SCAN_COL7_PIN);
    MAP_GPIOPadConfigSet(GPIO_PORTB_BASE, SCAN_COL7_PIN,
                         GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    /* EN_PIN PB3 — output HIGH permanently */
    MAP_GPIOPinTypeGPIOOutput(EN_BASE, EN_PIN);
    MAP_GPIOPinWrite(EN_BASE, EN_PIN, EN_PIN);

    /* Keypress LED PM0 — output, start OFF */
    MAP_GPIOPinTypeGPIOOutput(KP_LED_BASE, KP_LED_PIN);
    MAP_GPIOPinWrite(KP_LED_BASE, KP_LED_PIN, 0);

    /* CBA outputs — start all LOW */
    MAP_GPIOPinTypeGPIOOutput(OUT_K_BASE, OUT_K_MASK);
    MAP_GPIOPinWrite(OUT_K_BASE, OUT_K_MASK, 0);
    MAP_GPIOPinTypeGPIOOutput(OUT_A_BASE, OUT_A_MASK);
    MAP_GPIOPinWrite(OUT_A_BASE, OUT_A_MASK, 0);
}

/* ══════════════════════════════════════════════════════════════════════════
 * SCAN FUNCTIONS — UNCHANGED FROM WORKING MITM
 * ══════════════════════════════════════════════════════════════════════════ */
static void drive_row_low(uint8_t row)
{
    uint8_t r;
    for(r=0;r<8;r++) MAP_GPIOPinTypeGPIOInput(ROW_BASE[r], ROW_PIN[r]);
    MAP_GPIOPinTypeGPIOOutput(ROW_BASE[row], ROW_PIN[row]);
    MAP_GPIOPinWrite(ROW_BASE[row], ROW_PIN[row], 0);
}

static void rows_all_input(void)
{ uint8_t r; for(r=0;r<8;r++) MAP_GPIOPinTypeGPIOInput(ROW_BASE[r], ROW_PIN[r]); }

static uint8_t read_col(uint8_t col)
{
    switch(col){
        case 0: return MAP_GPIOPinRead(GPIO_PORTE_BASE, SCAN_COL0_PIN) ? 1 : 0;
        case 1: return MAP_GPIOPinRead(GPIO_PORTC_BASE, SCAN_COL1_PIN) ? 1 : 0;
        case 2: return MAP_GPIOPinRead(GPIO_PORTC_BASE, SCAN_COL2_PIN) ? 1 : 0;
        case 3: return MAP_GPIOPinRead(GPIO_PORTC_BASE, SCAN_COL3_PIN) ? 1 : 0;
        case 4: return MAP_GPIOPinRead(GPIO_PORTE_BASE, SCAN_COL4_PIN) ? 1 : 0;
        case 5: return MAP_GPIOPinRead(GPIO_PORTD_BASE, SCAN_COL5_PIN) ? 1 : 0;
        case 6: return MAP_GPIOPinRead(GPIO_PORTC_BASE, SCAN_COL6_PIN) ? 1 : 0;
        case 7: return MAP_GPIOPinRead(GPIO_PORTB_BASE, SCAN_COL7_PIN) ? 1 : 0;
        default: return 1;
    }
}

static char scan_once(uint8_t *row_out, uint8_t *col_out)
{
    uint8_t row, col;
    for(row=0;row<8;row++){
        drive_row_low(row);
        delay_us(20);
        for(col=0;col<8;col++){
            if(read_col(col)==0){
                rows_all_input();
                if(row_out) *row_out=row;
                if(col_out) *col_out=col;
                return KEYS[row][col];
            }
        }
    }
    rows_all_input();
    return '\0';
}

static char get_key(uint8_t *row_out, uint8_t *col_out)
{
    uint8_t r1=0,c1=0,r2=0,c2=0;
    char first=scan_once(&r1,&c1);
    if(first=='\0') return '\0';
    delay_ms(20);
    char second=scan_once(&r2,&c2);
    if(first!=second) return '\0';
    if(row_out) *row_out=r1;
    if(col_out) *col_out=c1;
    return first;
}

/* ══════════════════════════════════════════════════════════════════════════
 * CBA INJECT FUNCTIONS — UNCHANGED FROM WORKING MITM
 * ══════════════════════════════════════════════════════════════════════════ */
static void set_cba(uint8_t col_idx, uint8_t row_idx)
{
    uint8_t k=0;
    if(col_idx & 0x04u) k|=OUT_COL_C_PIN;
    if(col_idx & 0x02u) k|=OUT_COL_B_PIN;
    if(col_idx & 0x01u) k|=OUT_COL_A_PIN;
    if(row_idx & 0x04u) k|=OUT_ROW_C_PIN;
    MAP_GPIOPinWrite(OUT_K_BASE, OUT_K_MASK, k);
    uint8_t a=0;
    if(row_idx & 0x02u) a|=OUT_ROW_B_PIN;
    if(row_idx & 0x01u) a|=OUT_ROW_A_PIN;
    MAP_GPIOPinWrite(OUT_A_BASE, OUT_A_MASK, a);
}

static void clear_cba(void)
{
    MAP_GPIOPinWrite(OUT_K_BASE, OUT_K_MASK, 0);
    MAP_GPIOPinWrite(OUT_A_BASE, OUT_A_MASK, 0);
}

/* ── UART helpers ────────────────────────────────────────────────────────── */
static void print_cba(uint8_t v)
{
    uart_putchar('0'+((v>>2)&1));
    uart_putchar('0'+((v>>1)&1));
    uart_putchar('0'+( v    &1));
}

/* ── Enigma trace printer ────────────────────────────────────────────────── */
static void print_trace(const EnigmaTrace *t, const char *ms)
{
    uint8_t step=0; char s[4];
    uart_puts("[");uart_puts(ms);uart_puts("] ");
    uart_putchar(t->input);uart_puts(" => ");uart_putchar(t->after_pb_out);
    s[0]='0'+t->total_changes;s[1]='\0';
    uart_puts(" (");uart_puts(s);uart_puts(" changes)\r\n");
    if(t->pb_in_active){
        step++;s[0]='0'+step;s[1]='\0';
        uart_puts("  ");uart_puts(s);uart_puts(". PB1: ");
        uart_putchar(t->input);uart_puts("->");uart_putchar(t->after_pb_in);uart_puts("\r\n");
    }
    step++;s[0]='0'+step;s[1]='\0';
    uart_puts("  ");uart_puts(s);uart_puts(". Rfwd: ");
    uart_putchar(t->pb_in_active?t->after_pb_in:t->input);uart_puts("->");uart_putchar(t->after_r_right_fwd);uart_puts("\r\n");
    step++;s[0]='0'+step;s[1]='\0';
    uart_puts("  ");uart_puts(s);uart_puts(". Mfwd: ");
    uart_putchar(t->after_r_right_fwd);uart_puts("->");uart_putchar(t->after_r_mid_fwd);uart_puts("\r\n");
    step++;s[0]='0'+step;s[1]='\0';
    uart_puts("  ");uart_puts(s);uart_puts(". Lfwd: ");
    uart_putchar(t->after_r_mid_fwd);uart_puts("->");uart_putchar(t->after_r_left_fwd);uart_puts("\r\n");
    step++;s[0]='0'+step;s[1]='\0';
    uart_puts("  ");uart_puts(s);uart_puts(". Refl: ");
    uart_putchar(t->after_r_left_fwd);uart_puts("->");uart_putchar(t->after_reflector);uart_puts("\r\n");
    step++;s[0]='0'+step;s[1]='\0';
    uart_puts("  ");uart_puts(s);uart_puts(". Lrev: ");
    uart_putchar(t->after_reflector);uart_puts("->");uart_putchar(t->after_r_left_rev);uart_puts("\r\n");
    step++;s[0]='0'+step;s[1]='\0';
    uart_puts("  ");uart_puts(s);uart_puts(". Mrev: ");
    uart_putchar(t->after_r_left_rev);uart_puts("->");uart_putchar(t->after_r_mid_rev);uart_puts("\r\n");
    step++;s[0]='0'+step;s[1]='\0';
    uart_puts("  ");uart_puts(s);uart_puts(". Rrev: ");
    uart_putchar(t->after_r_mid_rev);uart_puts("->");uart_putchar(t->after_r_right_rev);uart_puts("\r\n");
    if(t->pb_out_active){
        step++;s[0]='0'+step;s[1]='\0';
        uart_puts("  ");uart_puts(s);uart_puts(". PB2: ");
        uart_putchar(t->after_r_right_rev);uart_puts("->");uart_putchar(t->after_pb_out);uart_puts("\r\n");
    }
    uart_puts("\r\n");
}

/* ══════════════════════════════════════════════════════════════════════════
 * PROCESS KEY — mode-aware, injects via verified MITM functions
 * ══════════════════════════════════════════════════════════════════════════ */
static void process_key(char key_in, uint8_t raw_row, uint8_t raw_col)
{
    char    out;
    uint8_t out_col, out_row;
    bool    is_special = (key_in == ' ' || key_in == '\r');

    /* ── Determine output character ── */
    if(is_special || g_currentMode == MODE_PASSTHROUGH)
    {
        out = key_in;

        if(is_special)
            uart_puts(key_in==' ' ? "[SPACE]\r\n" : "[RETURN]\r\n");
        else {
            uart_puts("[PASS] "); uart_putchar(key_in); uart_puts("\r\n");
        }

        if(!g_typing_started) control_panel_start_typing(&enigma);
        control_panel_typing_update(&enigma, key_in, out);
    }
    else
    {
        /* ENCRYPT / DECRYPT — run Enigma */
        EnigmaTrace trace;
        const char *ms = (g_currentMode==MODE_ENCRYPT) ? "ENC" : "DEC";
        out = enigma_encrypt_char_traced(&enigma, key_in, &trace);
        print_trace(&trace, ms);

        if(!g_typing_started) control_panel_start_typing(&enigma);
        control_panel_typing_update(&enigma, key_in, out);
    }

    msg_append(key_in, out);

    /* ── Select injection CBA ── */
    if(out == ' '){
        out_col = OUTPUT_COL_SPACE;  out_row = OUTPUT_ROW_SPACE;
    } else if(out == '\r'){
        out_col = OUTPUT_COL_RETURN; out_row = OUTPUT_ROW_RETURN;
    } else if(out >= 'A' && out <= 'Z'){
        out_col = OUTPUT_COL[out-'A'];
        out_row = OUTPUT_ROW[out-'A'];
    } else {
        out_col = raw_col; out_row = raw_row;
    }

    /* ── Inject (using verified set_cba / scan_once) ── */
    set_cba(out_col, out_row);
    MAP_GPIOPinWrite(KP_LED_BASE, KP_LED_PIN, KP_LED_PIN);

    uart_puts("[INJECT ON]  KEY_IN=");
    if(key_in==' ') uart_puts("SPC");
    else if(key_in=='\r') uart_puts("RET");
    else uart_putchar(key_in);
    uart_puts(" KEY_OUT=");
    if(out==' ') uart_puts("SPC");
    else if(out=='\r') uart_puts("RET");
    else uart_putchar(out);
    uart_puts("  COL_CBA="); print_cba(out_col);
    uart_puts("  ROW_CBA="); print_cba(out_row);
    uart_puts("\r\n");

    /* Hold while key physically held — same as working MITM */
    while(scan_once(0,0) != '\0') delay_ms(1);

    clear_cba();
    MAP_GPIOPinWrite(KP_LED_BASE, KP_LED_PIN, 0);
    uart_puts("[INJECT OFF]\r\n");

    /* Return key: save message, reset rotors */
    if(key_in == '\r' && msg_len > 0){
        char tag = (g_currentMode==MODE_ENCRYPT)?'E':
                   (g_currentMode==MODE_DECRYPT)?'D':'P';
        control_panel_history_add(tag, msg_input, msg_output, msg_len);
        uart_puts("[MSG] Saved\r\n");
        msg_clear();
        g_typing_started = false;
        enigma_reset_to_daily(&enigma);
        uart_puts("--- message end ---\r\n\r\n");
    }
}

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(void)
{
    /* 1. Clock + SysTick */
    g_ui32SysClock = MAP_SysCtlClockFreqSet(
        SYSCTL_XTAL_25MHZ|SYSCTL_OSC_MAIN|SYSCTL_USE_PLL|SYSCTL_CFG_VCO_480,
        120000000);
    MAP_SysTickPeriodSet(g_ui32SysClock/1000);
    MAP_SysTickIntEnable();
    MAP_SysTickEnable();
    MAP_IntMasterEnable();

    /* 2. Startup blink */
    startup_blink();

    /* 3. Debug UART */
    uart_init();
    uart_puts("\r\n=== ENIGMA MITM INTERCEPTOR ===\r\n");
    uart_puts("Booting — 5 s system check...\r\n");

    /* 4. LCD init — shows boot message during 5 s system check */
    LCD_UART_Init(g_ui32SysClock);
    LCD_Init();
    LCD_Clear();
    LCD_SetCursor(0);
    LCD_Print("  ENIGMA MACHINE  ");
    LCD_SetCursor(20);
    LCD_Print("  System Check... ");
    LCD_SetCursor(40);
    LCD_Print("  Please wait     ");
    delay_ms(BOOT_CHECK_MS);   /* 5 second system check window */

    /* 5. Enigma init */
    enigma_init(&enigma, 1);
    enigma_set_daily_settings(&enigma, 0, 0, 0);
    enigma_set_positions(&enigma, 0, 0, 0);

    /* 6. Control panel init → LCD updates to PASSTHROUGH idle display */
    control_panel_init();
    control_panel_update_display(&enigma);
    msg_clear();

    /* 7. MITM GPIO init — EN_PIN (PB3) goes HIGH here */
    kb_mitm_gpio_init();

    /* 8. Plugboard HW init */
    plugboard_hw_init();

    uart_puts("Ready. Mode: PASSTHROUGH\r\n\r\n");

    while(1)
    {
        /* ── 1. Scan keyboard ── */
        uint8_t row=0, col=0;
        char key = get_key(&row, &col);

        if(key >= 'A' && key <= 'Z' || key == ' ' || key == '\r')
        {
            process_key(key, row, col);
        }

        /* ── 2. Control panel — buttons, LCD, mode changes ── */
        {
            bool was_typing  = control_panel_typing_active();
            OperationMode prev = g_currentMode;

            control_panel_update(&enigma);

            /* Auto-save when leaving typing screen mid-message */
            if(was_typing && !control_panel_typing_active() && msg_len>0){
                char tag = (prev==MODE_ENCRYPT)?'E':
                           (prev==MODE_DECRYPT)?'D':'P';
                control_panel_history_add(tag, msg_input, msg_output, msg_len);
                uart_puts("[MSG] Auto-saved\r\n");
                msg_clear();
                g_typing_started = false;
                if(prev==MODE_ENCRYPT||prev==MODE_DECRYPT)
                    enigma_reset_to_daily(&enigma);
            }
        }

        delay_ms(10);
    }
}
