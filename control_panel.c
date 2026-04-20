/******************************************************************************
 * control_panel.c
 *
 * CHANGES IN THIS VERSION:
 *
 * 1. RING POSITION RESTORE ON RESET:
 *    In UI_RESET_MODE_SEL when SELECT is pressed, enigma_reset_to_daily(e)
 *    is called before typing_display_init(). This restores rotor_positions[]
 *    to daily_settings[] (the Grundstellung set via Ring Pos config).
 *    Example: if Ring was set to F,C,B, then after typing a message the
 *    rotors advance. Reset → Keep → confirm mode → positions go back to F,C,B.
 *
 * 2. CONFIG ACCESSIBLE FROM ALL MODES:
 *    ENTER always opens config menu regardless of current mode.
 *    Config shows 5 items in PASSTHROUGH (no Reset/Restart).
 *    Config shows 6 items in ENCRYPT/DECRYPT (adds "Reset/Restart" at item 5).
 *    History is item 4 in all modes.
 *
 * 3. BUTTON MAP:
 *    MODE   : PASSTHROUGH → ENCRYPT → DECRYPT (3-cycle)
 *    SELECT : cycle selected rotor slot in normal/typing
 *    UP/DOWN: adjust selected rotor position in normal/typing
 *    ENTER  : always opens config menu
 ******************************************************************************/
#include "control_panel.h"
#include "LCD.h"
#include "mitm.h"
#include "uart_comm.h"
#include "plugboardhw.h"
#include <string.h>

volatile bool g_btnModeFlag   = false;
volatile bool g_btnSelectFlag = false;
volatile bool g_btnUpFlag     = false;
volatile bool g_btnDownFlag   = false;
volatile bool g_btnEnterFlag  = false;
/* g_typing_started defined in main.c */

static uint32_t last_press[5]={0};
static const uint32_t btn_ports[5]={BTN_MODE_PORT,BTN_SELECT_PORT,BTN_UP_PORT,BTN_DOWN_PORT,BTN_ENTER_PORT};
static const uint32_t btn_pins[5] ={BTN_MODE_PIN, BTN_SELECT_PIN, BTN_UP_PIN, BTN_DOWN_PIN, BTN_ENTER_PIN};
static bool last_state[5]={false};

typedef enum {
    UI_NORMAL, UI_TYPING,
    UI_CONFIG_SELECT,
    UI_ROTOR_SELECT, UI_REFLECTOR_SELECT, UI_RING_SET,
    UI_PLUG_MENU, UI_PLUG_METHOD, UI_PLUG_PRESET, UI_PLUG_COUNT,
    UI_PLUG_PAIR_A, UI_PLUG_PAIR_B,
    UI_PLUG_PHYSICAL,               /* scan physical jacks, SEL=apply+exit */
    UI_RESET_MENU, UI_RESET_MODE_SEL,
    UI_HISTORY
} UIState;

static UIState  ui_state=UI_NORMAL;
static uint8_t  config_cursor=0, rotor_slot=2, sel_slot=2, typing_col=0;
static uint8_t  tmp_rotor[3], tmp_reflector=1, tmp_ring[3];
static uint8_t  reset_cursor=0, mode_sel_cursor=0;
static bool     config_from_reset=false;
static uint8_t  plug_stage,plug_menu_cursor,plug_method_cursor,plug_day=0;
static uint8_t  plug_count,plug_pair_idx,plug_letter_a,plug_letter_b;
static bool     plug_used[26];
static uint8_t  tmp_pb_in[26],tmp_pb_out[26],tmp_pb_count_in,tmp_pb_count_out;

/* ── Physical plugboard scan snapshot ─────────────────────────────────────── */
static PBoardPair phys_pairs[PBOARD_MAX_PAIRS];
static uint8_t    phys_count = 0;
static uint8_t    phys_last_count = 255;   /* force first redraw */
static bool       phys_inserted[PBOARD_NUM_JACKS]; /* which jacks have a cable in */
static bool       phys_last_inserted[PBOARD_NUM_JACKS]; /* prev inserted state */

/* ============================================================
 * HISTORY
 * ============================================================ */
#define MAX_HISTORY   8
#define HIST_MSG_LEN  20

typedef struct {
    char    mode;
    char    input [HIST_MSG_LEN+1];
    char    output[HIST_MSG_LEN+1];
    uint8_t len;
} HistEntry;

static HistEntry hist[MAX_HISTORY];
static uint8_t   hist_count=0, hist_head=0, hist_view=0;

void control_panel_history_add(char mode,const char*input,const char*output,uint8_t len)
{
    uint8_t idx,i,n;
    if(hist_count<MAX_HISTORY){idx=hist_count++;}
    else{idx=hist_head;hist_head=(hist_head+1)%MAX_HISTORY;}
    hist[idx].mode=mode; hist[idx].len=len;
    n=len<HIST_MSG_LEN?len:HIST_MSG_LEN;
    for(i=0;i<n;i++){hist[idx].input[i]=input[i];hist[idx].output[i]=output[i];}
    hist[idx].input[n]='\0'; hist[idx].output[n]='\0';
}

static const HistEntry* hist_get(uint8_t disp_idx)
{
    uint8_t real;
    if(disp_idx>=hist_count)return 0;
    real=(hist_count<MAX_HISTORY)?(hist_count-1-disp_idx):(hist_head+hist_count-1-disp_idx)%MAX_HISTORY;
    return &hist[real];
}

/* ============================================================
 * LOOKUP TABLES
 * ============================================================ */
static const char *rotor_names[5]={"I","II","III","IV","V"};
static const uint8_t rotor_id_list[5]={ROTOR_I,ROTOR_II,ROTOR_III,ROTOR_IV,ROTOR_V};
static const char *refl_names[3]={"A","B","C"};

static const char *plug_days[31]={
    "BF SD AY HG OU QC WI RL XP ZK","DI ZL RX UH QK PC YV OA SO EM",
    "ZM BQ TP YX FK AR WH SO NJ IG","NE MT RL OY HV IU GK FW PZ XC",
    "NE MT RL OY HV IU GK FW PZ XC","BF GR SZ OM WQ TY ME JU XN KD",
    "GS VD CA LK HI BO JP UZ PT RN","KA ZH QP GR MF LJ OT EN HD YW",
    "PI KM JB YU QS OV ZA GW CH XF","SX TD QP HU FB YN CO TK WE GZ",
    "GP XH IW BO NU MD SA ZK QR LT","XC AQ OT UZ HD RG KM BL NS JW",
    "PO TV QC ZS RX WR BJ DK FU LA","HA GM DI VK JF YU EF TB ZL XQ",
    "XF PE SQ GR AJ UO CN BV TM KI","UT ZC YN BE PX JX RS GF IA QH",
    "IN YJ SD UV GF BH TK QE AR OP","TM IJ VK OY NX PR WL GA BU SF",
    "WT RE PC TY JA VD OI HK NX ZS","AN IV LH YP WM TR XU FO ZB ED",
    "BA CI DH EG FL KM NO PQ RT VX","BC DJ EI FH GK LN MO PQ RS UW",
    "BD EK FJ GI HL MN OP QR ST UV","BE FL GK HJ IM NO PQ RS TU VW",
    "BF GM HK IJ LN OP QR ST UV WX","BG HN IK JL MO PQ RS TU VW XY",
    "BH IN JK LM NO PQ RS TU VW XZ","BI JN KL MN OP QR ST UV WX YZ",
    "BJ KN LM NO PQ RS TU VW XY ZA","BK LN MO NP QR ST UV WX YZ AB",
    "BL MN NO OP QR ST UV WX YZ AC"
};
/* ============================================================
 * LCD HELPERS
 * ============================================================ */
