/******************************************************************************
 * plugboardhw.h  —  Physical plugboard driver
 *
 * PBOARD_ACTIVE_JACKS controls how many jacks are scanned.
 * Set to 3 while only A, B, C are wired.
 * Change to 26 when all jacks are connected to the board.
 ******************************************************************************/

#ifndef PLUGBOARDHW_H_
#define PLUGBOARDHW_H_

#include <stdint.h>
#include <stdbool.h>
#include "ti/devices/msp432e4/driverlib/driverlib.h"

#define PBOARD_NUM_JACKS    26   /* total jacks (A-Z) */
#define PBOARD_ACTIVE_JACKS  3   /* ← only A, B, C wired right now.
                                       change to 26 when all jacks are ready */
#define PBOARD_MAX_PAIRS    13

typedef struct { uint8_t a; uint8_t b; } PBoardPair;

/* Enable peripherals + configure active pins as input+WPU */
void plugboard_hw_init(void);

/* Scan for confirmed pairs (bidirectional cable check).
 * Returns true + fills pairs_out[] if cables are detected. */
bool plugboard_hw_scan(PBoardPair *pairs_out, uint8_t *count_out);

/* Simple read: which active jacks have a cable inserted (pin HIGH)?
 * Fills inserted_out[0..PBOARD_ACTIVE_JACKS-1].  true = cable present.
 * Called while all pins are in input+WPU mode (not mid-scan).        */
void plugboard_hw_read_inserted(bool *inserted_out);

#endif /* PLUGBOARDHW_H_ */
