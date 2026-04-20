/******************************************************************************
 * enigma.c  —  Enigma engine with dual-stage independent plugboards
 ******************************************************************************/

#include "enigma.h"
#include <string.h>

/* ============================================================================
 * ROTOR / REFLECTOR DATA
 * ========================================================================== */
static const char *ALL_WIRINGS[NUM_AVAILABLE_ROTORS] = {
    "EKMFLGDQVZNTOWYHXUSPAIBRCJ",  /* I   */
    "AJDKSIRUXBLHWTMCQGZNPYFVOE",  /* II  */
    "BDFHJLCPRTXVZNYEIWGAKMUSQO",  /* III */
    "ESOVPZJAYQUIRHXLNFTGKDCMWB",  /* IV  */
    "VZBRGITYUPSDNHLXAWMJQOFECK"   /* V   */
};
static const uint8_t NOTCH1[NUM_AVAILABLE_ROTORS] = {16, 4, 21, 9, 25};
static const char *ROTOR_NAMES[NUM_AVAILABLE_ROTORS] = {"I","II","III","IV","V"};

static const char REFL_A[] = "EJMZALYXVBWFCRQUONTSPIKHGD";
static const char REFL_B[] = "YRUHQSLDPXNGOKMIEBFZCWVJAT";
static const char REFL_C[] = "FVPJIAOYEDRZXWGCTKUQSBNMHL";

/* ============================================================================
 * HELPERS
 * ========================================================================== */
static void build_inverse(const uint8_t *fwd, uint8_t *inv)
{
    int i;
    for (i = 0; i < ALPHABET_SIZE; i++) inv[fwd[i]] = (uint8_t)i;
}

static void load_rotor(EnigmaState *s, int slot, uint8_t id)
{
    int i;
    const char *w;
    if (id >= NUM_AVAILABLE_ROTORS) id = 0;
    w = ALL_WIRINGS[id];
    for (i = 0; i < ALPHABET_SIZE; i++)
        s->rotor_wiring[slot][i] = (uint8_t)(w[i] - 'A');
    build_inverse(s->rotor_wiring[slot], s->rotor_inverse[slot]);
    s->rotor_notches[slot]       = NOTCH1[id];
    s->rotor_has_dual_notch[slot]= 0;
    s->rotor_notch2[slot]        = 0;
    s->rotor_ids[slot]           = id;
}

static void pb_identity(uint8_t *pb)
{
    int i;
    for (i = 0; i < ALPHABET_SIZE; i++) pb[i] = (uint8_t)i;
}

/* ============================================================================
 * INIT
 * ========================================================================== */
void enigma_init(EnigmaState *s, uint8_t reflector_id)
{
    int i;
    const char *refl;
    memset(s, 0, sizeof(EnigmaState));
    load_rotor(s, 0, ROTOR_I);
    load_rotor(s, 1, ROTOR_II);
    load_rotor(s, 2, ROTOR_III);
    switch (reflector_id) {
        case 0:  refl = REFL_A; break;
        case 2:  refl = REFL_C; break;
        default: refl = REFL_B; break;
    }
    for (i = 0; i < ALPHABET_SIZE; i++)
        s->reflector[i] = (uint8_t)(refl[i] - 'A');
    pb_identity(s->plugboard_in);
    pb_identity(s->plugboard_out);
    s->num_plug_pairs_in  = 0;
    s->num_plug_pairs_out = 0;
}

/* ============================================================================
 * ROTOR MANAGEMENT
 * ========================================================================== */
bool enigma_select_rotors(EnigmaState *s,
                          uint8_t left, uint8_t mid, uint8_t right)
{
    if (left  >= NUM_AVAILABLE_ROTORS ||
        mid   >= NUM_AVAILABLE_ROTORS ||
        right >= NUM_AVAILABLE_ROTORS) return false;
    if (left == mid || left == right || mid == right) return false;
    load_rotor(s, 0, left);
    load_rotor(s, 1, mid);
    load_rotor(s, 2, right);
    s->rotor_positions[0] = s->rotor_positions[1] = s->rotor_positions[2] = 0;
    return true;
}

void enigma_set_positions(EnigmaState *s, uint8_t r0, uint8_t r1, uint8_t r2)
{
    s->rotor_positions[0] = r0 % ALPHABET_SIZE;
    s->rotor_positions[1] = r1 % ALPHABET_SIZE;
    s->rotor_positions[2] = r2 % ALPHABET_SIZE;
}

