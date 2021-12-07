/*
 * Remy Lewis(remyknight1119@gmail.com)
 * RFC 9001, Appendix A.  Sample Packet Protection
 */

#include "quic_test.h"

#include <assert.h>
#include <string.h>
#include <tbquic/quic.h>
#include <openssl/bio.h>

#include "quic_local.h"
#include "packet_local.h"
#include "packet_format.h"
#include "common.h"

static uint8_t cid[] = "\x83\x94\xC8\xF0\x3E\x51\x57\x08";
static uint8_t client_iv[] = "\xFA\x04\x4B\x2F\x42\xA3\xFD\x3B\x46\xFB\x25\x5C";
static uint8_t server_iv[] = "\x0A\xC1\x49\x3C\xA1\x90\x58\x53\xB0\xBB\xA0\x3E";
static uint8_t client_init_packet[] =
    "\xC0\x00\x00\x00\x01\x08\x83\x94\xC8\xF0\x3E\x51\x57\x08\x00\x00"
    "\x44\x9E\x7B\x9A\xEC\x34\xD1\xB1\xC9\x8D\xD7\x68\x9F\xB8\xEC\x11"
    "\xD2\x42\xB1\x23\xDC\x9B\xD8\xBA\xB9\x36\xB4\x7D\x92\xEC\x35\x6C"
    "\x0B\xAB\x7D\xF5\x97\x6D\x27\xCD\x44\x9F\x63\x30\x00\x99\xF3\x99"
    "\x1C\x26\x0E\xC4\xC6\x0D\x17\xB3\x1F\x84\x29\x15\x7B\xB3\x5A\x12"
    "\x82\xA6\x43\xA8\xD2\x26\x2C\xAD\x67\x50\x0C\xAD\xB8\xE7\x37\x8C"
    "\x8E\xB7\x53\x9E\xC4\xD4\x90\x5F\xED\x1B\xEE\x1F\xC8\xAA\xFB\xA1"
    "\x7C\x75\x0E\x2C\x7A\xCE\x01\xE6\x00\x5F\x80\xFC\xB7\xDF\x62\x12"
    "\x30\xC8\x37\x11\xB3\x93\x43\xFA\x02\x8C\xEA\x7F\x7F\xB5\xFF\x89"
    "\xEA\xC2\x30\x82\x49\xA0\x22\x52\x15\x5E\x23\x47\xB6\x3D\x58\xC5"
    "\x45\x7A\xFD\x84\xD0\x5D\xFF\xFD\xB2\x03\x92\x84\x4A\xE8\x12\x15"
    "\x46\x82\xE9\xCF\x01\x2F\x90\x21\xA6\xF0\xBE\x17\xDD\xD0\xC2\x08"
    "\x4D\xCE\x25\xFF\x9B\x06\xCD\xE5\x35\xD0\xF9\x20\xA2\xDB\x1B\xF3"
    "\x62\xC2\x3E\x59\x6D\x11\xA4\xF5\xA6\xCF\x39\x48\x83\x8A\x3A\xEC"
    "\x4E\x15\xDA\xF8\x50\x0A\x6E\xF6\x9E\xC4\xE3\xFE\xB6\xB1\xD9\x8E"
    "\x61\x0A\xC8\xB7\xEC\x3F\xAF\x6A\xD7\x60\xB7\xBA\xD1\xDB\x4B\xA3"
    "\x48\x5E\x8A\x94\xDC\x25\x0A\xE3\xFD\xB4\x1E\xD1\x5F\xB6\xA8\xE5"
    "\xEB\xA0\xFC\x3D\xD6\x0B\xC8\xE3\x0C\x5C\x42\x87\xE5\x38\x05\xDB"
    "\x05\x9A\xE0\x64\x8D\xB2\xF6\x42\x64\xED\x5E\x39\xBE\x2E\x20\xD8"
    "\x2D\xF5\x66\xDA\x8D\xD5\x99\x8C\xCA\xBD\xAE\x05\x30\x60\xAE\x6C"
    "\x7B\x43\x78\xE8\x46\xD2\x9F\x37\xED\x7B\x4E\xA9\xEC\x5D\x82\xE7"
    "\x96\x1B\x7F\x25\xA9\x32\x38\x51\xF6\x81\xD5\x82\x36\x3A\xA5\xF8"
    "\x99\x37\xF5\xA6\x72\x58\xBF\x63\xAD\x6F\x1A\x0B\x1D\x96\xDB\xD4"
    "\xFA\xDD\xFC\xEF\xC5\x26\x6B\xA6\x61\x17\x22\x39\x5C\x90\x65\x56"
    "\xBE\x52\xAF\xE3\xF5\x65\x63\x6A\xD1\xB1\x7D\x50\x8B\x73\xD8\x74"
    "\x3E\xEB\x52\x4B\xE2\x2B\x3D\xCB\xC2\xC7\x46\x8D\x54\x11\x9C\x74"
    "\x68\x44\x9A\x13\xD8\xE3\xB9\x58\x11\xA1\x98\xF3\x49\x1D\xE3\xE7"
    "\xFE\x94\x2B\x33\x04\x07\xAB\xF8\x2A\x4E\xD7\xC1\xB3\x11\x66\x3A"
    "\xC6\x98\x90\xF4\x15\x70\x15\x85\x3D\x91\xE9\x23\x03\x7C\x22\x7A"
    "\x33\xCD\xD5\xEC\x28\x1C\xA3\xF7\x9C\x44\x54\x6B\x9D\x90\xCA\x00"
    "\xF0\x64\xC9\x9E\x3D\xD9\x79\x11\xD3\x9F\xE9\xC5\xD0\xB2\x3A\x22"
    "\x9A\x23\x4C\xB3\x61\x86\xC4\x81\x9E\x8B\x9C\x59\x27\x72\x66\x32"
    "\x29\x1D\x6A\x41\x82\x11\xCC\x29\x62\xE2\x0F\xE4\x7F\xEB\x3E\xDF"
    "\x33\x0F\x2C\x60\x3A\x9D\x48\xC0\xFC\xB5\x69\x9D\xBF\xE5\x89\x64"
    "\x25\xC5\xBA\xC4\xAE\xE8\x2E\x57\xA8\x5A\xAF\x4E\x25\x13\xE4\xF0"
    "\x57\x96\xB0\x7B\xA2\xEE\x47\xD8\x05\x06\xF8\xD2\xC2\x5E\x50\xFD"
    "\x14\xDE\x71\xE6\xC4\x18\x55\x93\x02\xF9\x39\xB0\xE1\xAB\xD5\x76"
    "\xF2\x79\xC4\xB2\xE0\xFE\xB8\x5C\x1F\x28\xFF\x18\xF5\x88\x91\xFF"
    "\xEF\x13\x2E\xEF\x2F\xA0\x93\x46\xAE\xE3\x3C\x28\xEB\x13\x0F\xF2"
    "\x8F\x5B\x76\x69\x53\x33\x41\x13\x21\x19\x96\xD2\x00\x11\xA1\x98"
    "\xE3\xFC\x43\x3F\x9F\x25\x41\x01\x0A\xE1\x7C\x1B\xF2\x02\x58\x0F"
    "\x60\x47\x47\x2F\xB3\x68\x57\xFE\x84\x3B\x19\xF5\x98\x40\x09\xDD"
    "\xC3\x24\x04\x4E\x84\x7A\x4F\x4A\x0A\xB3\x4F\x71\x95\x95\xDE\x37"
    "\x25\x2D\x62\x35\x36\x5E\x9B\x84\x39\x2B\x06\x10\x85\x34\x9D\x73"
    "\x20\x3A\x4A\x13\xE9\x6F\x54\x32\xEC\x0F\xD4\xA1\xEE\x65\xAC\xCD"
    "\xD5\xE3\x90\x4D\xF5\x4C\x1D\xA5\x10\xB0\xFF\x20\xDC\xC0\xC7\x7F"
    "\xCB\x2C\x0E\x0E\xB6\x05\xCB\x05\x04\xDB\x87\x63\x2C\xF3\xD8\xB4"
    "\xDA\xE6\xE7\x05\x76\x9D\x1D\xE3\x54\x27\x01\x23\xCB\x11\x45\x0E"
    "\xFC\x60\xAC\x47\x68\x3D\x7B\x8D\x0F\x81\x13\x65\x56\x5F\xD9\x8C"
    "\x4C\x8E\xB9\x36\xBC\xAB\x8D\x06\x9F\xC3\x3B\xD8\x01\xB0\x3A\xDE"
    "\xA2\xE1\xFB\xC5\xAA\x46\x3D\x08\xCA\x19\x89\x6D\x2B\xF5\x9A\x07"
    "\x1B\x85\x1E\x6C\x23\x90\x52\x17\x2F\x29\x6B\xFB\x5E\x72\x40\x47"
    "\x90\xA2\x18\x10\x14\xF3\xB9\x4A\x4E\x97\xD1\x17\xB4\x38\x13\x03"
    "\x68\xCC\x39\xDB\xB2\xD1\x98\x06\x5A\xE3\x98\x65\x47\x92\x6C\xD2"
    "\x16\x2F\x40\xA2\x9F\x0C\x3C\x87\x45\xC0\xF5\x0F\xBA\x38\x52\xE5"
    "\x66\xD4\x45\x75\xC2\x9D\x39\xA0\x3F\x0C\xDA\x72\x19\x84\xB6\xF4"
    "\x40\x59\x1F\x35\x5E\x12\xD4\x39\xFF\x15\x0A\xAB\x76\x13\x49\x9D"
    "\xBD\x49\xAD\xAB\xC8\x67\x6E\xEF\x02\x3B\x15\xB6\x5B\xFC\x5C\xA0"
    "\x69\x48\x10\x9F\x23\xF3\x50\xDB\x82\x12\x35\x35\xEB\x8A\x74\x33"
    "\xBD\xAB\xCB\x90\x92\x71\xA6\xEC\xBC\xB5\x8B\x93\x6A\x88\xCD\x4E"
    "\x8F\x2E\x6F\xF5\x80\x01\x75\xF1\x13\x25\x3D\x8F\xA9\xCA\x88\x85"
    "\xC2\xF5\x52\xE6\x57\xDC\x60\x3F\x25\x2E\x1A\x8E\x30\x8F\x76\xF0"
    "\xBE\x79\xE2\xFB\x8F\x5D\x5F\xBB\xE2\xE3\x0E\xCA\xDD\x22\x07\x23"
    "\xC8\xC0\xAE\xA8\x07\x8C\xDF\xCB\x38\x68\x26\x3F\xF8\xF0\x94\x00"
    "\x54\xDA\x48\x78\x18\x93\xA7\xE4\x9A\xD5\xAF\xF4\xAF\x30\x0C\xD8"
    "\x04\xA6\xB6\x27\x9A\xB3\xFF\x3A\xFB\x64\x49\x1C\x85\x19\x4A\xAB"
    "\x76\x0D\x58\xA6\x06\x65\x4F\x9F\x44\x00\xE8\xB3\x85\x91\x35\x6F"
    "\xBF\x64\x25\xAC\xA2\x6D\xC8\x52\x44\x25\x9F\xF2\xB1\x9C\x41\xB9"
    "\xF9\x6F\x3C\xA9\xEC\x1D\xDE\x43\x4D\xA7\xD2\xD3\x92\xB9\x05\xDD"
    "\xF3\xD1\xF9\xAF\x93\xD1\xAF\x59\x50\xBD\x49\x3F\x5A\xA7\x31\xB4"
    "\x05\x6D\xF3\x1B\xD2\x67\xB6\xB9\x0A\x07\x98\x31\xAA\xF5\x79\xBE"
    "\x0A\x39\x01\x31\x37\xAA\xC6\xD4\x04\xF5\x18\xCF\xD4\x68\x40\x64"
    "\x7E\x78\xBF\xE7\x06\xCA\x4C\xF5\xE9\xC5\x45\x3E\x9F\x7C\xFD\x2B"
    "\x8B\x4C\x8D\x16\x9A\x44\xE5\x5C\x88\xD4\xA9\xA7\xF9\x47\x42\x41"
    "\xE2\x21\xAF\x44\x86\x00\x18\xAB\x08\x56\x97\x2E\x19\x4C\xD9\x34";