static void lcd_putc(char c){char b[2]={c,'\0'};LCD_Print(b);}
static void lcd_u8(uint8_t n){char buf[4];int i=3;buf[i]='\0';if(n==0){buf[--i]='0';}else{while(n>0){buf[--i]='0'+(n%10);n/=10;}}LCD_Print(&buf[i]);}
static void lcd_pad(const char*s,uint8_t w){uint8_t i=0;while(s[i]&&i<w){lcd_putc(s[i]);i++;}while(i<w){lcd_putc(' ');i++;}}

/* ============================================================
 * UART HELPERS
 * ============================================================ */
static void uart_putc_h(char c){uart_putchar(c);}
static void uart_print_pb(const uint8_t*pb,uint8_t cnt){
    uint8_t i;if(cnt==0){uart_puts("(none) ");return;}
    for(i=0;i<26;i++){if(pb[i]>i){uart_putc_h('A'+i);uart_putc_h('A'+pb[i]);uart_putc_h(' ');}}
}

/* ============================================================
 * LEDs
 * ============================================================ */
void control_panel_led_green(bool on){MAP_GPIOPinWrite(LED_GREEN_PORT,LED_GREEN_PIN,on?LED_GREEN_PIN:0);}
void control_panel_led_red(bool on)  {MAP_GPIOPinWrite(LED_RED_PORT,  LED_RED_PIN,  on?LED_RED_PIN:0);}
static void update_leds(void){
    switch(g_currentMode){
        case MODE_ENCRYPT:control_panel_led_green(true); control_panel_led_red(false);break;
        case MODE_DECRYPT:control_panel_led_green(false);control_panel_led_red(true); break;
        default:          control_panel_led_green(false);control_panel_led_red(false);break;
    }
}

/* ============================================================
 * NORMAL DISPLAY
 * ============================================================ */
static void display_normal(EnigmaState *e){
    uint8_t s;
    LCD_Clear();
    LCD_SetCursor(LCD_LINE1);LCD_Print("   ENIGMA MACHINE   ");
    LCD_SetCursor(LCD_LINE2);
    switch(g_currentMode){
        case MODE_ENCRYPT:LCD_Print("Mode: ENCRYPT       ");break;
        case MODE_DECRYPT:LCD_Print("Mode: DECRYPT       ");break;
        default:          LCD_Print("Mode: PASSTHROUGH   ");break;
    }
    LCD_SetCursor(LCD_LINE3);
    LCD_Print("L:");if(sel_slot==0)LCD_Print("[");LCD_Print(enigma_rotor_name(e->rotor_ids[0]));if(sel_slot==0)LCD_Print("]");
    LCD_Print(" M:");if(sel_slot==1)LCD_Print("[");LCD_Print(enigma_rotor_name(e->rotor_ids[1]));if(sel_slot==1)LCD_Print("]");
    LCD_Print(" R:");if(sel_slot==2)LCD_Print("[");LCD_Print(enigma_rotor_name(e->rotor_ids[2]));if(sel_slot==2)LCD_Print("]");
    LCD_Print("       ");
    LCD_SetCursor(LCD_LINE4);
    for(s=0;s<3;s++){if(sel_slot==s)LCD_Print(">");lcd_putc('A'+e->rotor_positions[s]);if(sel_slot==s)LCD_Print("<");else LCD_Print(" ");LCD_Print("  ");}
    LCD_Print("PB:");lcd_u8(e->num_plug_pairs_in);LCD_Print("+");lcd_u8(e->num_plug_pairs_out);LCD_Print(" ");
}

/* ============================================================
 * RESET MENU
 * ============================================================ */
static void display_reset_menu(void){
    LCD_Clear();
    LCD_SetCursor(LCD_LINE1);LCD_Print("   -- RESET MENU -- ");
    LCD_SetCursor(LCD_LINE2);LCD_Print(reset_cursor==0?"> Keep prev settings":"  Keep prev settings");
    LCD_SetCursor(LCD_LINE3);LCD_Print(reset_cursor==1?"> Change settings   ":"  Change settings   ");
    LCD_SetCursor(LCD_LINE4);LCD_Print(reset_cursor==2?"> Restart to default":"  Restart to default");
}

static void display_mode_sel(void){
    LCD_Clear();
    LCD_SetCursor(LCD_LINE1);LCD_Print("   Select Mode:     ");
    LCD_SetCursor(LCD_LINE2);LCD_Print(mode_sel_cursor==0?"> ENCRYPT           ":"  ENCRYPT           ");
    LCD_SetCursor(LCD_LINE3);LCD_Print(mode_sel_cursor==1?"> DECRYPT           ":"  DECRYPT           ");
    LCD_SetCursor(LCD_LINE4);LCD_Print("  SELECT to confirm ");
}

/* ============================================================
 * TYPING DISPLAY
 * Line 1: "ENC A    B    C     " = 4+1+4+1+4+6 = 20
 * Line 4: "I-II-III  PB:5+3   " (no Rf to save space)
 * ============================================================ */
#define POS_COL_L   4
#define POS_COL_M   9
#define POS_COL_R  14

