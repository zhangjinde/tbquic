/*
 * Remy Lewis(remyknight1119@gmail.com)
 */

#include "cipher.h"

#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <tbquic/cipher.h>

#include "quic_local.h"
#include "crypto.h"
#include "common.h"
#include "evp.h"

typedef struct {
    const uint8_t *salt;
    size_t salt_len;
    uint32_t version;
} QuicSalt;

static const int algorithm_cipher_nid[QUIC_ALG_MAX] = {
    [QUIC_ALG_AES_128_ECB] = NID_aes_128_ecb,
    [QUIC_ALG_AES_192_ECB] = NID_aes_192_ecb,
    [QUIC_ALG_AES_256_ECB] = NID_aes_256_ecb,
    [QUIC_ALG_AES_128_GCM] = NID_aes_128_gcm,
    [QUIC_ALG_AES_192_GCM] = NID_aes_192_gcm,
    [QUIC_ALG_AES_256_GCM] = NID_aes_256_gcm,
    [QUIC_ALG_AES_128_CCM] = NID_aes_128_ccm,
    [QUIC_ALG_AES_192_CCM] = NID_aes_192_ccm,
    [QUIC_ALG_AES_256_CCM] = NID_aes_256_ccm,
    [QUIC_ALG_CHACHA20] = NID_chacha20,
};

static const uint8_t handshake_salt_v1[] =
    "\x38\x76\x2C\xF7\xF5\x59\x34\xB3\x4D\x17"
    "\x9A\xE6\xA4\xC8\x0C\xAD\xCC\xBB\x7F\x0A";

static const QuicSalt handshake_salt[] = {
    {
        .salt = handshake_salt_v1,
        .salt_len = sizeof(handshake_salt_v1) - 1,
        .version = QUIC_VERSION_1,
    },
};

#define QUIC_HANDSHAKE_SALT_NUM QUIC_ARRAY_SIZE(handshake_salt)


static const QuicSalt *QuicSaltFind(const QuicSalt *salt, size_t num,
                                    uint32_t version)
{
    int i = 0;

    for (i = 0; i < num; i++) {
        if (salt[i].version == version) {
            return &salt[i];
        }
    }

    return NULL;
}

int QuicCipherNidFind(uint32_t alg)
{
    if (alg >= QUIC_ALG_MAX) {
        return -1;
    }

    return algorithm_cipher_nid[alg];
}

/*
 * Compute the initial secrets given Connection ID "cid".
 */
static int QuicDeriveInitialSecrets(const QUIC_DATA *cid, uint8_t *client_secret,
        uint8_t *server_secret, uint32_t version)
{
    const QuicSalt *salt = NULL;
    uint8_t secret[HASH_SHA2_256_LENGTH];
    static const uint8_t client_label[] = "client in";
    static const uint8_t server_label[] = "server in";
    size_t secret_len = 0;

    salt = QuicSaltFind(handshake_salt, QUIC_HANDSHAKE_SALT_NUM, version);
    if (salt == NULL) {
        return -1;
    }

    if (HkdfExtract(EVP_sha256(), salt->salt, salt->salt_len, cid->data,
                    cid->len, secret, &secret_len) == NULL) {
        return -1;
    }

    if (QuicTLS13HkdfExpandLabel(EVP_sha256(), secret, sizeof(secret),
                        client_label, sizeof(client_label) - 1,
                        client_secret, HASH_SHA2_256_LENGTH) < 0) {
        return -1;
    }

    if (QuicTLS13HkdfExpandLabel(EVP_sha256(), secret, sizeof(secret),
                        server_label, sizeof(server_label) - 1,
                        server_secret, HASH_SHA2_256_LENGTH) < 0) {
        return -1;
    }

    return 0;
}

static int QuicCipherDoPrepare(QUIC_CIPHER *cipher, const EVP_CIPHER *c,
                                uint8_t *secret, const uint8_t *key,
                                const uint8_t *iv, int enc)
{
    cipher->ctx = EVP_CIPHER_CTX_new();
    if (cipher->ctx == NULL) {
        return -1;
    }

    if (secret == NULL) {
        return 0;
    }

    return QuicEvpCipherInit(cipher->ctx, c, key, iv, enc);
}