static uint8_t payload_plaintext[1162] = 
    "\x06\x00\x40\xF1\x01\x00\x00\xED\x03\x03\xEB\xF8\xFA\x56\xF1\x29"
    "\x39\xB9\x58\x4A\x38\x96\x47\x2E\xC4\x0B\xB8\x63\xCF\xD3\xE8\x68"
    "\x04\xFE\x3A\x47\xF0\x6A\x2B\x69\x48\x4C\x00\x00\x04\x13\x01\x13"
    "\x02\x01\x00\x00\xC0\x00\x00\x00\x10\x00\x0E\x00\x00\x0B\x65\x78"
    "\x61\x6D\x70\x6C\x65\x2E\x63\x6F\x6D\xFF\x01\x00\x01\x00\x00\x0A"
    "\x00\x08\x00\x06\x00\x1D\x00\x17\x00\x18\x00\x10\x00\x07\x00\x05"
    "\x04\x61\x6C\x70\x6E\x00\x05\x00\x05\x01\x00\x00\x00\x00\x00\x33"
    "\x00\x26\x00\x24\x00\x1D\x00\x20\x93\x70\xB2\xC9\xCA\xA4\x7F\xBA"
    "\xBA\xF4\x55\x9F\xED\xBA\x75\x3D\xE1\x71\xFA\x71\xF5\x0F\x1C\xE1"
    "\x5D\x43\xE9\x94\xEC\x74\xD7\x48\x00\x2B\x00\x03\x02\x03\x04\x00"
    "\x0D\x00\x10\x00\x0E\x04\x03\x05\x03\x06\x03\x02\x03\x08\x04\x08"
    "\x05\x08\x06\x00\x2D\x00\x02\x01\x01\x00\x1C\x00\x02\x40\x01\x00"
    "\x39\x00\x32\x04\x08\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x05\x04\x80"
    "\x00\xFF\xFF\x07\x04\x80\x00\xFF\xFF\x08\x01\x10\x01\x04\x80\x00"
    "\x75\x30\x09\x01\x10\x0F\x08\x83\x94\xC8\xF0\x3E\x51\x57\x08\x06"
    "\x04\x80\x00\xFF\xFF";