static void draw_typing_l1(EnigmaState *e){
    LCD_SetCursor(LCD_LINE1);
    LCD_Print(g_currentMode==MODE_ENCRYPT?"ENC ":"DEC ");
    lcd_putc('A'+e->rotor_positions[0]);LCD_Print("    ");
    lcd_putc('A'+e->rotor_positions[1]);LCD_Print("    ");
    lcd_putc('A'+e->rotor_positions[2]);LCD_Print("     ");
}
static void draw_typing_l4(EnigmaState *e){
    LCD_SetCursor(LCD_LINE4);
    LCD_Print(enigma_rotor_name(e->rotor_ids[0]));LCD_Print("-");
    LCD_Print(enigma_rotor_name(e->rotor_ids[1]));LCD_Print("-");
    LCD_Print(enigma_rotor_name(e->rotor_ids[2]));
    LCD_Print("  PB:");lcd_u8(e->num_plug_pairs_in);LCD_Print("+");lcd_u8(e->num_plug_pairs_out);
    LCD_Print("          ");
}
static void typing_display_init(EnigmaState *e){
    extern bool g_typing_started;
    typing_col=0;LCD_Clear();
    draw_typing_l1(e);
    LCD_SetCursor(LCD_LINE2);LCD_Print("                    ");
    LCD_SetCursor(LCD_LINE3);LCD_Print("                    ");
    draw_typing_l4(e);
    ui_state=UI_TYPING;
    g_typing_started=true;
}
void control_panel_start_typing(EnigmaState *e){typing_display_init(e);}
void control_panel_typing_update(EnigmaState *e,char in_c,char out_c){
    LCD_SetCursor(LCD_LINE1+POS_COL_L);lcd_putc('A'+e->rotor_positions[0]);
    LCD_SetCursor(LCD_LINE1+POS_COL_M);lcd_putc('A'+e->rotor_positions[1]);
    LCD_SetCursor(LCD_LINE1+POS_COL_R);lcd_putc('A'+e->rotor_positions[2]);
    LCD_SetCursor(LCD_LINE2+typing_col);lcd_putc(in_c);
    LCD_SetCursor(LCD_LINE3+typing_col);lcd_putc(out_c);
    typing_col++;
    if(typing_col>=20){typing_col=0;LCD_SetCursor(LCD_LINE2);LCD_Print("                    ");LCD_SetCursor(LCD_LINE3);LCD_Print("                    ");}
}
bool control_panel_in_typing_state(void){return(ui_state==UI_TYPING||ui_state==UI_NORMAL);}
/* Returns true ONLY when actively in the typing display (not normal idle) */
bool control_panel_typing_active(void){return ui_state==UI_TYPING;}

/* ============================================================
 * HISTORY DISPLAY
 * ============================================================ */
static void display_history(void){
    const HistEntry*h; uint8_t i;
    LCD_Clear();
    if(hist_count==0){
        LCD_SetCursor(LCD_LINE1);LCD_Print("  Message History   ");
        LCD_SetCursor(LCD_LINE2);LCD_Print("  No messages yet   ");
        LCD_SetCursor(LCD_LINE3);LCD_Print("                    ");
        LCD_SetCursor(LCD_LINE4);LCD_Print("  ENTER to go back  ");
        return;
    }
    h=hist_get(hist_view);
    LCD_SetCursor(LCD_LINE1);
    LCD_Print("[");lcd_u8(hist_view+1);LCD_Print("/");lcd_u8(hist_count);LCD_Print("] ");
    switch(h->mode){case 'E':LCD_Print("ENCRYPT        ");break;case 'D':LCD_Print("DECRYPT        ");break;default:LCD_Print("PASSTHROUGH    ");break;}
    LCD_SetCursor(LCD_LINE2);LCD_Print("IN:  ");
    i=0;while(h->input[i]&&i<15){lcd_putc(h->input[i]);i++;}while(i<15){lcd_putc(' ');i++;}
    LCD_SetCursor(LCD_LINE3);LCD_Print("OUT: ");
    i=0;while(h->output[i]&&i<15){lcd_putc(h->output[i]);i++;}while(i<15){lcd_putc(' ');i++;}
    LCD_SetCursor(LCD_LINE4);LCD_Print("UP/DW=prv/nxt ENT=ok");
}

/* ============================================================
 * CONFIG DISPLAY
 * 5 items in PASSTHROUGH, 6 items in ENC/DEC.
 * Items: 0=Rotors 1=Reflector 2=Plugboard 3=Ring Pos 4=History
 *        5=Reset/Restart (ENC/DEC only)
 * ============================================================ */
static uint8_t config_n(void){
    return (g_currentMode==MODE_PASSTHROUGH)?5:6;
}
static void display_config(void){
    uint8_t N=config_n();
    const char*items[6]={"Rotors","Reflector","Plugboard","Ring Pos","History","Reset/Restart"};
    uint8_t base,i;
    /* clamp cursor */
    if(config_cursor>=N)config_cursor=N-1;
    /* base: first of 3 visible items, range [0, N-3] */
    base=config_cursor==0?0:(config_cursor>=N-2?N-3:config_cursor-1);
    LCD_Clear();LCD_SetCursor(LCD_LINE1);LCD_Print("-- CONFIGURATION -- ");
    for(i=0;i<3;i++){
        uint8_t idx=base+i;
        switch(i){case 0:LCD_SetCursor(LCD_LINE2);break;case 1:LCD_SetCursor(LCD_LINE3);break;default:LCD_SetCursor(LCD_LINE4);break;}
        LCD_Print(idx==config_cursor?"> ":"  ");lcd_pad(items[idx],18);
    }
}

/* ============================================================
 * ROTOR / REFLECTOR / RING
 * ============================================================ */
static void display_rotor_select(void){
    int i,idx=0;for(i=0;i<5;i++)if(tmp_rotor[rotor_slot]==rotor_id_list[i])idx=i;
    LCD_Clear();LCD_SetCursor(LCD_LINE1);
    switch(rotor_slot){case 2:LCD_Print("Select RIGHT Rotor  ");break;case 1:LCD_Print("Select MID   Rotor  ");break;default:LCD_Print("Select LEFT  Rotor  ");break;}
    LCD_SetCursor(LCD_LINE2);LCD_Print("  I  II  III  IV  V ");
    LCD_SetCursor(LCD_LINE3);LCD_Print("  Rotor: ");lcd_pad(rotor_names[idx],11);
    LCD_SetCursor(LCD_LINE4);LCD_Print("UP/DW=chg SEL=next  ");
}
static void display_reflector(void){
    LCD_Clear();LCD_SetCursor(LCD_LINE1);LCD_Print("  Select Reflector  ");
    LCD_SetCursor(LCD_LINE2);LCD_Print("    A     B     C   ");
    LCD_SetCursor(LCD_LINE3);LCD_Print("  Reflector: ");lcd_pad(refl_names[tmp_reflector],7);
    LCD_SetCursor(LCD_LINE4);LCD_Print("UP/DW=chg SEL=apply ");
}
static void display_ring(void){
    LCD_Clear();LCD_SetCursor(LCD_LINE1);
    switch(rotor_slot){case 2:LCD_Print("RIGHT Rotor Start   ");break;case 1:LCD_Print("MIDDLE Rotor Start  ");break;default:LCD_Print("LEFT  Rotor Start   ");break;}
    LCD_SetCursor(LCD_LINE2);LCD_Print("  A B C ... Z       ");
    LCD_SetCursor(LCD_LINE3);LCD_Print("  Position: >");lcd_putc('A'+tmp_ring[rotor_slot]);LCD_Print("<         ");
    LCD_SetCursor(LCD_LINE4);LCD_Print("UP/DW=chg SEL=next  ");
}

/* ============================================================
 * PLUGBOARD
 * ============================================================ */