static int QuicHPCipherPrepare(QuicHPCipher *cipher, const EVP_MD *md,
                                uint8_t *secret)
{
    const EVP_CIPHER *c = NULL;
    uint8_t key[AES_KEY_MAX_SIZE] = {};
    static const uint8_t quic_hp_label[] = "quic hp";
    int key_len = 0;

    c = EVP_get_cipherbynid(cipher->cipher.cipher_nid);
    if (c == NULL) {
        return -1;
    }

    key_len = EVP_CIPHER_key_length(c);
    if (key_len > sizeof(key)) {
        return -1;
    }

    if (secret != NULL) {
        if (QuicTLS13HkdfExpandLabel(md, secret, EVP_MD_size(md), quic_hp_label,
                    sizeof(quic_hp_label) - 1, key, key_len) < 0) {
            return -1;
        }
    }

    return QuicCipherDoPrepare(&cipher->cipher, c, secret, key, NULL,
                                QUIC_EVP_ENCRYPT);
}

static int QuicPPCipherPrepare(QuicPPCipher *cipher, const EVP_MD *md,
                                uint8_t *secret, int enc)
{
    const EVP_CIPHER *c = NULL;
    uint8_t key[AES_KEY_MAX_SIZE] = {};
    static const uint8_t quic_key_label[] = "quic key";
    static const uint8_t quic_iv_label[] = "quic iv";
    int key_len = 0;

    c = EVP_get_cipherbynid(cipher->cipher.cipher_nid);
    if (c == NULL) {
        return -1;
    }

    key_len = EVP_CIPHER_key_length(c);
    if (key_len > sizeof(key)) {
        return -1;
    }

    if (secret != NULL) {
        if (QuicTLS13HkdfExpandLabel(md, secret, EVP_MD_size(md),
                    quic_key_label, sizeof(quic_key_label) - 1,
                    key, key_len) < 0) {
            return -1;
        }

        if (QuicTLS13HkdfExpandLabel(md, secret, EVP_MD_size(md), quic_iv_label,
                    sizeof(quic_iv_label) - 1, cipher->iv, sizeof(cipher->iv))
                < 0) {
            return -1;
        }
    }

    return QuicCipherDoPrepare(&cipher->cipher, c, secret, key, cipher->iv, enc);
}

int QuicCiphersPrepare(QUIC_CIPHERS *ciphers, const EVP_MD *md,
                        uint8_t *secret, int enc)
{
    if (QuicHPCipherPrepare(&ciphers->hp_cipher, md, secret)
            < 0) {
        return 1;
    }

    return QuicPPCipherPrepare(&ciphers->pp_cipher, md, secret, enc);
}

int QuicCreateInitialDecoders(QUIC *quic, uint32_t version)
{
    uint8_t client_secret[HASH_SHA2_256_LENGTH];
    uint8_t server_secret[HASH_SHA2_256_LENGTH];
    
    if (QuicDeriveInitialSecrets(&quic->peer_dcid, client_secret, server_secret,
                version) < 0) {
        return -1;
    }

    /* 
     * Packet numbers are protected with AES128-CTR,
     * initial packets are protected with AEAD_AES_128_GCM.
     */
    if (QuicCiphersPrepare(&quic->initial.client.ciphers, EVP_sha256(),
                client_secret, QUIC_EVP_DECRYPT) < 0) {
        return -1;
    }

    if (QuicCiphersPrepare(&quic->initial.server.ciphers, EVP_sha256(),
                server_secret, QUIC_EVP_ENCRYPT) < 0) {
        return -1;
    }

    return 0;
}

int QuicDoCipher(QUIC_CIPHER *cipher, uint8_t *out, size_t *outl,
                        const uint8_t *in, size_t inl)
{
    size_t len = 0;

    if (QuicEvpCipherUpdate(cipher->ctx, out, outl, in, inl) < 0) {
        return -1;
    }

    if (QuicEvpCipherFinal(cipher->ctx, &out[*outl], &len) < 0) {
        return -1;
    }

    *outl += len;

    return 0;
}

void QuicCipherCtxFree(QUIC_CIPHERS *ciphers)
{
    EVP_CIPHER_CTX_free(ciphers->hp_cipher.cipher.ctx);
    EVP_CIPHER_CTX_free(ciphers->pp_cipher.cipher.ctx);
}