static void QuicPktPayloadInject(QUIC_BUFFER *buffer)
{
    size_t len = sizeof(payload_plaintext);

    assert(QUIC_GE(QuicBufLength(buffer), len));

    memcpy(QuicBufData(buffer), payload_plaintext, len);
    buffer->data_len = len;
}

int QuicPktFormatTestClient(void)
{
    QUIC_CTX *ctx = NULL;
    QUIC *quic = NULL;
    BIO *rbio = NULL;
    BIO *wbio = NULL;
    BIO *out_bio = NULL;
    static uint8_t buf[2048] = {};
    size_t msg_len = sizeof(client_init_packet) - 1;
    size_t max_len = 4;
    int case_num = -1;
    int rlen = 0;
    int ret = 0;

    ctx = QuicCtxNew(QuicClientMethod());
    if (ctx == NULL) {
        goto out;
    }

    quic = QuicNew(ctx);
    if (quic == NULL) {
        goto out;
    }

    rbio = BIO_new(BIO_s_mem());
    if (rbio == NULL) {
        goto out;
    }

    wbio = BIO_new(BIO_s_mem());
    if (wbio == NULL) {
        goto out;
    }
    QUIC_set_bio(quic, rbio, wbio);

    out_bio = wbio;
    rbio = NULL;
    wbio = NULL;

    quic->dcid.data = cid;
    quic->dcid.len = sizeof(cid) - 1;
    quic->initial.encrypt.pkt_num = 1;

    QuicEncryptFrameHook = QuicPktPayloadInject;

    if (QuicCtrl(quic, QUIC_CTRL_SET_PKT_NUM_MAX_LEN, &max_len, 0) < 0) {
        goto out;
    }
    ret = QuicDoHandshake(quic);
    quic->dcid.data = NULL;
    if (ret < 0) {
        printf("Do Client Handshake failed\n");
        goto out;
    }

    if (memcmp(client_iv, quic->initial.encrypt.ciphers.pp_cipher.iv,
                sizeof(client_iv) - 1) != 0) {
        QuicPrint(quic->initial.encrypt.ciphers.pp_cipher.iv,
                sizeof(client_iv) - 1);
        printf("Client IV incorrect\n");
        goto out;
    }

    if (memcmp(server_iv, quic->initial.decrypt.ciphers.pp_cipher.iv,
                sizeof(server_iv) - 1) != 0) {
        printf("Server IV incorrect\n");
        goto out;
    }

    rlen = BIO_read(out_bio, buf, sizeof(buf));
    if (rlen != msg_len) {
        printf("Message len incorrect, rlen %d, msg_len %lu\n", rlen, msg_len);
        goto out;
    }

    if (memcmp(buf, client_init_packet, rlen) != 0) {
        printf("Message content incorrect\n");
        goto out;
    }

    case_num = 1;
out:
    BIO_free(rbio);
    BIO_free(wbio);
    QuicFree(quic);
    QuicCtxFree(ctx);

    return case_num;
}

