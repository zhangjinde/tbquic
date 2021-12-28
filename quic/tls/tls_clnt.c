/*
 * Remy Lewis(remyknight1119@gmail.com)
 */

#include "tls.h"

#include <assert.h>
#include <tbquic/types.h>
#include <tbquic/quic.h>
#include <tbquic/cipher.h>

#include "packet_local.h"
#include "common.h"
#include "quic_local.h"
#include "tls_cipher.h"
#include "extension.h"
#include "log.h"

static int QuicTlsClientHelloBuild(QUIC_TLS *, void *);
static int QuicTlsServerHelloProc(QUIC_TLS *, void *);
static int QuicTlsEncExtProc(QUIC_TLS *, void *);
static int QuicTlsServerCertProc(QUIC_TLS *, void *);
static int QuicTlsCertVerifyProc(QUIC_TLS *, void *);
static int QuicTlsFinishedProc(QUIC_TLS *, void *);

static const QuicTlsProcess client_proc[HANDSHAKE_MAX] = {
    [QUIC_TLS_ST_OK] = {
        .flow_state = QUIC_FLOW_NOTHING,
        .next_state = QUIC_TLS_ST_CW_CLIENT_HELLO,
    },
    [QUIC_TLS_ST_CW_CLIENT_HELLO] = {
        .flow_state = QUIC_FLOW_WRITING,
        .next_state = QUIC_TLS_ST_CR_SERVER_HELLO,
        .handshake_type = CLIENT_HELLO,
        .handler = QuicTlsClientHelloBuild,
    },
    [QUIC_TLS_ST_CR_SERVER_HELLO] = {
        .flow_state = QUIC_FLOW_READING,
        .next_state = QUIC_TLS_ST_CR_ENCRYPTED_EXTENSIONS,
        .handshake_type = SERVER_HELLO,
        .handler = QuicTlsServerHelloProc,
    },
    [QUIC_TLS_ST_CR_ENCRYPTED_EXTENSIONS] = {
        .flow_state = QUIC_FLOW_READING,
        .next_state = QUIC_TLS_ST_CR_SERVER_CERTIFICATE,
        .handshake_type = ENCRYPTED_EXTENSIONS,
        .handler = QuicTlsEncExtProc,
    },
    [QUIC_TLS_ST_CR_SERVER_CERTIFICATE] = {
        .flow_state = QUIC_FLOW_READING,
        .next_state = QUIC_TLS_ST_CR_CERTIFICATE_VERIFY,
        .handshake_type = CERTIFICATE,
        .handler = QuicTlsServerCertProc,
    },
    [QUIC_TLS_ST_CR_CERTIFICATE_VERIFY] = {
        .flow_state = QUIC_FLOW_READING,
        .next_state = QUIC_TLS_ST_CR_FINISHED,
        .handshake_type = CERTIFICATE_VERIFY,
        .handler = QuicTlsCertVerifyProc,
    },
    [QUIC_TLS_ST_CR_FINISHED] = {
        .flow_state = QUIC_FLOW_READING,
        .next_state = QUIC_TLS_ST_CW_FINISHED,
        .handshake_type = FINISHED,
        .handler = QuicTlsFinishedProc,
    },
    [QUIC_TLS_ST_CW_FINISHED] = {
        .flow_state = QUIC_FLOW_WRITING,
        .next_state = QUIC_TLS_ST_HANDSHAKE_DONE,
        .handshake_type = FINISHED,
        .handler = QuicTlsFinishedBuild,
    },
    [QUIC_TLS_ST_HANDSHAKE_DONE] = {
        .flow_state = QUIC_FLOW_FINISHED,
    },
};

static QuicFlowReturn QuicTlsConnect(QUIC_TLS *tls)
{
    return QuicTlsHandshake(tls, client_proc, QUIC_NELEM(client_proc));
}

static int QuicTlsClientHelloBuild(QUIC_TLS *tls, void *packet)
{
    WPacket *pkt = packet;

    if (WPacketPut2(pkt, TLS_VERSION_1_2) < 0) {
        QUIC_LOG("Put leagacy version failed\n");
        return -1;
    }

    if (QuicTlsGenRandom(tls->client_random, sizeof(tls->client_random),
                            pkt) < 0) {
        QUIC_LOG("Generate Client Random failed\n");
        return -1;
    }

    if (WPacketPut1(pkt, 0) < 0) {
        QUIC_LOG("Put session ID len failed\n");
        return -1;
    }

    if (QuicTlsPutCipherList(tls, pkt) < 0) {
        QUIC_LOG("Put cipher list failed\n");
        return -1;
    }

    if (WPacketPut1(pkt, 1) < 0) {
        QUIC_LOG("Put compression len failed\n");
        return -1;
    }

    if (QuicTlsPutCompressionMethod(pkt) < 0) {
        QUIC_LOG("Put compression method failed\n");
        return -1;
    }

    if (TlsClientConstructExtensions(tls, pkt, TLSEXT_CLIENT_HELLO,
                                        NULL, 0) < 0) {
        QUIC_LOG("Construct extension failed\n");
        return -1;
    }

    return 0;
}

