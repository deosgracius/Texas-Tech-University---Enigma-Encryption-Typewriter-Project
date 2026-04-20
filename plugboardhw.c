/******************************************************************************
 * plugboardhw.c  —  Physical plugboard driver
 *
 * JACK TYPE: normally-closed (NC) contact to GND.
 *   Empty jack  → NC shorts tip to GND → pin reads LOW
 *   Cable in jack → insertion opens NC contact → WPU pulls pin HIGH
 *
 * SCAN ALGORITHM: pre-filter, then drive-LOW.
 *
 *   Step 1 — read all active pins at idle (input + WPU).
 *             HIGH = cable inserted (NC open).
 *             LOW  = empty jack    (NC grounded) — skip entirely.
 *
 *   Step 2 — among HIGH pairs only, drive one LOW.
 *             If the other goes LOW → real cable confirmed.
 *             No bidirectional check needed: since both started HIGH
 *             (NC open), the only way one goes LOW when the other is
 *             driven LOW is through an actual cable connection.
 *             Phantom pairs from NC-grounded pins are impossible because
 *             those pins are excluded in step 1.
 *
 * PBOARD_ACTIVE_JACKS: change to 26 when all jacks are wired.
 ******************************************************************************/

#include "plugboardhw.h"

typedef struct { uint32_t periph; uint32_t base; uint8_t pin; } JackPin;

static const JackPin jack[PBOARD_NUM_JACKS] = {
    /* A  0 */ { SYSCTL_PERIPH_GPIOD, GPIO_PORTD_BASE, GPIO_PIN_0 },
    /* B  1 */ { SYSCTL_PERIPH_GPIOM, GPIO_PORTM_BASE, GPIO_PIN_3 },
    /* C  2 */ { SYSCTL_PERIPH_GPIOH, GPIO_PORTH_BASE, GPIO_PIN_2 },
    /* D  3 */ { SYSCTL_PERIPH_GPIOH, GPIO_PORTH_BASE, GPIO_PIN_3 },
    /* E  4 */ { SYSCTL_PERIPH_GPIOD, GPIO_PORTD_BASE, GPIO_PIN_1 },
    /* F  5 */ { SYSCTL_PERIPH_GPION, GPIO_PORTN_BASE, GPIO_PIN_2 },
    /* G  6 */ { SYSCTL_PERIPH_GPION, GPIO_PORTN_BASE, GPIO_PIN_3 },
    /* H  7 */ { SYSCTL_PERIPH_GPIOP, GPIO_PORTP_BASE, GPIO_PIN_2 },
    /* I  8 */ { SYSCTL_PERIPH_GPIOL, GPIO_PORTL_BASE, GPIO_PIN_3 },
    /* J  9 */ { SYSCTL_PERIPH_GPIOL, GPIO_PORTL_BASE, GPIO_PIN_2 },
    /* K 10 */ { SYSCTL_PERIPH_GPIOL, GPIO_PORTL_BASE, GPIO_PIN_1 },
    /* L 11 */ { SYSCTL_PERIPH_GPIOL, GPIO_PORTL_BASE, GPIO_PIN_0 },
    /* M 12 */ { SYSCTL_PERIPH_GPIOL, GPIO_PORTL_BASE, GPIO_PIN_5 },
    /* N 13 */ { SYSCTL_PERIPH_GPIOL, GPIO_PORTL_BASE, GPIO_PIN_4 },
    /* O 14 */ { SYSCTL_PERIPH_GPIOG, GPIO_PORTG_BASE, GPIO_PIN_0 },
    /* P 15 */ { SYSCTL_PERIPH_GPIOF, GPIO_PORTF_BASE, GPIO_PIN_3 },
    /* Q 16 */ { SYSCTL_PERIPH_GPIOF, GPIO_PORTF_BASE, GPIO_PIN_2 },
    /* R 17 */ { SYSCTL_PERIPH_GPIOF, GPIO_PORTF_BASE, GPIO_PIN_1 },
    /* S 18 */ { SYSCTL_PERIPH_GPIOM, GPIO_PORTM_BASE, GPIO_PIN_7 },
    /* T 19 */ { SYSCTL_PERIPH_GPIOP, GPIO_PORTP_BASE, GPIO_PIN_5 },
    /* U 20 */ { SYSCTL_PERIPH_GPIOA, GPIO_PORTA_BASE, GPIO_PIN_7 },
    /* V 21 */ { SYSCTL_PERIPH_GPIOQ, GPIO_PORTQ_BASE, GPIO_PIN_2 },
    /* W 22 */ { SYSCTL_PERIPH_GPIOQ, GPIO_PORTQ_BASE, GPIO_PIN_3 },
    /* X 23 */ { SYSCTL_PERIPH_GPIOQ, GPIO_PORTQ_BASE, GPIO_PIN_1 },
    /* Y 24 */ { SYSCTL_PERIPH_GPIOM, GPIO_PORTM_BASE, GPIO_PIN_6 },
    /* Z 25 */ { SYSCTL_PERIPH_GPIOG, GPIO_PORTG_BASE, GPIO_PIN_1 },
};

