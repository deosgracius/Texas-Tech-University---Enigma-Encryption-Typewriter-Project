#ifndef CONTROL_PANEL_H_
#define CONTROL_PANEL_H_

#include "config.h"
#include "enigma.h"

extern volatile bool g_btnModeFlag;
extern volatile bool g_btnSelectFlag;
extern volatile bool g_btnUpFlag;
extern volatile bool g_btnDownFlag;
extern volatile bool g_btnEnterFlag;
extern bool          g_typing_started;   /* defined in main.c */

void control_panel_init(void);
void control_panel_update(EnigmaState *enigma);
void control_panel_update_display(EnigmaState *enigma);
void control_panel_led_green(bool on);
void control_panel_led_red(bool on);

void control_panel_start_typing(EnigmaState *e);
void control_panel_typing_update(EnigmaState *e, char in_char, char out_char);
bool control_panel_in_typing_state(void);
bool control_panel_typing_active(void);  /* true only during active typing display */

void control_panel_history_add(char mode, const char *input,
                               const char *output, uint8_t len);

#endif