void enigma_set_daily_settings(EnigmaState *s, uint8_t r0, uint8_t r1, uint8_t r2)
{
    s->daily_settings[0] = r0 % ALPHABET_SIZE;
    s->daily_settings[1] = r1 % ALPHABET_SIZE;
    s->daily_settings[2] = r2 % ALPHABET_SIZE;
}

void enigma_set_ring_settings(EnigmaState *s, uint8_t r0, uint8_t r1, uint8_t r2)
{
    s->ring_settings[0] = r0 % ALPHABET_SIZE;
    s->ring_settings[1] = r1 % ALPHABET_SIZE;
    s->ring_settings[2] = r2 % ALPHABET_SIZE;
}

void enigma_reset_to_daily(EnigmaState *s)
{
    s->rotor_positions[0] = s->daily_settings[0];
    s->rotor_positions[1] = s->daily_settings[1];
    s->rotor_positions[2] = s->daily_settings[2];
}

/* ============================================================================
 * ROTOR STEPPING (double-stepping anomaly)
 * ========================================================================== */
void enigma_step_rotors(EnigmaState *s)
{
    bool mid_at_notch   = (s->rotor_positions[1] == s->rotor_notches[1]);
    bool right_at_notch = (s->rotor_positions[2] == s->rotor_notches[2]);

    if (mid_at_notch) {
        s->rotor_positions[0] = (s->rotor_positions[0] + 1) % ALPHABET_SIZE;
        s->rotor_positions[1] = (s->rotor_positions[1] + 1) % ALPHABET_SIZE;
    } else if (right_at_notch) {
        s->rotor_positions[1] = (s->rotor_positions[1] + 1) % ALPHABET_SIZE;
    }
    s->rotor_positions[2] = (s->rotor_positions[2] + 1) % ALPHABET_SIZE;
}

/* ============================================================================
 * CORE ENCRYPT — no trace
 * ========================================================================== */
char enigma_encrypt_char(EnigmaState *s, char input)
{
    int c, r;
    uint8_t off;
    int ring;

    if      (input >= 'a' && input <= 'z') c = input - 'a';
    else if (input >= 'A' && input <= 'Z') c = input - 'A';
    else return input;

    enigma_step_rotors(s);

    /* Stage 1 plugboard */
    c = s->plugboard_in[c];

    /* Forward pass R2→R1→R0 */
    for (r = NUM_ROTORS - 1; r >= 0; r--) {
        off  = s->rotor_positions[r];
        ring = s->ring_settings[r];
        c = s->rotor_wiring[r][(c + off - ring + ALPHABET_SIZE) % ALPHABET_SIZE];
        c = (c - off + ring + ALPHABET_SIZE) % ALPHABET_SIZE;
    }

    /* Reflector */
    c = s->reflector[c];

    /* Backward pass R0→R1→R2 */
    for (r = 0; r < NUM_ROTORS; r++) {
        off  = s->rotor_positions[r];
        ring = s->ring_settings[r];
        c = s->rotor_inverse[r][(c + off - ring + ALPHABET_SIZE) % ALPHABET_SIZE];
        c = (c - off + ring + ALPHABET_SIZE) % ALPHABET_SIZE;
    }

    /* Stage 2 plugboard */
    c = s->plugboard_out[c];

    s->character_count++;
    return (char)('A' + c);
}

/* ============================================================================
 * TRACED ENCRYPT — records every step for UART display
 * ========================================================================== */