static void jack_input(int idx)
{
    MAP_GPIODirModeSet(jack[idx].base, jack[idx].pin, GPIO_DIR_MODE_IN);
    MAP_GPIOPadConfigSet(jack[idx].base, jack[idx].pin,
                         GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
}

static void jack_drive_low(int idx)
{
    MAP_GPIOPinTypeGPIOOutput(jack[idx].base, jack[idx].pin);
    MAP_GPIOPinWrite(jack[idx].base, jack[idx].pin, 0x00);
}

/* true = pin HIGH (cable inserted, NC open) */
static bool jack_is_high(int idx)
{
    return (MAP_GPIOPinRead(jack[idx].base, jack[idx].pin) & jack[idx].pin) != 0;
}

/* true = pin LOW (either NC-grounded empty jack, or pulled via cable) */
static bool jack_is_low(int idx)
{
    return !jack_is_high(idx);
}

/* 1 ms settle at 120 MHz */
static void settle(void)
{
    MAP_SysCtlDelay(120000000UL / 3UL / 1000UL);
}

/* ════════════════════════════════════════════════════════════════════════════
 * plugboard_hw_init
 * Only enables and configures the PBOARD_ACTIVE_JACKS that are physically
 * wired.  SDK-configured pins beyond that range are not touched.
 * ════════════════════════════════════════════════════════════════════════════ */
void plugboard_hw_init(void)
{
    int i;
    for(i = 0; i < PBOARD_ACTIVE_JACKS; i++) {
        if(!MAP_SysCtlPeripheralReady(jack[i].periph)) {
            MAP_SysCtlPeripheralEnable(jack[i].periph);
            while(!MAP_SysCtlPeripheralReady(jack[i].periph));
        }
        jack_input(i);
    }
    settle();   /* let pins stabilise after init */
}

/* ════════════════════════════════════════════════════════════════════════════
 * plugboard_hw_scan
 *
 * Step 1: read idle state.  active[i] = true means cable present (pin HIGH).
 * Step 2: drive-LOW only among active pairs.
 * ════════════════════════════════════════════════════════════════════════════ */
bool plugboard_hw_scan(PBoardPair *pairs_out, uint8_t *count_out)
{
    int i, j;
    uint8_t cnt = 0;
    int N = PBOARD_ACTIVE_JACKS;
    bool active[26];   /* PBOARD_NUM_JACKS max, only first N used */

    /* ── Step 1: identify jacks with cables inserted ── */
    for(i = 0; i < N; i++)
        active[i] = jack_is_high(i);

    /* ── Step 2: scan only among active (HIGH) pairs ── */
    for(i = 0; i < N - 1 && cnt < PBOARD_MAX_PAIRS; i++)
    {
        if(!active[i]) continue;        /* empty jack — skip */

        jack_drive_low(i);
        settle();

        for(j = i + 1; j < N && cnt < PBOARD_MAX_PAIRS; j++)
        {
            if(!active[j]) continue;    /* empty jack — skip */

            /* Both i and j had NC open (HIGH) before we started.
             * If j reads LOW now, it was pulled down through a cable to i.
             * No false positives possible from NC-grounded empty pins
             * because they were excluded in step 1.                    */
            if(jack_is_low(j))
            {
                pairs_out[cnt].a = (uint8_t)i;
                pairs_out[cnt].b = (uint8_t)j;
                cnt++;
            }
        }

        jack_input(i);
        settle();
    }

    *count_out = cnt;
    return (cnt > 0);
}

/* ════════════════════════════════════════════════════════════════════════════
 * plugboard_hw_read_inserted
 * Just reads the idle state of all active pins.
 * NC-contact jack with no cable → pin LOW → inserted_out[i] = false.
 * NC-contact jack with cable inserted → NC opened → WPU HIGH → true.
 * ════════════════════════════════════════════════════════════════════════════ */
void plugboard_hw_read_inserted(bool *inserted_out)
{
    int i;
    for(i = 0; i < PBOARD_ACTIVE_JACKS; i++)
        inserted_out[i] = jack_is_high(i);
}
