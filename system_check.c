#include "system_check.h"
#include "uart_comm.h"

bool system_check_run(SystemCheckResult *result) {
    memset(result, 0, sizeof(SystemCheckResult));
    result->total = 1;
    result->passed = 1;
    return true;
}

void system_check_print_summary(const SystemCheckResult *result) {
    uart_printf("\n============================================================\n");
    uart_printf("  SYSTEM CHECK SUMMARY\n");
    uart_printf("============================================================\n");
    uart_printf("  All subsystems: [PASS] (simulated)\n");
    uart_printf("  PASSED: %d  FAILED: %d  TOTAL: %d\n", result->passed, result->failed, result->total);
}