int QuicPktFormatTestServer(void)
{
    QUIC_CTX *ctx = NULL;
    QUIC *quic = NULL;
    BIO *rbio = NULL;
    BIO *wbio = NULL;
    int case_num = -1;
    int ret = 0;

    ctx = QuicCtxNew(QuicServerMethod());
    if (ctx == NULL) {
        goto out;
    }

    quic = QuicNew(ctx);
    if (quic == NULL) {
        goto out;
    }

    rbio = BIO_new(BIO_s_mem());
    if (rbio == NULL) {
        goto out;
    }

    wbio = BIO_new(BIO_s_mem());
    if (wbio == NULL) {
        goto out;
    }
    QUIC_set_bio(quic, rbio, wbio);

    BIO_write(rbio, client_init_packet, sizeof(client_init_packet) - 1);
    rbio = NULL;
    wbio = NULL;

    if (QuicCtxUsePrivateKeyFile(ctx, quic_key, QUIC_FILE_TYPE_PEM) < 0) {
        printf("Use Private Key file %s failed\n", quic_key);
        goto out;
    }

    if (QuicCtxUseCertificate_File(ctx, quic_cert, QUIC_FILE_TYPE_PEM) < 0) {
        printf("Use Private Cert file %s failed\n", quic_cert);
        goto out;
    }

    ret = QuicDoHandshake(quic);
    if (ret < 0) {
        printf("Do Server Handshake failed\n");
        goto out;
    }

    if (memcmp(client_iv, quic->initial.decrypt.ciphers.pp_cipher.iv,
                sizeof(client_iv) - 1) != 0) {
        QuicPrint(quic->initial.encrypt.ciphers.pp_cipher.iv,
                sizeof(client_iv) - 1);
        printf("Client IV incorrect\n");
        goto out;
    }

    if (memcmp(server_iv, quic->initial.encrypt.ciphers.pp_cipher.iv,
                sizeof(server_iv) - 1) != 0) {
        printf("Server IV incorrect\n");
        goto out;
    }

    if (quic->initial.decrypt.pkt_num != 2) {
        printf("PKT number incorrect, %lu\n", quic->initial.decrypt.pkt_num);
        goto out;
    }

    if (quic->plain_buffer.data_len != sizeof(payload_plaintext)) {
        printf("Plaintext len incorrect\n");
        goto out;
    }

    if (memcmp(quic->plain_buffer.buf->data, payload_plaintext,
                sizeof(payload_plaintext)) != 0) {
        printf("Plaintext incorrect\n");
        goto out;
    }

    case_num = 1;
out:
    BIO_free(rbio);
    BIO_free(wbio);
    QuicFree(quic);
    QuicCtxFree(ctx);

    return case_num;
}

