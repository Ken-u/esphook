/* main/test/test_config_store.c */
#include "unity.h"
#include "config_store.h"

void test_keymap_default(void)
{
    config_factory_reset();
    TEST_ASSERT_EQUAL_STRING("ok", config_get_key(0));
    TEST_ASSERT_EQUAL_STRING("continue", config_get_key(1));
    TEST_ASSERT_EQUAL_STRING("done", config_get_key(2));
}

void test_keymap_set_get(void)
{
    const char *keys[3] = {"yes", "no", "go"};
    config_set_keymap(keys);
    TEST_ASSERT_EQUAL_STRING("yes", config_get_key(0));
    TEST_ASSERT_EQUAL_STRING("no", config_get_key(1));
    TEST_ASSERT_EQUAL_STRING("go", config_get_key(2));
}

void test_keymap_partial_set(void)
{
    config_factory_reset();
    const char *keys[3] = {NULL, "no", NULL};
    config_set_keymap(keys);
    TEST_ASSERT_EQUAL_STRING("ok", config_get_key(0));
    TEST_ASSERT_EQUAL_STRING("no", config_get_key(1));
    TEST_ASSERT_EQUAL_STRING("done", config_get_key(2));
}

void test_keymap_index_out_of_range(void)
{
    TEST_ASSERT_EQUAL_STRING("ok", config_get_key(5));
    TEST_ASSERT_EQUAL_STRING("ok", config_get_key(-1));
}

void test_display_name_missing(void)
{
    config_factory_reset();
    char out[32];
    TEST_ASSERT_EQUAL_STRING("", config_get_display_name("client", "nonexistent", out, sizeof(out)));
}

void test_display_name_set_get(void)
{
    config_set_display_name("client", "espdev:claude", "Desktop");
    char out[32];
    TEST_ASSERT_EQUAL_STRING("Desktop", config_get_display_name("client", "espdev:claude", out, sizeof(out)));
}

void test_display_name_long_session_id(void)
{
    const char *session = "0123456789abcdef0123456789abcdef0123456789abcdef";
    config_set_display_name("session", session, "Long session");
    char out[32];
    TEST_ASSERT_EQUAL_STRING("Long session",
        config_get_display_name("session", session, out, sizeof(out)));
}

void test_display_name_delete(void)
{
    config_set_display_name("session", "s1", "NES");
    config_set_display_name("session", "s1", NULL);
    char out[32];
    TEST_ASSERT_EQUAL_STRING("", config_get_display_name("session", "s1", out, sizeof(out)));
}

void test_wifi_roundtrip(void)
{
    config_factory_reset();
    config_set_wifi("MyNet", "secret123");
    char ssid[64], pass[64];
    config_get_wifi_ssid(ssid, sizeof ssid);
    config_get_wifi_pass(pass, sizeof pass);
    TEST_ASSERT_EQUAL_STRING("MyNet", ssid);
    TEST_ASSERT_EQUAL_STRING("secret123", pass);
}