static void display_plug_menu(EnigmaState *e){
    LCD_Clear();LCD_SetCursor(LCD_LINE1);LCD_Print("  PLUGBOARD SETUP   ");
    LCD_SetCursor(LCD_LINE2);LCD_Print(plug_menu_cursor==0?"> ":"  ");LCD_Print("Stage 1 (In): ");lcd_u8(e->num_plug_pairs_in);LCD_Print("  ");
    LCD_SetCursor(LCD_LINE3);LCD_Print(plug_menu_cursor==1?"> ":"  ");LCD_Print("Stage 2 (Out): ");lcd_u8(e->num_plug_pairs_out);LCD_Print(" ");
    LCD_SetCursor(LCD_LINE4);LCD_Print("  ENTER=back config ");
}
/* display_plug_method — 4 items, 3 visible at a time (scrolls)
 *   0: Preset (Day sheet)
 *   1: Manual entry
 *   2: Physical plug     ← NEW
 *   3: Clear (0 pairs)
 *
 * base = cursor < 2 ? 0 : 1
 *   cursor 0,1 → show items 0,1,2
 *   cursor 2,3 → show items 1,2,3
 */
static void display_plug_method(void){
    const char *items[4]={
        "Preset (Day sheet)",
        "Manual entry      ",
        "Physical plug     ",
        "Clear (0 pairs)   "
    };
    uint8_t base = (plug_method_cursor < 2) ? 0 : 1;
    uint8_t i;
    LCD_Clear();
    LCD_SetCursor(LCD_LINE1);
    LCD_Print(plug_stage==0?"Stage 1 (In) Method ":"Stage 2 (Out) Method");
    for(i=0;i<3;i++){
        uint8_t idx=base+i;
        switch(i){case 0:LCD_SetCursor(LCD_LINE2);break;case 1:LCD_SetCursor(LCD_LINE3);break;default:LCD_SetCursor(LCD_LINE4);break;}
        LCD_Print(idx==plug_method_cursor?"> ":"  ");
        LCD_Print(items[idx]);
    }
}

/* display_plug_physical — live scan display
 *
 *   L1: "Stg1 Phys  PB:Xpr  "
 *   L2: confirmed pairs  "AB CD EF ..."  (3 chars each: letter+letter+space)
 *   L3: waiting jacks    "A? C? ..."     (3 chars each: letter+?+space)
 *        — jacks with a cable inserted but no partner confirmed yet
 *   L4: "SEL=apply ENT=back "
 *
 * A jack is "waiting" when phys_inserted[i] is true but that index
 * does not appear in any entry of phys_pairs[].
 */
static void display_plug_physical(void){
    uint8_t i;
    LCD_Clear();

    /* Line 1: header + confirmed pair count */
    LCD_SetCursor(LCD_LINE1);
    LCD_Print(plug_stage==0?"Stg1 Phys  PB:":"Stg2 Phys  PB:");
    lcd_u8(phys_count);
    LCD_Print("pr      ");

    /* Line 2: confirmed pairs (up to 6, 3 chars each) */
    LCD_SetCursor(LCD_LINE2);
    for(i=0;i<6;i++){
        if(i<phys_count){lcd_putc('A'+phys_pairs[i].a);lcd_putc('A'+phys_pairs[i].b);lcd_putc(' ');}
        else LCD_Print("   ");
    }
    LCD_Print("  ");

    /* Line 3: waiting jacks — inserted but not yet paired */
    LCD_SetCursor(LCD_LINE3);
    {
        uint8_t col=0;
        for(i=0;i<PBOARD_ACTIVE_JACKS && col<18;i++){
            if(!phys_inserted[i]) continue;
            /* check if this jack is already in a confirmed pair */
            bool in_pair=false; uint8_t p;
            for(p=0;p<phys_count&&!in_pair;p++)
                if(phys_pairs[p].a==i||phys_pairs[p].b==i) in_pair=true;
            if(!in_pair){
                lcd_putc('A'+i);
                lcd_putc('?');
                lcd_putc(' ');
                col+=3;
            }
        }
        /* pad remainder */
        while(col<20){lcd_putc(' ');col++;}
    }

    /* Line 4: controls */
    LCD_SetCursor(LCD_LINE4);
    LCD_Print("SEL=apply ENT=back ");
}
static void display_plug_preset(void){
    const char*p=plug_days[plug_day];uint8_t i;
    LCD_Clear();LCD_SetCursor(LCD_LINE1);LCD_Print(plug_stage==0?"Stg1 Day:":"Stg2 Day:");lcd_u8(plug_day+1);LCD_Print("       ");
    LCD_SetCursor(LCD_LINE2);i=0;while(p[i]&&i<20){lcd_putc(p[i]);i++;}while(i<20){lcd_putc(' ');i++;}
    LCD_SetCursor(LCD_LINE3);i=0;while(p[i])i++;if(i>20){uint8_t j=0;p+=20;while(*p&&j<20){lcd_putc(*p++);j++;}while(j<20){lcd_putc(' ');j++;}}else LCD_Print("                    ");
    LCD_SetCursor(LCD_LINE4);LCD_Print("UP/DW=day SEL=apply ");
}
static void display_plug_count(void){
    LCD_Clear();LCD_SetCursor(LCD_LINE1);LCD_Print(plug_stage==0?"Stage 1 Pair Count  ":"Stage 2 Pair Count  ");
    LCD_SetCursor(LCD_LINE2);LCD_Print("  Pairs: ");lcd_u8(plug_count);LCD_Print("           ");
    LCD_SetCursor(LCD_LINE3);LCD_Print("  Range: 0 to 13    ");
    LCD_SetCursor(LCD_LINE4);LCD_Print("UP/DW=chg SEL=start ");
}
static void display_plug_pair_a(void){
    LCD_Clear();LCD_SetCursor(LCD_LINE1);LCD_Print(plug_stage==0?"Stg1":"Stg2");LCD_Print(" Pair ");lcd_u8(plug_pair_idx+1);LCD_Print("/");lcd_u8(plug_count);LCD_Print("          ");
    LCD_SetCursor(LCD_LINE2);LCD_Print("  First letter:     ");
    LCD_SetCursor(LCD_LINE3);LCD_Print("       > ");lcd_putc('A'+plug_letter_a);LCD_Print(" <          ");
    LCD_SetCursor(LCD_LINE4);LCD_Print("UP/DW=chg SEL=next  ");
}
static void display_plug_pair_b(void){
    LCD_Clear();LCD_SetCursor(LCD_LINE1);LCD_Print(plug_stage==0?"Stg1":"Stg2");LCD_Print(" Pair ");lcd_u8(plug_pair_idx+1);LCD_Print("/");lcd_u8(plug_count);LCD_Print("          ");
    LCD_SetCursor(LCD_LINE2);LCD_Print("  ");lcd_putc('A'+plug_letter_a);LCD_Print(" <-> ?              ");
    LCD_SetCursor(LCD_LINE3);LCD_Print("  Second:  > ");lcd_putc('A'+plug_letter_b);LCD_Print(" <        ");
    LCD_SetCursor(LCD_LINE4);LCD_Print("UP/DW=chg SEL=ok    ");
}

/* ============================================================
 * PLUGBOARD HELPERS
 * ============================================================ */
