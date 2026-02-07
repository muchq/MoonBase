#include "string_splitter.h"

#include <check.h>
#include <stdlib.h>

START_TEST(test_string_splitter_null_input) {
    split_string_holder_t *holder = new_split_string_holder(NULL, ",");
    ck_assert_ptr_null(holder);
}
END_TEST

START_TEST(test_string_splitter_null_separator) {
    split_string_holder_t *holder = new_split_string_holder("hello,world", NULL);
    ck_assert_ptr_null(holder);
}
END_TEST

// Test Suite Setup
Suite *string_splitter_suite(void) {
    Suite *s = suite_create("string_splitter");

    // Negative test cases
    TCase *tc_negative = tcase_create("Splitter Negative Tests");
    tcase_add_test(tc_negative, test_string_splitter_null_input);
    tcase_add_test(tc_negative, test_string_splitter_null_separator);
    suite_add_tcase(s, tc_negative);

    // Positive test cases
    TCase *tc_positive = tcase_create("Splitter Positive Tests");
    suite_add_tcase(s, tc_positive);

    return s;
}

int main(void) {
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = string_splitter_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}