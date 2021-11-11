/*
 * Remy Lewis(remyknight1119@gmail.com)
 */

#include "quic_test.h"

#include <tbquic/quic.h>

#include <stdio.h>
#include <arpa/inet.h>

#include "packet_format.h"
#include "packet_local.h"
#include "common.h"

typedef struct FuncTest {
    int (*test)(void);
    char *err_msg;
} QuicFuncTest;

static QuicFuncTest TestFuncs[] = {
    {
        .test = QuicVariableLengthDecodeTest,
        .err_msg = "Varibale Length Decode",
    },
    {
        .test = QuicHkdfExtractExpandTest,
        .err_msg = "HKDF Extract and Expand",
    },
    {
        .test = QuicHkdfExpandLabel,
        .err_msg = "HKDF Expand Label",
    },
    {
        .test = QuicPktFormatTest,
        .err_msg = "Packet Format",
    },
    {
        .test = QuicPktNumberDecodeTest,
        .err_msg = "PKT Number Decode",
    },
};

#define QUIC_FUNC_TEST_NUM QUIC_ARRAY_SIZE(TestFuncs)

static uint8_t QuicBitsOrderTrans(uint8_t value)
{
    uint8_t v = 0;
    int i = 0;

    for (i = 0; i < 8; i++) {
        v |= (((value >> i) & 0x1) << (7 - i));
    }

    return v;
}

uint16_t QuicShortOrderTrans(uint16_t value)
{
    uint16_t v = 0;
    uint8_t b = 0;

    b = value | 0xFF;
    v = QuicBitsOrderTrans(b) << 8;
    b = value >> 8;
    v |= QuicBitsOrderTrans(b);

    return v;
}

uint32_t QuicUintOrderTrans(uint32_t value)
{
    uint32_t v = 0;
    uint8_t b = 0;
    int i = 0;

    for (i = 0; i < 4; i++) {
        b = (value >> (i * 8)) | 0xFF;
        v |= QuicBitsOrderTrans(b) << ((3 - i) * 8);
    }

    return v;
}

int main(void)
{
    int passed = 0;
    int ok_num = 0;
    int i = 0;
    int ret = 0;

    QuicInit();

    for (i = 0; i < QUIC_FUNC_TEST_NUM; i++) {
        ret = TestFuncs[i].test();
        if (ret < 0) {
            fprintf(stderr, "%s failed\n", TestFuncs[i].err_msg);
        } else {
            passed++;
            ok_num += ret;
        }
    }

    fprintf(stdout, "%d/%lu Function test passed! Total passed case number"
            " is %d\n", passed, QUIC_FUNC_TEST_NUM, ok_num);
    return 0;
}