static uint8_t next_free(uint8_t cur,bool inc,uint8_t skip){uint8_t c=cur;int i;for(i=0;i<26;i++){c=inc?(c+1)%26:(c+25)%26;if(c!=skip&&!plug_used[c])return c;}return cur;}
static void reset_tmp_pb(void){int i;if(plug_stage==0){for(i=0;i<26;i++)tmp_pb_in[i]=(uint8_t)i;tmp_pb_count_in=0;}else{for(i=0;i<26;i++)tmp_pb_out[i]=(uint8_t)i;tmp_pb_count_out=0;}memset(plug_used,0,26);}
static void tmp_pb_add(uint8_t a,uint8_t b){if(plug_stage==0){tmp_pb_in[a]=b;tmp_pb_in[b]=a;tmp_pb_count_in++;}else{tmp_pb_out[a]=b;tmp_pb_out[b]=a;tmp_pb_count_out++;}plug_used[a]=true;plug_used[b]=true;}
static void apply_tmp_pb(EnigmaState *e){int i;if(plug_stage==0){for(i=0;i<26;i++)e->plugboard_in[i]=tmp_pb_in[i];e->num_plug_pairs_in=tmp_pb_count_in;}else{for(i=0;i<26;i++)e->plugboard_out[i]=tmp_pb_out[i];e->num_plug_pairs_out=tmp_pb_count_out;}}
static void apply_preset_to_stage(EnigmaState *e,uint8_t stage,uint8_t day){const char*p=plug_days[day];char a,b;enigma_plugboard_clear_stage(e,stage);while(*p){while(*p==' ')p++;if(!*p)break;a=*p++;while(*p==' ')p++;if(!*p)break;b=*p++;enigma_plugboard_add_pair_stage(e,stage,a,b);}}

/* ============================================================
 * APPLY HELPERS
 * ============================================================ */
static void apply_rotors(EnigmaState *e){
    enigma_select_rotors(e,tmp_rotor[0],tmp_rotor[1],tmp_rotor[2]);
    uart_puts("Rotors: L=");uart_puts(enigma_rotor_name(tmp_rotor[0]));
    uart_puts(" M=");uart_puts(enigma_rotor_name(tmp_rotor[1]));
    uart_puts(" R=");uart_puts(enigma_rotor_name(tmp_rotor[2]));uart_puts("\r\n");
}
static void apply_reflector(EnigmaState *e){
    enigma_init(e,tmp_reflector);
    enigma_select_rotors(e,tmp_rotor[0],tmp_rotor[1],tmp_rotor[2]);
    enigma_set_positions(e,e->rotor_positions[0],e->rotor_positions[1],e->rotor_positions[2]);
    uart_puts("Reflector: ");uart_puts(refl_names[tmp_reflector]);uart_puts("\r\n");
}
static void print_pb_applied(EnigmaState *e,uint8_t stage){
    const uint8_t*pb=(stage==0)?e->plugboard_in:e->plugboard_out;
    uint8_t cnt=(stage==0)?e->num_plug_pairs_in:e->num_plug_pairs_out;
    uart_puts(stage==0?"Stage1: ":"Stage2: ");
    uart_print_pb(pb,cnt);uart_putchar('(');uart_putchar('0'+cnt/10);uart_putchar('0'+cnt%10);uart_puts(" pairs)\r\n");
}
static void apply_ring(EnigmaState *e){
    enigma_set_daily_settings(e,tmp_ring[0],tmp_ring[1],tmp_ring[2]);
    enigma_reset_to_daily(e);
    uart_puts("Ring: L=");uart_putchar('A'+tmp_ring[0]);
    uart_puts(" M=");uart_putchar('A'+tmp_ring[1]);
    uart_puts(" R=");uart_putchar('A'+tmp_ring[2]);uart_puts("\r\n");
}

/* ============================================================
 * BUTTON POLLING
 * ============================================================ */
static void poll_buttons(void){
    int i;uint32_t now=g_tickMs;
    for(i=0;i<5;i++){
        bool pressed=(MAP_GPIOPinRead(btn_ports[i],btn_pins[i])&btn_pins[i])==0;
        if(pressed&&!last_state[i]&&(now-last_press[i])>DEBOUNCE_MS){
            last_press[i]=now;
            switch(i){case 0:g_btnModeFlag=true;break;case 1:g_btnSelectFlag=true;break;case 2:g_btnUpFlag=true;break;case 3:g_btnDownFlag=true;break;case 4:g_btnEnterFlag=true;break;}
        }
        last_state[i]=pressed;
    }
}

/* ============================================================
 * INIT
 * ============================================================ */
void control_panel_init(void){
    int i;
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD));
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOQ);while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOQ));
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOP);while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOP));
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPION);while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPION));
    MAP_GPIOPinTypeGPIOInput(BTN_MODE_PORT,  BTN_MODE_PIN);  MAP_GPIOPadConfigSet(BTN_MODE_PORT,  BTN_MODE_PIN,  GPIO_STRENGTH_2MA,GPIO_PIN_TYPE_STD_WPU);
    MAP_GPIOPinTypeGPIOInput(BTN_SELECT_PORT,BTN_SELECT_PIN);MAP_GPIOPadConfigSet(BTN_SELECT_PORT,BTN_SELECT_PIN,GPIO_STRENGTH_2MA,GPIO_PIN_TYPE_STD_WPU);
    MAP_GPIOPinTypeGPIOInput(BTN_UP_PORT,    BTN_UP_PIN);    MAP_GPIOPadConfigSet(BTN_UP_PORT,    BTN_UP_PIN,    GPIO_STRENGTH_2MA,GPIO_PIN_TYPE_STD_WPU);
    MAP_GPIOPinTypeGPIOInput(BTN_DOWN_PORT,  BTN_DOWN_PIN|BTN_ENTER_PIN);MAP_GPIOPadConfigSet(BTN_DOWN_PORT,BTN_DOWN_PIN|BTN_ENTER_PIN,GPIO_STRENGTH_2MA,GPIO_PIN_TYPE_STD_WPU);
    MAP_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);while(!MAP_SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB));
    MAP_GPIOPinTypeGPIOOutput(LED_GREEN_PORT,LED_GREEN_PIN|LED_RED_PIN);
    control_panel_led_green(false);control_panel_led_red(false);
    for(i=0;i<5;i++){last_press[i]=0;last_state[i]=false;}
    tmp_reflector=1;plug_day=0;ui_state=UI_NORMAL;sel_slot=2;config_cursor=0;update_leds();
    hist_count=0;hist_head=0;hist_view=0;
}

/* ============================================================
 * UPDATE
 * ============================================================ */
