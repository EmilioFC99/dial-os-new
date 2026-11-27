#include "test_runner.h"

int test_failures_count = 0;
int test_assertions_count = 0;

int main(void) {
    printf("Running DialOS Phase 1 C unit tests...\n");
    run_protocol_tests();
    run_boot_sequence_tests();
    run_home_screen_tests();
    run_logger_tests();
    run_library_catalog_tests();
    run_render_tests();
    run_app_tests();

    if (test_failures_count != 0) {
        fprintf(stderr, "FAILED: %d failures across %d assertions\n",
                test_failures_count, test_assertions_count);
        return 1;
    }

    printf("PASSED: all %d assertions succeeded\n", test_assertions_count);
    return 0;
}