char enigma_encrypt_char_traced(EnigmaState *s, char input, EnigmaTrace *t)
{
    int c;
    uint8_t off;
    int ring;

    memset(t, 0, sizeof(EnigmaTrace));
    t->input = input;

    if      (input >= 'a' && input <= 'z') c = input - 'a';
    else if (input >= 'A' && input <= 'Z') c = input - 'A';
    else { t->after_pb_out = input; return input; }

    enigma_step_rotors(s);

    t->positions[0] = s->rotor_positions[0];
    t->positions[1] = s->rotor_positions[1];
    t->positions[2] = s->rotor_positions[2];
    t->rotor_ids[0] = s->rotor_ids[0];
    t->rotor_ids[1] = s->rotor_ids[1];
    t->rotor_ids[2] = s->rotor_ids[2];
    t->pb_in_active  = (s->num_plug_pairs_in  > 0);
    t->pb_out_active = (s->num_plug_pairs_out > 0);
    t->total_changes = 7
                     + (t->pb_in_active  ? 1 : 0)
                     + (t->pb_out_active ? 1 : 0);

    /* Stage 1 plugboard */
    c = s->plugboard_in[c];
    t->after_pb_in = (char)('A' + c);

    /* R2 forward */
    off = s->rotor_positions[2]; ring = s->ring_settings[2];
    c = s->rotor_wiring[2][(c + off - ring + ALPHABET_SIZE) % ALPHABET_SIZE];
    c = (c - off + ring + ALPHABET_SIZE) % ALPHABET_SIZE;
    t->after_r_right_fwd = (char)('A' + c);

    /* R1 forward */
    off = s->rotor_positions[1]; ring = s->ring_settings[1];
    c = s->rotor_wiring[1][(c + off - ring + ALPHABET_SIZE) % ALPHABET_SIZE];
    c = (c - off + ring + ALPHABET_SIZE) % ALPHABET_SIZE;
    t->after_r_mid_fwd = (char)('A' + c);

    /* R0 forward */
    off = s->rotor_positions[0]; ring = s->ring_settings[0];
    c = s->rotor_wiring[0][(c + off - ring + ALPHABET_SIZE) % ALPHABET_SIZE];
    c = (c - off + ring + ALPHABET_SIZE) % ALPHABET_SIZE;
    t->after_r_left_fwd = (char)('A' + c);

    /* Reflector */
    c = s->reflector[c];
    t->after_reflector = (char)('A' + c);

    /* R0 backward */
    off = s->rotor_positions[0]; ring = s->ring_settings[0];
    c = s->rotor_inverse[0][(c + off - ring + ALPHABET_SIZE) % ALPHABET_SIZE];
    c = (c - off + ring + ALPHABET_SIZE) % ALPHABET_SIZE;
    t->after_r_left_rev = (char)('A' + c);

    /* R1 backward */
    off = s->rotor_positions[1]; ring = s->ring_settings[1];
    c = s->rotor_inverse[1][(c + off - ring + ALPHABET_SIZE) % ALPHABET_SIZE];
    c = (c - off + ring + ALPHABET_SIZE) % ALPHABET_SIZE;
    t->after_r_mid_rev = (char)('A' + c);

    /* R2 backward */
    off = s->rotor_positions[2]; ring = s->ring_settings[2];
    c = s->rotor_inverse[2][(c + off - ring + ALPHABET_SIZE) % ALPHABET_SIZE];
    c = (c - off + ring + ALPHABET_SIZE) % ALPHABET_SIZE;
    t->after_r_right_rev = (char)('A' + c);

    /* Stage 2 plugboard */
    c = s->plugboard_out[c];
    t->after_pb_out = (char)('A' + c);

    s->character_count++;
    return (char)('A' + c);
}

/* ============================================================================
 * PLUGBOARD API
 * ========================================================================== */
void enigma_plugboard_clear_stage(EnigmaState *s, uint8_t stage)
{
    if (stage == 0) {
        pb_identity(s->plugboard_in);
        s->num_plug_pairs_in = 0;
    } else {
        pb_identity(s->plugboard_out);
        s->num_plug_pairs_out = 0;
    }
}

void enigma_plugboard_clear(EnigmaState *s)
{
    enigma_plugboard_clear_stage(s, 0);
    enigma_plugboard_clear_stage(s, 1);
}

bool enigma_plugboard_add_pair_stage(EnigmaState *s, uint8_t stage,
                                     char a, char b)
{
    uint8_t *pb;
    uint8_t *cnt;
    uint8_t  ia, ib;

    if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
    if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
    if (a < 'A' || a > 'Z' || b < 'A' || b > 'Z') return false;
    if (a == b) return false;

    pb  = (stage == 0) ? s->plugboard_in  : s->plugboard_out;
    cnt = (stage == 0) ? &s->num_plug_pairs_in : &s->num_plug_pairs_out;

    ia = (uint8_t)(a - 'A');
    ib = (uint8_t)(b - 'A');

    if (pb[ia] != ia || pb[ib] != ib) return false;  /* already used */
    if (*cnt >= MAX_PLUGBOARD_PAIRS)   return false;

    pb[ia] = ib;
    pb[ib] = ia;
    (*cnt)++;
    return true;
}

uint8_t enigma_plugboard_get_pairs(const EnigmaState *s, uint8_t stage)
{
    return (stage == 0) ? s->num_plug_pairs_in : s->num_plug_pairs_out;
}

const char *enigma_rotor_name(uint8_t id)
{
    if (id < NUM_AVAILABLE_ROTORS) return ROTOR_NAMES[id];
    return "?";
}
