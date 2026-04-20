#ifndef SYSTEM_CHECK_H_
#define SYSTEM_CHECK_H_

#include "config.h"

typedef struct {
    uint8_t total, passed, failed, skipped;
} SystemCheckResult;

bool system_check_run(SystemCheckResult *result);
void system_check_print_summary(const SystemCheckResult *result);

#endif