static int QuicTlsServerHelloProc(QUIC_TLS *tls, void *packet)
{
    QUIC *quic = QuicTlsTrans(tls);
    RPacket *pkt = packet;
    const TlsCipher *cipher = NULL;
    TlsCipherListNode *server_cipher = NULL;
    HLIST_HEAD(cipher_list);
    uint16_t id = 0;

    if (QuicTlsHelloHeadParse(tls, pkt, tls->server_random,
                sizeof(tls->server_random)) < 0) {
        return -1;
    }

    if (QuicTlsParseCipherList(&cipher_list, pkt, 2) < 0) {
        QUIC_LOG("Parse cipher list failed\n");
        return -1;
    }

    /* Get the only one cipher member */
    hlist_for_each_entry(server_cipher, &cipher_list, node) {
        assert(server_cipher->cipher != NULL);
        id = server_cipher->cipher->id;
        break;
    }

    QuicTlsDestroyCipherList(&cipher_list);

    if (id == 0) {
        QUIC_LOG("Get server cipher failed\n");
        return -1;
    }

    cipher = QuicTlsCipherMatchListById(&tls->cipher_list, id);
    if (cipher == NULL) {
        QUIC_LOG("Get shared cipher failed\n");
        return -1;
    }

    tls->handshake_cipher = cipher;
    /* Skip legacy Compression Method */
    if (RPacketPull(pkt, 1) < 0) {
        return -1;
    }

    if (TlsClientParseExtensions(tls, pkt, TLSEXT_SERVER_HELLO, NULL, 0) < 0) {
        return -1;
    }

    //change cipher state
    if (QuicCreateHandshakeServerDecoders(quic) < 0) {
        return -1;
    }

    QuicBufClear(QUIC_TLS_BUFFER(quic));
    return 0;
}

static int QuicTlsEncExtProc(QUIC_TLS *tls, void *packet)
{
    QUIC_LOG("in\n");
    return TlsClientParseExtensions(tls, packet, TLSEXT_SERVER_HELLO, NULL, 0);
}

static int QuicTlsServerCertProc(QUIC_TLS *tls, void *packet)
{
    QUIC *quic = QuicTlsTrans(tls);
    RPacket *pkt = packet;
    const uint8_t *certbytes = NULL;
    const uint8_t *certstart = NULL;
    STACK_OF(X509) *sk = NULL;
    X509 *x = NULL;
    RPacket extensions = {};
    size_t chainidx = 0;
    uint32_t context = 0;
    uint32_t cert_list_len = 0;
    uint32_t cert_len = 0;
    int v = 0;
    int ret = -1;

    if (RPacketGet1(pkt, &context) < 0) {
        return -1;
    }

    if (RPacketGet3(pkt, &cert_list_len) < 0) {
        return -1;
    }

    if (RPacketRemaining(pkt) != cert_list_len) {
        return -1;
    }

    if ((sk = sk_X509_new_null()) == NULL) {
        return -1;
    }

    for (chainidx = 0; RPacketRemaining(pkt); chainidx++) {
        if (RPacketGet3(pkt, &cert_len) < 0) {
            QUIC_LOG("Get cert len failed\n");
            goto out;
        }

        if (RPacketGetBytes(pkt, &certbytes, cert_len) < 0) {
            QUIC_LOG("Get bytes(%u) failed\n", cert_len);
            goto out;
        }

        certstart = certbytes;
        x = d2i_X509(NULL, (const unsigned char **)&certbytes, cert_len);
        if (x == NULL) {
            QUIC_LOG("Parse cert failed\n");
            goto out;
        }
        
        if (certbytes != (certstart + cert_len)) {
            QUIC_LOG("Cert bytes not match(b = %p, s = %p))\n",
                    certbytes, certstart + cert_len);
            goto out;
        }
        
        if (RPacketGetLengthPrefixed2(pkt, &extensions) < 0) {
            QUIC_LOG("Get cert extension failed\n");
            goto out;
        }
        
        if (RPacketRemaining(&extensions) && TlsClientParseExtensions(tls,
                    &extensions, TLSEXT_CERTIFICATE, x, chainidx) < 0) {
            QUIC_LOG("Parse cert extension failed\n");
            goto out;
        }

        if (!sk_X509_push(sk, x)) {
            goto out;
        }
        x = NULL;
    }

    v = QuicVerifyCertChain(quic, sk);
    if (quic->verify_mode != QUIC_TLS_VERIFY_NONE && v < 0) {
        goto out;
    }

    ret = 0;
out:
    QUIC_LOG("in\n");
    X509_free(x);
    sk_X509_pop_free(sk, X509_free);
    return ret;
}

static int QuicTlsCertVerifyProc(QUIC_TLS *, void *)
{
    QUIC_LOG("in\n");
    return 0;
}

static int QuicTlsFinishedProc(QUIC_TLS *, void *)
{
    QUIC_LOG("in\n");
    return 0;
}

void QuicTlsClientInit(QUIC_TLS *tls)
{
    tls->handshake = QuicTlsConnect;
}
