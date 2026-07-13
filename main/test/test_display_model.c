/* main/test/test_display_model.c */
#include "unity.h"
#include "display_model.h"
#include <stdio.h>
#include <string.h>

void setUp(void) { dm_init(); }
void tearDown(void) {}

void test_notify_increments(void)
{
    dm_notify("espdev:claude", "s1", "done", "ok");
    TEST_ASSERT_EQUAL(1, dm_client_unread("espdev:claude"));
    dm_notify("espdev:claude", "s1", "done2", "ok");
    TEST_ASSERT_EQUAL(2, dm_client_unread("espdev:claude"));
}

void test_dismiss_decrements(void)
{
    dm_notify("a", "s1", "t", "b");
    dm_notify("a", "s1", "t", "b");
    dm_dismiss("a", "s1");
    TEST_ASSERT_EQUAL(1, dm_client_unread("a"));
    dm_dismiss("a", "s1");
    TEST_ASSERT_EQUAL(0, dm_client_unread("a"));
}

void test_dismiss_removes_when_zero(void)
{
    dm_notify("a", "s1", "t", "b");
    dm_dismiss("a", "s1");
    TEST_ASSERT_EQUAL(0, dm_client_count());
}

void test_dismiss_nonexistent_noop(void)
{
    dm_notify("a", "s1", "t", "b");
    dm_dismiss("a", "s2");  /* 不存在的 session */
    TEST_ASSERT_EQUAL(1, dm_client_unread("a"));
    TEST_ASSERT_EQUAL(1, dm_client_count());
}

void test_multi_session_same_client_sums(void)
{
    dm_notify("a", "s1", "t", "b");
    dm_notify("a", "s2", "t", "b");
    dm_notify("a", "s1", "t", "b");
    TEST_ASSERT_EQUAL(3, dm_client_unread("a"));
    TEST_ASSERT_EQUAL(1, dm_client_count());
}

void test_dismiss_one_session_only(void)
{
    dm_notify("a", "s1", "t", "b");
    dm_notify("a", "s2", "t", "b");
    dm_dismiss("a", "s1");
    TEST_ASSERT_EQUAL(1, dm_client_unread("a"));
}

void test_multi_client_separate(void)
{
    dm_notify("a", "s1", "t", "b");
    dm_notify("b", "s1", "t", "b");
    TEST_ASSERT_EQUAL(1, dm_client_unread("a"));
    TEST_ASSERT_EQUAL(1, dm_client_unread("b"));
    TEST_ASSERT_EQUAL(2, dm_client_count());
}

void test_current_set_on_notify(void)
{
    dm_notify("a", "s1", "title1", "body1");
    char c[24], s[48], t[32], b[128];
    TEST_ASSERT_TRUE(dm_current(c, s, sizeof c, sizeof s, t, sizeof t, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("a", c);
    TEST_ASSERT_EQUAL_STRING("title1", t);
    TEST_ASSERT_EQUAL_STRING("body1", b);
}

void test_clear_current(void)
{
    dm_notify("a", "s1", "t", "b");
    dm_clear_current();
    char c[24], s[48];
    TEST_ASSERT_FALSE(dm_current(c, s, sizeof c, sizeof s, NULL, 0, NULL, 0));
}

void test_dismiss_current_clears_current(void)
{
    dm_notify("a", "s1", "t", "b");
    dm_dismiss("a", "s1");
    char c[24], s[48];
    TEST_ASSERT_FALSE(dm_current(c, s, sizeof c, sizeof s, NULL, 0, NULL, 0));
}

void test_overflow_drops_oldest(void)
{
    for (int i = 0; i < 40; i++) {
        char sid[16];
        snprintf(sid, sizeof sid, "s%d", i);
        dm_notify("a", sid, "t", "b");
    }
    /* 最多 32，溢出丢最旧。s0 应已不在（被覆盖）。
       dismiss s0 不影响计数——证明它已不占槽位。 */
    int before = dm_client_unread("a");
    dm_dismiss("a", "s0");
    TEST_ASSERT_EQUAL(before, dm_client_unread("a"));
    /* 但最新的 s39 应在 */
    dm_dismiss("a", "s39");
    TEST_ASSERT_EQUAL(before - 1, dm_client_unread("a"));
}

void test_client_at_enumerates(void)
{
    dm_notify("a", "s1", "t", "b");
    dm_notify("b", "s1", "t", "b");
    char out[24];
    dm_client_at(0, out, sizeof out);
    TEST_ASSERT_TRUE(strcmp(out, "a") == 0 || strcmp(out, "b") == 0);
    dm_client_at(2, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("", out);  /* 越界返回空 */
}
