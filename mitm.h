#ifndef MITM_H_
#define MITM_H_

#include "config.h"

// MITM board is disabled in this version.

static inline void mitm_init(void) { }
static inline void mitm_set_passthrough(void) { }
static inline void mitm_set_intercept(void) { }
static inline void mitm_inject_key(uint8_t row, uint8_t col) { (void)row; (void)col; }

#endif