void control_panel_update(EnigmaState *e){
    extern bool g_typing_started;
    poll_buttons();

    /* MODE: 3-mode cycle */
    if(g_btnModeFlag){
        g_btnModeFlag=false;
        switch(g_currentMode){
            case MODE_PASSTHROUGH:g_currentMode=MODE_ENCRYPT;    break;
            case MODE_ENCRYPT:    g_currentMode=MODE_DECRYPT;    break;
            default:              g_currentMode=MODE_PASSTHROUGH;break;
        }
        update_leds();
        g_typing_started=false;typing_col=0;ui_state=UI_NORMAL;display_normal(e);
        uart_puts(g_currentMode==MODE_ENCRYPT?"Mode: ENCRYPT\r\n":
                  g_currentMode==MODE_DECRYPT?"Mode: DECRYPT\r\n":"Mode: PASSTHROUGH\r\n");
        return;
    }

    switch(ui_state){

    /* ---- Normal / Typing: ENTER always opens config ---- */
    case UI_NORMAL:
    case UI_TYPING:
        if(g_btnSelectFlag){
            g_btnSelectFlag=false;sel_slot=(sel_slot==0)?2:sel_slot-1;
            g_typing_started=false;typing_col=0;ui_state=UI_NORMAL;display_normal(e);
        }
        else if(g_btnUpFlag){
            g_btnUpFlag=false;e->rotor_positions[sel_slot]=(e->rotor_positions[sel_slot]+1)%26;
            g_typing_started=false;typing_col=0;ui_state=UI_NORMAL;display_normal(e);
        }
        else if(g_btnDownFlag){
            g_btnDownFlag=false;e->rotor_positions[sel_slot]=(e->rotor_positions[sel_slot]+25)%26;
            g_typing_started=false;typing_col=0;ui_state=UI_NORMAL;display_normal(e);
        }
        else if(g_btnEnterFlag){
            /* ENTER always → config, regardless of mode */
            g_btnEnterFlag=false;
            g_typing_started=false;typing_col=0;
            tmp_rotor[0]=e->rotor_ids[0];tmp_rotor[1]=e->rotor_ids[1];tmp_rotor[2]=e->rotor_ids[2];
            tmp_ring[0]=e->daily_settings[0];tmp_ring[1]=e->daily_settings[1];tmp_ring[2]=e->daily_settings[2];
            config_from_reset=false;
            /* clamp cursor to valid range for current mode */
            if(config_cursor>=config_n())config_cursor=config_n()-1;
            ui_state=UI_CONFIG_SELECT;display_config();
        }
        break;

    /* ---- Config (5 items in PASS, 6 in ENC/DEC) ---- */
    case UI_CONFIG_SELECT:{
        uint8_t N=config_n();
        if(g_btnUpFlag){   g_btnUpFlag=false;   config_cursor=(config_cursor>0)?config_cursor-1:N-1; display_config(); }
        else if(g_btnDownFlag){g_btnDownFlag=false; config_cursor=(config_cursor<N-1)?config_cursor+1:0; display_config(); }
        else if(g_btnSelectFlag){
            g_btnSelectFlag=false;
            switch(config_cursor){
                case 0:ui_state=UI_ROTOR_SELECT;rotor_slot=2;display_rotor_select();break;
                case 1:ui_state=UI_REFLECTOR_SELECT;display_reflector();break;
                case 2:plug_menu_cursor=0;ui_state=UI_PLUG_MENU;display_plug_menu(e);break;
                case 3:ui_state=UI_RING_SET;rotor_slot=2;
                       tmp_ring[0]=e->daily_settings[0];tmp_ring[1]=e->daily_settings[1];tmp_ring[2]=e->daily_settings[2];
                       display_ring();break;
                case 4:hist_view=0;ui_state=UI_HISTORY;display_history();break;
                case 5: /* Reset/Restart — only reachable in ENC/DEC (N=6) */
                    reset_cursor=0;ui_state=UI_RESET_MENU;display_reset_menu();break;
            }
        }
        else if(g_btnEnterFlag){
            g_btnEnterFlag=false;
            if(config_from_reset){
                config_from_reset=false;
                mode_sel_cursor=(g_currentMode==MODE_ENCRYPT)?0:1;
                ui_state=UI_RESET_MODE_SEL;display_mode_sel();
            }else{
                ui_state=UI_NORMAL;display_normal(e);
            }
        }
        break;}

    /* ---- Reset menu ---- */
    case UI_RESET_MENU:
        if(g_btnUpFlag){   g_btnUpFlag=false;   reset_cursor=(reset_cursor>0)?reset_cursor-1:2; display_reset_menu(); }
        else if(g_btnDownFlag){g_btnDownFlag=false; reset_cursor=(reset_cursor<2)?reset_cursor+1:0; display_reset_menu(); }
        else if(g_btnSelectFlag){
            g_btnSelectFlag=false;
            switch(reset_cursor){
            case 0: /* Keep settings → pick mode */
                uart_puts("Keep settings: pick mode\r\n");
                mode_sel_cursor=(g_currentMode==MODE_ENCRYPT)?0:1;
                ui_state=UI_RESET_MODE_SEL;display_mode_sel();
                break;
            case 1: /* Change settings → config */
                uart_puts("Change settings\r\n");
                config_from_reset=true;config_cursor=0;
                ui_state=UI_CONFIG_SELECT;display_config();
                break;
            case 2: /* Restart to factory defaults */
                uart_puts("Factory defaults\r\n");
                enigma_init(e,1);
                enigma_set_daily_settings(e,0,0,0);
                enigma_set_positions(e,0,0,0);
                tmp_reflector=1;
                tmp_ring[0]=0;tmp_ring[1]=0;tmp_ring[2]=0;
                tmp_rotor[0]=ROTOR_I;tmp_rotor[1]=ROTOR_II;tmp_rotor[2]=ROTOR_III;
                g_currentMode=MODE_PASSTHROUGH;update_leds();
                g_typing_started=false;typing_col=0;ui_state=UI_NORMAL;display_normal(e);
                uart_puts("I-II-III Ref:B Pos:AAA Ring:AAA PB:none\r\n");
                break;
            }
        }
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_CONFIG_SELECT;display_config();}
        break;

    /* ---- Mode selection after Keep/Change ---- */
    case UI_RESET_MODE_SEL:
        if(g_btnUpFlag||g_btnDownFlag){g_btnUpFlag=g_btnDownFlag=false;mode_sel_cursor^=1;display_mode_sel();}
        else if(g_btnSelectFlag){
            g_btnSelectFlag=false;
            g_currentMode=(mode_sel_cursor==0)?MODE_ENCRYPT:MODE_DECRYPT;
            update_leds();
            /* KEY FIX: restore rotor positions to Grundstellung (daily_settings)
             * before starting the new typing session.
             * Example: Ring set to F,C,B → daily_settings={F,C,B}.
             * After typing a message, rotors advanced to A,B,F.
             * Reset → Keep → confirm → enigma_reset_to_daily restores to F,C,B. */
            enigma_reset_to_daily(e);
            uart_puts(g_currentMode==MODE_ENCRYPT?"Confirmed: ENCRYPT, pos restored to Grundstellung\r\n":
                                                   "Confirmed: DECRYPT, pos restored to Grundstellung\r\n");
            uart_puts("Grundstellung: L=");uart_putchar('A'+e->daily_settings[0]);
            uart_puts(" M=");uart_putchar('A'+e->daily_settings[1]);
            uart_puts(" R=");uart_putchar('A'+e->daily_settings[2]);uart_puts("\r\n");
            typing_display_init(e);  /* clears streams, g_typing_started=true */
        }
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_RESET_MENU;display_reset_menu();}
        break;

    /* ---- History ---- */
    case UI_HISTORY:
        if(g_btnUpFlag){g_btnUpFlag=false;if(hist_count>0)hist_view=(hist_view>0)?hist_view-1:hist_count-1;display_history();}
        else if(g_btnDownFlag){g_btnDownFlag=false;if(hist_count>0)hist_view=(hist_view<hist_count-1)?hist_view+1:0;display_history();}
        else if(g_btnEnterFlag||g_btnSelectFlag){g_btnEnterFlag=false;g_btnSelectFlag=false;ui_state=UI_CONFIG_SELECT;display_config();}
        break;

    /* ---- Rotor / Reflector / Ring ---- */
    case UI_ROTOR_SELECT:{
        int i,idx=0;for(i=0;i<5;i++)if(tmp_rotor[rotor_slot]==rotor_id_list[i])idx=i;
        if(g_btnUpFlag){g_btnUpFlag=false;idx=(idx+1)%5;tmp_rotor[rotor_slot]=rotor_id_list[idx];display_rotor_select();}
        else if(g_btnDownFlag){g_btnDownFlag=false;idx=(idx+4)%5;tmp_rotor[rotor_slot]=rotor_id_list[idx];display_rotor_select();}
        else if(g_btnSelectFlag){g_btnSelectFlag=false;if(rotor_slot>0){rotor_slot--;display_rotor_select();}else{apply_rotors(e);ui_state=UI_CONFIG_SELECT;display_config();}}
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_CONFIG_SELECT;display_config();}
        break;}
    case UI_REFLECTOR_SELECT:
        if(g_btnUpFlag){g_btnUpFlag=false;tmp_reflector=(tmp_reflector+1)%3;display_reflector();}
        else if(g_btnDownFlag){g_btnDownFlag=false;tmp_reflector=(tmp_reflector+2)%3;display_reflector();}
        else if(g_btnSelectFlag){g_btnSelectFlag=false;apply_reflector(e);ui_state=UI_CONFIG_SELECT;display_config();}
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_CONFIG_SELECT;display_config();}
        break;
    case UI_RING_SET:
        if(g_btnUpFlag){g_btnUpFlag=false;tmp_ring[rotor_slot]=(tmp_ring[rotor_slot]+1)%26;display_ring();}
        else if(g_btnDownFlag){g_btnDownFlag=false;tmp_ring[rotor_slot]=(tmp_ring[rotor_slot]+25)%26;display_ring();}
        else if(g_btnSelectFlag){g_btnSelectFlag=false;if(rotor_slot>0){rotor_slot--;display_ring();}else{apply_ring(e);ui_state=UI_CONFIG_SELECT;display_config();}}
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_CONFIG_SELECT;display_config();}
        break;

    /* ---- Plugboard ---- */
    case UI_PLUG_MENU:
        if(g_btnUpFlag||g_btnDownFlag){g_btnUpFlag=g_btnDownFlag=false;plug_menu_cursor^=1;display_plug_menu(e);}
        else if(g_btnSelectFlag){g_btnSelectFlag=false;plug_stage=plug_menu_cursor;plug_method_cursor=0;ui_state=UI_PLUG_METHOD;display_plug_method();}
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_CONFIG_SELECT;display_config();}
        break;
    case UI_PLUG_METHOD:
        if(g_btnUpFlag){g_btnUpFlag=false;plug_method_cursor=(plug_method_cursor>0)?plug_method_cursor-1:3;display_plug_method();}
        else if(g_btnDownFlag){g_btnDownFlag=false;plug_method_cursor=(plug_method_cursor<3)?plug_method_cursor+1:0;display_plug_method();}
        else if(g_btnSelectFlag){
            g_btnSelectFlag=false;
            switch(plug_method_cursor){
                case 0:ui_state=UI_PLUG_PRESET;display_plug_preset();break;
                case 1:plug_count=0;ui_state=UI_PLUG_COUNT;display_plug_count();break;
                case 2: /* Physical plug */
                    plugboard_hw_init();
                    phys_count=0; phys_last_count=255;
                    { uint8_t _z; for(_z=0;_z<PBOARD_NUM_JACKS;_z++){phys_inserted[_z]=false;phys_last_inserted[_z]=false;} }
                    plugboard_hw_scan(phys_pairs,&phys_count);
                    phys_last_count=phys_count;
                    /* UART: show which jacks are being scanned + initial state */
                    uart_puts("\r\n[PHYSICAL PLUG] Scanning jacks A");
                    { int _i; for(_i=1;_i<PBOARD_ACTIVE_JACKS;_i++){uart_putchar('-');uart_putchar('A'+_i);} }
                    uart_puts("\r\nInsert: cable opens NC contact -> pin HIGH -> detected\r\n");
                    uart_puts("Remove: NC contact closes -> pin LOW -> ignored\r\n");
                    uart_puts("Initial pairs: ");
                    uart_putchar('0'+phys_count);
                    uart_puts("\r\n");
                    if(phys_count==0) uart_puts("(none -- insert cables to pair jacks)\r\n");
                    else { uint8_t _k; for(_k=0;_k<phys_count;_k++){uart_putchar('A'+phys_pairs[_k].a);uart_puts(" <--> ");uart_putchar('A'+phys_pairs[_k].b);uart_puts("\r\n");} }
                    ui_state=UI_PLUG_PHYSICAL;
                    display_plug_physical();
                    break;
                case 3:enigma_plugboard_clear_stage(e,plug_stage);uart_puts("Stage cleared\r\n");ui_state=UI_PLUG_MENU;display_plug_menu(e);break;
            }
        }
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_PLUG_MENU;display_plug_menu(e);}
        break;
    /* ── Physical plug ─────────────────────────────────────────────────────
     * Rescans every loop tick (~20 ms).
     * Tracks two things: confirmed pairs AND which jacks have a cable in.
     * LCD redraws whenever either changes.
     * UART reports every individual insert/remove/pair/unplug event.
     * SELECT → apply pairs to enigma stage, exit.
     * ENTER  → cancel, back to method menu.
     * ─────────────────────────────────────────────────────────────────────*/
    case UI_PLUG_PHYSICAL:{
        uint8_t new_count=0;
        PBoardPair new_pairs[PBOARD_MAX_PAIRS];
        bool new_inserted[PBOARD_NUM_JACKS];
        bool pairs_changed, inserted_changed;
        uint8_t k, i;

        /* Read inserted state first (simple pin read, all input+WPU) */
        plugboard_hw_read_inserted(new_inserted);

        /* Full pair scan */
        plugboard_hw_scan(new_pairs, &new_count);

        /* ── Detect inserted state changes ── */
        inserted_changed = false;
        for(i=0;i<PBOARD_ACTIVE_JACKS&&!inserted_changed;i++)
            if(new_inserted[i]!=phys_last_inserted[i]) inserted_changed=true;

        /* ── Detect pair changes ── */
        pairs_changed = (new_count != phys_count);
        if(!pairs_changed)
            for(k=0;k<new_count&&!pairs_changed;k++)
                if(new_pairs[k].a!=phys_pairs[k].a||new_pairs[k].b!=phys_pairs[k].b)
                    pairs_changed=true;

        if(inserted_changed || pairs_changed){

            /* ── UART: single jack inserted ── */
            for(i=0;i<PBOARD_ACTIVE_JACKS;i++){
                if(new_inserted[i] && !phys_last_inserted[i]){
                    uart_puts("[INSERTED]  ");
                    uart_putchar('A'+i);
                    uart_puts(" (waiting for partner)\r\n");
                }
            }

            /* ── UART: single jack removed ── */
            for(i=0;i<PBOARD_ACTIVE_JACKS;i++){
                if(!new_inserted[i] && phys_last_inserted[i]){
                    uart_puts("[REMOVED]   ");
                    uart_putchar('A'+i);
                    uart_puts("\r\n");
                }
            }

            /* ── UART: new confirmed pairs ── */
            for(k=0;k<new_count;k++){
                bool found=false; uint8_t m;
                for(m=0;m<phys_count&&!found;m++)
                    if(phys_pairs[m].a==new_pairs[k].a&&phys_pairs[m].b==new_pairs[k].b)
                        found=true;
                if(!found){
                    uart_puts("[PAIRED]    ");
                    uart_putchar('A'+new_pairs[k].a);
                    uart_puts(" <--> ");
                    uart_putchar('A'+new_pairs[k].b);
                    uart_puts("\r\n");
                }
            }

            /* ── UART: broken pairs ── */
            for(k=0;k<phys_count;k++){
                bool found=false; uint8_t m;
                for(m=0;m<new_count&&!found;m++)
                    if(new_pairs[m].a==phys_pairs[k].a&&new_pairs[m].b==phys_pairs[k].b)
                        found=true;
                if(!found){
                    uart_puts("[UNPLUGGED] ");
                    uart_putchar('A'+phys_pairs[k].a);
                    uart_puts(" -x- ");
                    uart_putchar('A'+phys_pairs[k].b);
                    uart_puts("\r\n");
                }
            }

            /* ── UART: summary ── */
            { uint8_t ins_cnt=0;
              for(i=0;i<PBOARD_ACTIVE_JACKS;i++) if(new_inserted[i]) ins_cnt++;
              uart_puts("Inserted:");uart_putchar('0'+ins_cnt);
              uart_puts(" Pairs:");uart_putchar('0'+new_count);
              uart_puts("\r\n");
            }

            /* ── Update stored state ── */
            for(k=0;k<new_count;k++) phys_pairs[k]=new_pairs[k];
            phys_count=new_count;
            phys_last_count=new_count;
            for(i=0;i<PBOARD_ACTIVE_JACKS;i++){
                phys_inserted[i]=new_inserted[i];
                phys_last_inserted[i]=new_inserted[i];
            }
            display_plug_physical();
        }

        if(g_btnSelectFlag){
            g_btnSelectFlag=false;
            enigma_plugboard_clear_stage(e, plug_stage);
            for(k=0;k<phys_count&&k<PBOARD_MAX_PAIRS;k++)
                enigma_plugboard_add_pair_stage(e, plug_stage,
                    (char)('A'+phys_pairs[k].a),(char)('A'+phys_pairs[k].b));
            print_pb_applied(e, plug_stage);
            uart_puts("Physical PB applied\r\n");
            ui_state=UI_PLUG_MENU;
            display_plug_menu(e);
        }
        else if(g_btnEnterFlag){
            g_btnEnterFlag=false;
            uart_puts("Physical PB cancelled\r\n");
            ui_state=UI_PLUG_METHOD;
            display_plug_method();
        }
        break;
    }

    case UI_PLUG_PRESET:
        if(g_btnUpFlag){g_btnUpFlag=false;plug_day=(plug_day+1)%31;display_plug_preset();}
        else if(g_btnDownFlag){g_btnDownFlag=false;plug_day=(plug_day+30)%31;display_plug_preset();}
        else if(g_btnSelectFlag){g_btnSelectFlag=false;apply_preset_to_stage(e,plug_stage,plug_day);print_pb_applied(e,plug_stage);ui_state=UI_PLUG_MENU;display_plug_menu(e);}
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_PLUG_METHOD;display_plug_method();}
        break;
    case UI_PLUG_COUNT:
        if(g_btnUpFlag){g_btnUpFlag=false;if(plug_count<13)plug_count++;display_plug_count();}
        else if(g_btnDownFlag){g_btnDownFlag=false;if(plug_count>0)plug_count--;display_plug_count();}
        else if(g_btnSelectFlag){
            g_btnSelectFlag=false;
            if(plug_count==0){enigma_plugboard_clear_stage(e,plug_stage);uart_puts("Cleared\r\n");ui_state=UI_PLUG_MENU;display_plug_menu(e);}
            else{reset_tmp_pb();plug_pair_idx=0;plug_letter_a=0;ui_state=UI_PLUG_PAIR_A;display_plug_pair_a();}
        }
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_PLUG_METHOD;display_plug_method();}
        break;
    case UI_PLUG_PAIR_A:
        if(g_btnUpFlag){g_btnUpFlag=false;plug_letter_a=next_free(plug_letter_a,true,255);display_plug_pair_a();}
        else if(g_btnDownFlag){g_btnDownFlag=false;plug_letter_a=next_free(plug_letter_a,false,255);display_plug_pair_a();}
        else if(g_btnSelectFlag){g_btnSelectFlag=false;plug_letter_b=next_free(plug_letter_a,true,255);ui_state=UI_PLUG_PAIR_B;display_plug_pair_b();}
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_PLUG_COUNT;display_plug_count();}
        break;
    case UI_PLUG_PAIR_B:
        if(g_btnUpFlag){g_btnUpFlag=false;plug_letter_b=next_free(plug_letter_b,true,plug_letter_a);display_plug_pair_b();}
        else if(g_btnDownFlag){g_btnDownFlag=false;plug_letter_b=next_free(plug_letter_b,false,plug_letter_a);display_plug_pair_b();}
        else if(g_btnSelectFlag){
            g_btnSelectFlag=false;tmp_pb_add(plug_letter_a,plug_letter_b);plug_pair_idx++;
            if(plug_pair_idx>=plug_count){apply_tmp_pb(e);print_pb_applied(e,plug_stage);ui_state=UI_PLUG_MENU;display_plug_menu(e);}
            else{uint8_t k;for(k=0;k<26;k++){if(!plug_used[k]){plug_letter_a=k;break;}}ui_state=UI_PLUG_PAIR_A;display_plug_pair_a();}
        }
        else if(g_btnEnterFlag){g_btnEnterFlag=false;ui_state=UI_PLUG_PAIR_A;display_plug_pair_a();}
        break;

    default:ui_state=UI_NORMAL;break;
    }
}

void control_panel_update_display(EnigmaState *e)
{if(ui_state==UI_NORMAL||ui_state==UI_TYPING)display_normal(e);}
