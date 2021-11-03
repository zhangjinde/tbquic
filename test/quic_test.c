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

static int QuicVariableLengthDecodeTest(void);

static QuicFuncTest TestFuncs[] = {
    {
        .test = QuicVariableLengthDecodeTest,
        .err_msg = "Varibale Length Decode Test"
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

static uint16_t QuicShortOrderTrans(uint16_t value)
{
    uint16_t v = 0;
    uint8_t b = 0;

    b = value | 0xFF;
    v = QuicBitsOrderTrans(b) << 8;
    b = value >> 8;
    v |= QuicBitsOrderTrans(b);

    return v;
}

static uint32_t QuicUintOrderTrans(uint32_t value)
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

static int QuicVariableLengthDecodeTest(void)
{
    RPacket pkt;
    uint8_t a = 0x25;
    uint16_t b = 0x7bbd;
    uint32_t c = 0x9d7f3e7d;
    uint64_t len = 0;

    if (false) {
        a = QuicBitsOrderTrans(a);
        printf("a = %d\n", a);
        b = QuicShortOrderTrans(b);
        c = QuicUintOrderTrans(c);
    } else {
        b = htons(b);
        c = htonl(c);
    }

    RPacketBufInit(&pkt, &a, sizeof(a));
    QuicVariableLengthDecode(&pkt, &len);
    if (len != 37) {
        printf("len = %lu\n", len);
        return -1;
    }
    RPacketBufInit(&pkt, (uint8_t *)&b, sizeof(b));
    QuicVariableLengthDecode(&pkt, &len);
    if (len != 15293) {
        printf("len = %lu\n", len);
        return -1;
    }
    RPacketBufInit(&pkt, (uint8_t *)&c, sizeof(c));
    QuicVariableLengthDecode(&pkt, &len);
    if (len != 494878333) {
        printf("len = %lu\n", len);
        return -1;
    }

    return 0;
}

int main(void)
{
    int i = 0;

    QuicInit();

    for (i = 0; i < QUIC_FUNC_TEST_NUM; i++) {
        if (TestFuncs[i].test() < 0) {
            fprintf(stderr, "%s failed\n", TestFuncs[i].err_msg);
            return -1;
        }
    }

    return 0;
}

