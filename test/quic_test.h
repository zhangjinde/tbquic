#ifndef TBQUIC_TEST_QUIC_TEST_H_
#define TBQUIC_TEST_QUIC_TEST_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include <tbquic/types.h>

#define ARRAY_SIZE(array)    (sizeof(array)/sizeof(array[0]))
#define MSG_SIZE(m)  ((sizeof(m) - 1)/2)
#define QUIC_TEST_BUF_LEN 2048
#define QUIC_RECORD_MSS_LEN  1250
#define QUIC_TEST_EVENT_MAX_NUM   10

typedef union UdpConnKey {
	struct sockaddr 		addr;
	struct sockaddr_in  	addr4;
	struct sockaddr_in6  	addr6;
} QuicUdpConnKey;

typedef struct {
    uint64_t type;
    uint64_t value;
} TlsTestParam;

typedef struct {
    void *data;
    size_t len;
} QuicTestData;

typedef struct {
    uint8_t buf[QUIC_TEST_BUF_LEN];
} QuicTestBuff;

static inline uint8_t char2hex(char c)
{
	if ((c >= '0') && (c <= '9')) {
		return c - 0x30;
	}

	if ((c >= 'A') && (c <= 'F')) {
		return c - 0x37;
	}

	if ((c >= 'a') && (c <= 'f')) {
		return c - 'a' + 10;
	}

	return 0;
}

static inline void str2hex(uint8_t *dest, char *src, size_t len)
{
	char h1 = 0;
	char h2 = 0;
	uint8_t s1 = 0;
	uint8_t s2 = 0;
	size_t i = 0;

	for (i = 0; i < len; i++) {
		h1 = src[2*i];
		h2 = src[2*i + 1];

		s1 = char2hex(h1);
		s2 = char2hex(h2);

		dest[i] = (s1 << 4) + s2;
	}
}

extern char *quic_cert;
extern char *quic_key;
extern char *quic_ca;

void QuicKeyLog(const QUIC *, const char *);
int QuicTlsCtxClientExtensionSet(QUIC_CTX *);
int QuicTlsClientExtensionSet(QUIC *);
int QuicCtxServerExtensionSet(QUIC_CTX *);
void QuicTestStreamIovecInit(QUIC_STREAM_IOVEC *, QuicTestBuff *, size_t);
void QuicSetVerify(void *, int, char *);
void AddEpollEvent(int, struct epoll_event *, int);
int QuicVariableLengthDecodeTest(void);
int QuicHkdfExtractExpandTest(void);
int QuicHkdfExpandLabel(void);
int QuicPktFormatTestClient(void);
int QuicPktFormatTestServer(void);
int QuicPktNumberEncodeTest(void);
int QuicPktNumberDecodeTest(void);
int QuicWPacketSubMemcpyVarTest(void);
int QuicSessionAsn1Test(void);
int QuicConstructStatelessTicket(void);
int QuicDecryptStatelessTicket(void);
int QuicHandshakeTest(void);
int TlsCipherListTest(void);
int TlsClientHelloTest(void);
int TlsClientExtensionTest(void);
int TlsGenerateSecretTest(void);
int TlsGenerateServerSecretTest(void);
int TlsClientHandshakeReadTest(void);
int TlsGenerateMasterSecretTest(void);
int TlsTlsFinalFinishMacTest(void);
int TlsPskDoBinderTest(void);

#endif
