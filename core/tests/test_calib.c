/* test_calib.c: zero-offset calibration rules (brief 5.11). */
#include "cc700/calib.h"
#include "unity.h"

void setUp(void) { cc_calibration_meter(0, 0, 0); cc_calibration_ack(); }
void tearDown(void) {}

static const uint32_t FEAT = 1u << 9;

static void test_unsupported_without_feature_bit_or_control_point(void)
{
    TEST_ASSERT_EQUAL(CC_CAL_UNSUPPORTED, cc_calibration_state());
    cc_calibration_meter(1, 1u << 8, 1); /* bit 8 is the indicator, not support */
    TEST_ASSERT_EQUAL(CC_CAL_UNSUPPORTED, cc_calibration_state());
    TEST_ASSERT_EQUAL(-1, cc_calibration_start(0));
    cc_calibration_meter(1, FEAT, 0);
    TEST_ASSERT_EQUAL(CC_CAL_UNSUPPORTED, cc_calibration_state());
    cc_calibration_meter(1, FEAT, 1);
    TEST_ASSERT_EQUAL(CC_CAL_IDLE, cc_calibration_state());
}

static void test_success_needs_the_exact_response(void)
{
    cc_calibration_meter(1, FEAT, 1);
    TEST_ASSERT_EQUAL(0, cc_calibration_start(1000));
    TEST_ASSERT_EQUAL(1, cc_calibration_take_request());
    TEST_ASSERT_EQUAL(0, cc_calibration_take_request());
    TEST_ASSERT_EQUAL(-1, cc_calibration_start(1100)); /* already running */
    const uint8_t other_op[] = {0x20, 0x05, 0x01};
    cc_calibration_indication(1200, other_op, 3);
    TEST_ASSERT_EQUAL(CC_CAL_RUNNING, cc_calibration_state());
    const uint8_t not_resp[] = {0x0C, 0x0C, 0x01};
    cc_calibration_indication(1200, not_resp, 3);
    TEST_ASSERT_EQUAL(CC_CAL_RUNNING, cc_calibration_state());
    const uint8_t ok[] = {0x20, 0x0C, 0x01, 0x9C, 0xFF}; /* offset -100 */
    cc_calibration_indication(1300, ok, sizeof ok);
    TEST_ASSERT_EQUAL(CC_CAL_DONE, cc_calibration_state());
    int16_t off = 0;
    TEST_ASSERT_EQUAL(1, cc_calibration_offset(&off));
    TEST_ASSERT_EQUAL(-100, off);
    cc_calibration_ack();
    TEST_ASSERT_EQUAL(CC_CAL_IDLE, cc_calibration_state());
}

static void test_failure_values_timeout_and_loss(void)
{
    cc_calibration_meter(1, FEAT, 1);
    cc_calibration_start(0);
    const uint8_t failed[] = {0x20, 0x0C, 0x04};
    cc_calibration_indication(10, failed, 3);
    TEST_ASSERT_EQUAL(CC_CAL_FAILED, cc_calibration_state());
    cc_calibration_ack();

    cc_calibration_start(5000);
    cc_calibration_tick(14999);
    TEST_ASSERT_EQUAL(CC_CAL_RUNNING, cc_calibration_state());
    cc_calibration_tick(15000);
    TEST_ASSERT_EQUAL(CC_CAL_FAILED, cc_calibration_state());
    const uint8_t late[] = {0x20, 0x0C, 0x01};
    cc_calibration_indication(15100, late, 3); /* too late: still failed */
    TEST_ASSERT_EQUAL(CC_CAL_FAILED, cc_calibration_state());
    cc_calibration_ack();

    cc_calibration_start(20000);
    cc_calibration_meter(0, 0, 0);
    TEST_ASSERT_EQUAL(CC_CAL_FAILED, cc_calibration_state());
    cc_calibration_ack();
    TEST_ASSERT_EQUAL(CC_CAL_UNSUPPORTED, cc_calibration_state());

    cc_calibration_meter(1, FEAT, 1);
    cc_calibration_start(30000);
    cc_calibration_write_failed();
    TEST_ASSERT_EQUAL(CC_CAL_FAILED, cc_calibration_state());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_unsupported_without_feature_bit_or_control_point);
    RUN_TEST(test_success_needs_the_exact_response);
    RUN_TEST(test_failure_values_timeout_and_loss);
    return UNITY_END();
}
