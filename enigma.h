/******************************************************************************
 * enigma.h  —  Enigma engine with dual-stage independent plugboards
 *
 * Signal path:
 *   input → plugboard_in → R3fwd → R2fwd → R1fwd → Reflector
 *         → R1rev → R2rev → R3rev → plugboard_out → output
 *
 *   7 changes: both plugboards identity (0 pairs each)
 *   8 changes: one plugboard active
 *   9 changes: both plugboards active
 ******************************************************************************/

#ifndef ENIGMA_H_
#define ENIGMA_H_

#include "config.h"

#define NUM_AVAILABLE_ROTORS    5
#define NUM_REFLECTORS          3

#define ROTOR_I      0
#define ROTOR_II     1
#define ROTOR_III    2
#define ROTOR_IV     3
#define ROTOR_V      4

/* ============================================================================
 * TRACE — records every substitution step for UART display
 * ========================================================================== */
typedef struct {
    char    input;
    char    after_pb_in;        /* after stage-1 plugboard  */
    char    after_r_right_fwd;
    char    after_r_mid_fwd;
    char    after_r_left_fwd;
    char    after_reflector;
    char    after_r_left_rev;
    char    after_r_mid_rev;
    char    after_r_right_rev;
    char    after_pb_out;       /* after stage-2 plugboard  */
    uint8_t positions[NUM_ROTORS];
    uint8_t rotor_ids[NUM_ROTORS];
    bool    pb_in_active;       /* plugboard_in  has ≥1 pair */
    bool    pb_out_active;      /* plugboard_out has ≥1 pair */
    uint8_t total_changes;      /* 7, 8, or 9               */
} EnigmaTrace;

/* ============================================================================
 * STATE
 * ========================================================================== */
typedef struct {
    uint8_t  rotor_wiring[NUM_ROTORS][ALPHABET_SIZE];
    uint8_t  rotor_inverse[NUM_ROTORS][ALPHABET_SIZE];
    uint8_t  rotor_positions[NUM_ROTORS];
    uint8_t  daily_settings[NUM_ROTORS];
    uint8_t  ring_settings[NUM_ROTORS];
    uint8_t  rotor_notches[NUM_ROTORS];
    uint8_t  rotor_has_dual_notch[NUM_ROTORS];
    uint8_t  rotor_notch2[NUM_ROTORS];
    uint8_t  rotor_ids[NUM_ROTORS];
    uint8_t  reflector[ALPHABET_SIZE];

    /* Dual-stage plugboards (independent) */
    uint8_t  plugboard_in[ALPHABET_SIZE];    /* Stage 1: before rotors  */
    uint8_t  plugboard_out[ALPHABET_SIZE];   /* Stage 2: after  rotors  */
    uint8_t  num_plug_pairs_in;
    uint8_t  num_plug_pairs_out;

    uint32_t character_count;
} EnigmaState;

/* ============================================================================
 * CORE API
 * ========================================================================== */
void    enigma_init(EnigmaState *state, uint8_t reflector_id);
bool    enigma_select_rotors(EnigmaState *state,
                             uint8_t left_id, uint8_t mid_id, uint8_t right_id);
void    enigma_set_positions(EnigmaState *state,
                             uint8_t r0, uint8_t r1, uint8_t r2);
void    enigma_set_daily_settings(EnigmaState *state,
                                  uint8_t r0, uint8_t r1, uint8_t r2);
void    enigma_set_ring_settings(EnigmaState *state,
                                 uint8_t r0, uint8_t r1, uint8_t r2);
void    enigma_reset_to_daily(EnigmaState *state);
void    enigma_step_rotors(EnigmaState *state);

char    enigma_encrypt_char(EnigmaState *state, char input);
char    enigma_encrypt_char_traced(EnigmaState *state, char input,
                                   EnigmaTrace *trace);

/* ============================================================================
 * PLUGBOARD API  (stage: 0 = in/stage-1,  1 = out/stage-2)
 * ========================================================================== */
void    enigma_plugboard_clear_stage(EnigmaState *state, uint8_t stage);
bool    enigma_plugboard_add_pair_stage(EnigmaState *state, uint8_t stage,
                                        char a, char b);
uint8_t enigma_plugboard_get_pairs(const EnigmaState *state, uint8_t stage);

/* Legacy wrappers (apply to both stages) */
void    enigma_plugboard_clear(EnigmaState *state);

const char *enigma_rotor_name(uint8_t id);

#endif /* ENIGMA_H_ */
