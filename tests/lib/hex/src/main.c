#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>

ZTEST(hex, test_char2hex_valid)
{
    uint8_t x;

    zassert_ok(char2hex('0', &x));
    zassert_equal(x, 0U, NULL);
    zassert_ok(char2hex('9', &x));
    zassert_equal(x, 9U, NULL);
    zassert_ok(char2hex('a', &x));
    zassert_equal(x, 10U, NULL);
    zassert_ok(char2hex('f', &x));
    zassert_equal(x, 15U, NULL);
    zassert_ok(char2hex('A', &x));
    zassert_equal(x, 10U, NULL);
    zassert_ok(char2hex('F', &x));
    zassert_equal(x, 15U, NULL);
}

ZTEST(hex, test_char2hex_invalid)
{
    uint8_t x;
    zassert_true(char2hex('g', &x) < 0, NULL);
}

ZTEST(hex, test_hex2char_valid)
{
    char c;

    for (uint8_t i = 0; i <= 9; i++) {
        zassert_ok(hex2char(i, &c));
        zassert_equal(c, '0' + i, NULL);
    }
    for (uint8_t i = 10; i <= 15; i++) {
        zassert_ok(hex2char(i, &c));
        zassert_equal(c, 'a' + (i - 10), NULL);
    }
}

ZTEST(hex, test_hex2char_invalid)
{
    char c;
    zassert_true(hex2char(16, &c) < 0, NULL);
}

ZTEST(hex, test_bin2hex_success)
{
    uint8_t buf[] = {0x12, 0xaf, 0x00, 0xff};
    char hexstr[9];

    size_t len = bin2hex(buf, sizeof(buf), hexstr, sizeof(hexstr));

    zassert_equal(len, 8U, NULL);
    zassert_true(strcmp(hexstr, "12af00ff") == 0, NULL);
}

ZTEST(hex, test_bin2hex_insufficient)
{
    uint8_t buf[2] = {0x12, 0x34};
    char hexstr[4];

    zassert_equal(bin2hex(buf, sizeof(buf), hexstr, sizeof(hexstr)), 0U, NULL);
}

ZTEST(hex, test_hex2bin_even)
{
    const char *hex = "12af";
    uint8_t buf[2];

    size_t len = hex2bin(hex, strlen(hex), buf, sizeof(buf));

    zassert_equal(len, 2U, NULL);
    zassert_equal(buf[0], 0x12, NULL);
    zassert_equal(buf[1], 0xaf, NULL);
}

ZTEST(hex, test_hex2bin_odd)
{
    const char *hex = "f12";
    uint8_t buf[2];

    size_t len = hex2bin(hex, strlen(hex), buf, sizeof(buf));

    zassert_equal(len, 2U, NULL);
    zassert_equal(buf[0], 0x0f, NULL);
    zassert_equal(buf[1], 0x12, NULL);
}

ZTEST(hex, test_hex2bin_invalid_char)
{
    const char *hex = "12xz";
    uint8_t buf[2];

    zassert_equal(hex2bin(hex, strlen(hex), buf, sizeof(buf)), 0U, NULL);
}

ZTEST(hex, test_hex2bin_insufficient)
{
    const char *hex = "1122";
    uint8_t buf[1];

    zassert_equal(hex2bin(hex, strlen(hex), buf, sizeof(buf)), 0U, NULL);
}

ZTEST_SUITE(hex, NULL, NULL, NULL, NULL, NULL);

