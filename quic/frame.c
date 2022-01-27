/*
 * Remy Lewis(remyknight1119@gmail.com)
 */

#include "frame.h"

#include <assert.h>
#include <string.h>
#include <tbquic/stream.h>

#include "common.h"
#include "log.h"
#include "format.h"
#include "quic_local.h"
#include "q_buff.h"
#include "buffer.h"
#include "time.h"

#define QUIC_FRAM_IS_ACK_ELICITING(type) \
        (type != QUIC_FRAME_TYPE_PADDING && type != QUIC_FRAME_TYPE_ACK && \
                type != QUIC_FRAME_TYPE_CONNECTION_CLOSE)

static int QuicFramePingParser(QUIC *, RPacket *, uint64_t, QUIC_CRYPTO *,
                                    void *);
static int QuicFrameCryptoParser(QUIC *, RPacket *, uint64_t, QUIC_CRYPTO *,
                                    void *);
static int QuicFrameNewTokenParser(QUIC *, RPacket *, uint64_t, QUIC_CRYPTO *,
                                    void *);
static int QuicFrameAckParser(QUIC *, RPacket *, uint64_t, QUIC_CRYPTO *,
                                    void *);
static int QuicFrameResetStreamParser(QUIC *, RPacket *, uint64_t,
                                        QUIC_CRYPTO *, void *);
static int QuicFrameStopSendingParser(QUIC *, RPacket *, uint64_t,
                                        QUIC_CRYPTO *, void *);
static int QuicFrameNewConnIdParser(QUIC *, RPacket *, uint64_t, QUIC_CRYPTO *,
                                        void *);
static int QuicFrameHandshakeDoneParser(QUIC *, RPacket *, uint64_t,
                                        QUIC_CRYPTO *, void *);
static int QuicFrameStreamParser(QUIC *, RPacket *, uint64_t, QUIC_CRYPTO *,
                                        void *);
static int QuicFrameMaxStreamDataParser(QUIC *, RPacket *, uint64_t,
                                        QUIC_CRYPTO *, void *);
static int QuicFrameStreamDataBlockedParser(QUIC *, RPacket *, uint64_t,
                                        QUIC_CRYPTO *, void *);
static int QuicFrameAckBuild(QUIC *, WPacket *, QUIC_CRYPTO *, void *, long);
static int QuicFrameStreamDataBlockedBuild(QUIC *, WPacket *, QUIC_CRYPTO *,
                                        void *, long);

static QuicFrameProcess frame_handler[QUIC_FRAME_TYPE_MAX] = {
    [QUIC_FRAME_TYPE_PADDING] = {
        .flags = QUIC_FRAME_FLAGS_NO_BODY|QUIC_FRAME_FLAGS_SKIP,
    },
    [QUIC_FRAME_TYPE_PING] = {
        .flags = QUIC_FRAME_FLAGS_NO_BODY,
        .parser = QuicFramePingParser,
    },
    [QUIC_FRAME_TYPE_ACK] = {
        .parser = QuicFrameAckParser,
        .builder = QuicFrameAckBuild,
    },
    [QUIC_FRAME_TYPE_RESET_STREAM] = {
        .parser = QuicFrameResetStreamParser,
    },
    [QUIC_FRAME_TYPE_STOP_SENDING] = {
        .parser = QuicFrameStopSendingParser,
    },
    [QUIC_FRAME_TYPE_CRYPTO] = {
        .parser = QuicFrameCryptoParser,
    },
    [QUIC_FRAME_TYPE_NEW_TOKEN] = {
        .parser = QuicFrameNewTokenParser,
    },
    [QUIC_FRAME_TYPE_STREAM] = {
        .flags = QUIC_FRAME_FLAGS_SPLIT_ENABLE,
        .parser = QuicFrameStreamParser,
    },
    [QUIC_FRAME_TYPE_STREAM_FIN] = {
        .flags = QUIC_FRAME_FLAGS_SPLIT_ENABLE,
        .parser = QuicFrameStreamParser,
    },
    [QUIC_FRAME_TYPE_STREAM_LEN] = {
        .flags = QUIC_FRAME_FLAGS_SPLIT_ENABLE,
        .parser = QuicFrameStreamParser,
    },
    [QUIC_FRAME_TYPE_STREAM_LEN_FIN] = {
        .flags = QUIC_FRAME_FLAGS_SPLIT_ENABLE,
        .parser = QuicFrameStreamParser,
    },
    [QUIC_FRAME_TYPE_STREAM_OFF] = {
        .flags = QUIC_FRAME_FLAGS_SPLIT_ENABLE,
        .parser = QuicFrameStreamParser,
    },
    [QUIC_FRAME_TYPE_STREAM_OFF_FIN] = {
        .flags = QUIC_FRAME_FLAGS_SPLIT_ENABLE,
        .parser = QuicFrameStreamParser,
    },
    [QUIC_FRAME_TYPE_STREAM_OFF_LEN] = {
        .flags = QUIC_FRAME_FLAGS_SPLIT_ENABLE,
        .parser = QuicFrameStreamParser,
    },
    [QUIC_FRAME_TYPE_STREAM_OFF_LEN_FIN] = {
        .flags = QUIC_FRAME_FLAGS_SPLIT_ENABLE,
        .parser = QuicFrameStreamParser,
    },
    [QUIC_FRAME_TYPE_MAX_STREAM_DATA] = {
        .parser = QuicFrameMaxStreamDataParser,
    },
    [QUIC_FRAME_TYPE_STREAM_DATA_BLOCKED] = {
        .parser = QuicFrameStreamDataBlockedParser,
        .builder = QuicFrameStreamDataBlockedBuild,
    },
    [QUIC_FRAME_TYPE_NEW_CONNECTION_ID] = {
        .parser = QuicFrameNewConnIdParser,
    },
    [QUIC_FRAME_TYPE_HANDSHAKE_DONE] = {
        .flags = QUIC_FRAME_FLAGS_NO_BODY,
        .parser = QuicFrameHandshakeDoneParser,
    },
};

int QuicFrameDoParser(QUIC *quic, RPacket *pkt, QUIC_CRYPTO *c,
                        uint32_t pkt_type, void *buf)
{
    QuicFrameParser parser = NULL;
    QuicFlowReturn ret;
    uint64_t type = 0;
    uint64_t flags = 0;
    bool crypto_found = false;
    bool ack_eliciting = false;

    while (QuicVariableLengthDecode(pkt, &type) >= 0) {
        if (type >= QUIC_FRAME_TYPE_MAX) {
            QUIC_LOG("Unknown type(%lx)\n", type);
            return -1;
        }
        flags = frame_handler[type].flags;
        if (flags & QUIC_FRAME_FLAGS_SKIP) {
            continue;
        }

        parser = frame_handler[type].parser;
        if (parser == NULL) {
            QUIC_LOG("No parser for type(%lx)\n", type);
            return -1;
        }

        if (parser(quic, pkt, type, c, buf) < 0) {
            QUIC_LOG("Parse failed: type = %lx\n", type);
            return -1;
        }

        if (QUIC_FRAM_IS_ACK_ELICITING(type)) {
            ack_eliciting = true;
        }

        if (type == QUIC_FRAME_TYPE_CRYPTO) {
            crypto_found = true;
        }
    }

    if (ack_eliciting) {
        QuicAckFrameBuild(quic, pkt_type);
    }

    if (crypto_found) {
        if (quic->tls.handshake_state != TLS_ST_HANDSHAKE_DONE) {
            ret = TlsDoHandshake(&quic->tls);
            if (ret == QUIC_FLOW_RET_ERROR) {
                QUIC_LOG("TLS Hadshake failed!\n");
                return -1;
            }
        }

        if (quic->tls.handshake_state == TLS_ST_HANDSHAKE_DONE) {
            QUIC_LOG("TLS handshake done\n");
            quic->statem.state = QUIC_STATEM_HANDSHAKE_DONE;
        }
    }

    return 0;
}

static int
QuicFrameCryptoParser(QUIC *quic, RPacket *pkt, uint64_t type, QUIC_CRYPTO *c,
                        void *buffer)
{
    QUIC_BUFFER *buf = QUIC_TLS_BUFFER(quic);
    uint8_t *data = NULL;
    uint64_t offset = 0;
    uint64_t length = 0;
    uint64_t total_len = 0;

    if (QuicVariableLengthDecode(pkt, &offset) < 0) {
        QUIC_LOG("Offset decode failed!\n");
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &length) < 0) {
        QUIC_LOG("Length decode failed!\n");
        return -1;
    }

    total_len = offset + length;
    if (QUIC_GT(total_len, QuicBufLength(buf))) {
        if (QuicBufMemGrow(buf, total_len) == 0) {
            QUIC_LOG("Buffer grow failed!\n");
            return -1;
        }
    }

    data = QuicBufData(buf);
    if (data == NULL) {
        QUIC_LOG("Get buffer data failed!\n");
        return -1;
    }

    if (RPacketCopyBytes(pkt, &data[offset], length) < 0) {
        QUIC_LOG("Copy PKT data failed!\n");
        return -1;
    }

    if (QUIC_GT(total_len, QuicBufGetDataLength(buf))) {
        QuicBufSetDataLength(buf, total_len);
    }

    return 0;
}

static int
QuicFrameNewTokenParser(QUIC *quic, RPacket *pkt, uint64_t type, QUIC_CRYPTO *c,
                        void *buf)
{
    uint64_t length = 0;

    if (quic->quic_server) {
        //error PROTOCOL_VIOLATION
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &length) < 0) {
        QUIC_LOG("Length decode failed!\n");
        return -1;
    }

    return QuicDataParse(&quic->token, pkt, length);
}

static int
QuicFramePingParser(QUIC *quic, RPacket *pkt, uint64_t type, QUIC_CRYPTO *c,
                        void *buf)
{
    return 0;
}

static int QuicFrameResetStreamParser(QUIC *quic, RPacket *pkt, uint64_t type,
                                        QUIC_CRYPTO *c, void *buf)
{
    QuicStreamInstance *si = NULL;
    uint64_t stream_id = 0;
    uint64_t error_code = 0;
    uint64_t final_size = 0;
    uint8_t recv_state = 0;

    if (QuicVariableLengthDecode(pkt, &stream_id) < 0) {
        QUIC_LOG("Stream ID decode failed!\n");
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &error_code) < 0) {
        QUIC_LOG("Application error code decode failed!\n");
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &final_size) < 0) {
        QUIC_LOG("Final size decode failed!\n");
        return -1;
    }

    si = QuicStreamGetInstance(quic, stream_id);
    if (si == NULL) {
        QUIC_LOG("Instance not found for ID %lu\n", stream_id);
        return -1;
    }

    recv_state = si->recv_state;
    if (recv_state == QUIC_STREAM_STATE_RECV ||
            recv_state == QUIC_STREAM_STATE_SIZE_KNOWN ||
            recv_state == QUIC_STREAM_STATE_DATA_RECVD) {
        si->recv_state = QUIC_STREAM_STATE_RESET_RECVD;
    }

    return 0;
}

static int QuicFrameStopSendingParser(QUIC *quic, RPacket *pkt,
                                    uint64_t type, QUIC_CRYPTO *c,
                                    void *buf)
{
    QuicStreamInstance *si = NULL;
    uint64_t id = 0;
    uint64_t err_code = 0;

    if (QuicVariableLengthDecode(pkt, &id) < 0) {
        QUIC_LOG("ID decode failed!\n");
        return -1;
    }

    QUIC_LOG("Stream %lu\n", id);
    si = QuicStreamGetInstance(quic, id);
    if (si == NULL) {
        QUIC_LOG("Instance not found for ID %lu\n", id);
        return -1;
    }

    if (si->send_state == QUIC_STREAM_STATE_START ||
            si->send_state == QUIC_STREAM_STATE_DISABLE) {
        //STREAM_STATE_ERROR
        QUIC_LOG("Send stream disabled\n");
        return -1;
    }

    si->send_state = QUIC_STREAM_STATE_DISABLE;

    if (QuicVariableLengthDecode(pkt, &err_code) < 0) {
        QUIC_LOG("Max Stream Data decode failed!\n");
        return -1;
    }

    if (si->recv_state == QUIC_STREAM_STATE_START) {
        si->recv_state = QUIC_STREAM_STATE_RECV;
    }

    return 0;
}

static int
QuicFrameAckParser(QUIC *quic, RPacket *pkt, uint64_t type, QUIC_CRYPTO *c,
                        void *buf)
{
    uint64_t largest_acked = 0;
    uint64_t ack_delay = 0;
    uint64_t range_count = 0;
    uint64_t first_ack_range = 0;
    uint64_t gap = 0;
    uint64_t ack_range_len = 0;
    uint64_t i = 0;

    if (QuicVariableLengthDecode(pkt, &largest_acked) < 0) {
        QUIC_LOG("Largest acked decode failed!\n");
        return -1;
    }

    if (QUIC_GE(largest_acked, c->largest_acked)) {
        c->largest_acked = largest_acked;
    }

    if (QuicVariableLengthDecode(pkt, &ack_delay) < 0) {
        QUIC_LOG("Ack delay decode failed!\n");
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &range_count) < 0) {
        QUIC_LOG("Range count decode failed!\n");
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &first_ack_range) < 0) {
        QUIC_LOG("First ack range decode failed!\n");
        return -1;
    }

    for (i = 0; i < range_count; i++) {
        if (QuicVariableLengthDecode(pkt, &gap) < 0) {
            QUIC_LOG("Gap decode failed!\n");
            return -1;
        }

        if (QuicVariableLengthDecode(pkt, &ack_range_len) < 0) {
            QUIC_LOG("ACK range len decode failed!\n");
            return -1;
        }
    }

    QUIC_LOG("in\n");
    return 0;
}

static int
QuicFrameStreamParser(QUIC *quic, RPacket *pkt, uint64_t type, QUIC_CRYPTO *c,
                        void *buf)
{
    QuicStreamConf *scf = &quic->stream;
    QuicStreamInstance *si = NULL;
    QuicStreamData *sd = NULL;
    QuicStreamMsg *msg = NULL;
    const uint8_t *data = NULL;
    uint64_t id = 0;
    uint64_t offset = 0;
    uint64_t len = 0;

    if (QuicVariableLengthDecode(pkt, &id) < 0) {
        QUIC_LOG("ID decode failed!\n");
        return -1;
    }

    QUIC_LOG("Stream %lu\n", id);
    if (type & QUIC_FRAME_STREAM_BIT_OFF) {
        QUIC_LOG("Stream offset\n");
        if (QuicVariableLengthDecode(pkt, &offset) < 0) {
            QUIC_LOG("offset decode failed!\n");
            return -1;
        }
    }

    if (type & QUIC_FRAME_STREAM_BIT_LEN) {
        QUIC_LOG("Stream len\n");
        if (QuicVariableLengthDecode(pkt, &len) < 0) {
            QUIC_LOG("Length decode failed!\n");
            return -1;
        }
    } else {
        len = RPacketRemaining(pkt);
    }

    si = QuicStreamGetInstance(quic, id);
    if (si == NULL) {
        QUIC_LOG("Instance not found for ID %lu\n", id);
        return -1;
    }

    if (si->recv_state == QUIC_STREAM_STATE_DISABLE) {
        QUIC_LOG("Receive stream disabled\n");
        return -1;
    }

    if (si->send_state == QUIC_STREAM_STATE_START) {
        si->send_state = QUIC_STREAM_STATE_READY;
    }

    if (type & QUIC_FRAME_STREAM_BIT_FIN) {
        QUIC_LOG("Stream FIN\n");
        if (si->recv_state == QUIC_STREAM_STATE_RECV) {
            si->recv_state = QUIC_STREAM_STATE_SIZE_KNOWN;
        }
    }

    if (RPacketGetBytes(pkt, &data, len) < 0) {
        QUIC_LOG("Peek stream data failed!\n");
        return -1;
    }

    if (si->recv_state == QUIC_STREAM_STATE_START) {
        si->recv_state = QUIC_STREAM_STATE_RECV;
    }

    if (si->recv_state != QUIC_STREAM_STATE_RECV &&
            si->recv_state != QUIC_STREAM_STATE_SIZE_KNOWN) {
        return 0;
    }

    sd = QuicStreamDataCreate(buf, offset, data, len);
    if (sd == NULL) {
        return -1;
    }

    QuicStreamDataAdd(sd, si);

    if (!si->notified) {
        msg = QuicStreamMsgCreate(id, QUIC_STREAM_MSG_TYPE_DATA_RECVED);
        if (msg == NULL) {
            return 0;
        }
        QuicStreamMsgAdd(scf, msg);
        si->notified = 1;
    }

    return 0;
}

static int QuicFrameStreamDataBlockedParser(QUIC *quic, RPacket *pkt,
                                    uint64_t type, QUIC_CRYPTO *c,
                                    void *buf)
{
    QuicStreamInstance *si = NULL;
    uint64_t id = 0;
    uint64_t max_stream_data = 0;

    if (QuicVariableLengthDecode(pkt, &id) < 0) {
        QUIC_LOG("ID decode failed!\n");
        return -1;
    }

    QUIC_LOG("Stream %lu\n", id);
    si = QuicStreamGetInstance(quic, id);
    if (si == NULL) {
        QUIC_LOG("Instance not found for ID %lu\n", id);
        return -1;
    }

    if (si->recv_state == QUIC_STREAM_STATE_DISABLE) {
        QUIC_LOG("Receive stream disabled\n");
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &max_stream_data) < 0) {
        QUIC_LOG("Max Stream Data decode failed!\n");
        return -1;
    }

    if (si->recv_state == QUIC_STREAM_STATE_START) {
        si->recv_state = QUIC_STREAM_STATE_RECV;
    }

    return 0;
}

static int QuicFrameMaxStreamDataParser(QUIC *quic, RPacket *pkt,
                                    uint64_t type, QUIC_CRYPTO *c,
                                    void *buf)
{
    QuicStreamInstance *si = NULL;
    uint64_t id = 0;
    uint64_t max_stream_data = 0;

    if (QuicVariableLengthDecode(pkt, &id) < 0) {
        QUIC_LOG("ID decode failed!\n");
        return -1;
    }

    QUIC_LOG("Stream %lu\n", id);
    si = QuicStreamGetInstance(quic, id);
    if (si == NULL) {
        QUIC_LOG("Instance not found for ID %lu\n", id);
        return -1;
    }

    if (si->recv_state == QUIC_STREAM_STATE_DISABLE ||
            si->recv_state == QUIC_STREAM_STATE_START) {
        //STREAM_STATE_ERROR
        QUIC_LOG("Receive stream disabled\n");
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &max_stream_data) < 0) {
        QUIC_LOG("Max Stream Data decode failed!\n");
        return -1;
    }

    si->max_stream_data = max_stream_data;

    return 0;
}

static int QuicFrameNewConnIdParser(QUIC *quic, RPacket *pkt, uint64_t type,
                                        QUIC_CRYPTO *c, void *buf)
{
    QuicConn *conn = &quic->conn;
    uint64_t seq = 0;
    uint64_t retire_prior_to = 0;
    uint64_t len = 0;

    QUIC_LOG("in\n");

    if (QuicVariableLengthDecode(pkt, &seq) < 0) {
        QUIC_LOG("Seq decode failed!\n");
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &retire_prior_to) < 0) {
        QUIC_LOG("'Retire Prior To' decode failed!\n");
        return -1;
    }

    if (QuicVariableLengthDecode(pkt, &len) < 0) {
        QUIC_LOG("Length decode failed!\n");
        return -1;
    }

    if (QuicDataParse(&conn->id, pkt, len) < 0) {
        QUIC_LOG("Connection ID parse failed!\n");
        return -1;
    }

    return RPacketCopyBytes(pkt, conn->stateless_reset_token,
            sizeof(conn->stateless_reset_token));
}

static int QuicFrameHandshakeDoneParser(QUIC *quic, RPacket *pkt, uint64_t type,
                                        QUIC_CRYPTO *c, void *buf)
{
    QUIC_LOG("handshake done\n");
    if (!quic->quic_server) {
        quic->statem.state = QUIC_STATEM_HANDSHAKE_DONE;
    }

    return 0;
}

int QuicFramePaddingBuild(WPacket *pkt, size_t len)
{
    return WPacketMemset(pkt, 0, len);
}

static int QuicFrameCryptoBuild(QUIC *quic, WPacket *pkt, uint64_t offset,
                                uint8_t *data, size_t len)
{
    if (QuicVariableLengthWrite(pkt, QUIC_FRAME_TYPE_CRYPTO) < 0) {
        return -1;
    }

    if (QuicVariableLengthWrite(pkt, offset) < 0) {
        return -1;
    }

    return QuicWPacketSubMemcpyVar(pkt, data, len);
}

static int QuicFrameStreamDataBlockedBuild(QUIC *quic, WPacket *pkt,
                                        QUIC_CRYPTO *c,
                                        void *arg, long larg)
{
    return 0;
}

static int QuicFrameAckGen(QUIC *quic, WPacket *pkt, QUIC_CRYPTO *c)
{
    uint64_t largest_ack = c->largest_pn;
    uint64_t curr_time = 0;
    uint64_t delay = 0;
    uint64_t range_count = 0;

    if (QuicVariableLengthWrite(pkt, largest_ack) < 0) {
        return -1;
    }

    curr_time = QuicGetTimeUs();
    delay = curr_time - c->arriv_time;
    assert(QUIC_GE(delay, 0));

    if (QuicVariableLengthWrite(pkt, delay) < 0) {
        return -1;
    }

    if (QuicVariableLengthWrite(pkt, range_count) < 0) {
        return -1;
    }

    if (QuicVariableLengthWrite(pkt, c->first_ack_range) < 0) {
        return -1;
    }

    c->largest_ack = largest_ack;

    return 0;
}

static int QuicFrameAckBuild(QUIC *quic, WPacket *pkt, QUIC_CRYPTO *c,
                            void *arg, long larg)
{
    return QuicFrameAckGen(quic, pkt, c);
}

int QuicFrameAckSendCheck(QUIC_CRYPTO *c)
{
    if (!c->encrypt.cipher_inited) {
        return -1;
    }

    if (c->largest_ack == c->largest_pn) {
        return -1;
    }

    return 0;
}

int QuicFrameStreamBuild(QUIC *quic, WPacket *pkt, uint64_t id,
                            uint8_t *data, size_t len, bool fin,
                            bool last)
{
    QuicStreamInstance *si = NULL;
    uint64_t type = QUIC_FRAME_TYPE_STREAM;
    uint64_t offset = 0;
    size_t space = 0;
    int data_len = 0;

    si = QuicStreamGetInstance(quic, id);
    if (si == NULL) {
        return -1;
    }

    if (si->send_state == QUIC_STREAM_STATE_DISABLE) {
        return -1;
    }

    if (si->send_state == QUIC_STREAM_STATE_START) {
        si->send_state = QUIC_STREAM_STATE_READY;
    }

    offset = si->sent_byptes;
    if (offset) {
        type |= QUIC_FRAME_STREAM_BIT_OFF;
    }

    if (fin) {
        type |= QUIC_FRAME_STREAM_BIT_FIN;
        if (si->send_state == QUIC_STREAM_STATE_SEND) {
            si->send_state = QUIC_STREAM_STATE_DATA_SENT;
        }
    }

    if (!last) {
        type |= QUIC_FRAME_STREAM_BIT_LEN;
    }

    if (QuicVariableLengthWrite(pkt, type) < 0) {
        return -1;
    }

    if (QuicVariableLengthWrite(pkt, id) < 0) {
        return -1;
    }

    if (type & QUIC_FRAME_STREAM_BIT_OFF) {
        if (QuicVariableLengthWrite(pkt, offset) < 0) {
            return -1;
        }
    }

    if (type & QUIC_FRAME_STREAM_BIT_LEN) {
        return QuicWPacketSubMemcpyVar(pkt, data, len);
    }

    space = WPacket_get_space(pkt);
    data_len = QUIC_MIN(space, len);
    if (WPacketMemcpy(pkt, data, data_len) < 0) {
        return -1;
    }

    return data_len;
}

static int QuicFrameAddQueue(QUIC *quic, WPacket *pkt, QBUFF *qb)
{
    size_t written = 0;

    written = WPacket_get_written(pkt);
    if (written == 0) {
        return -1;
    }

    if (QBuffSetDataLen(qb, written) < 0) {
        return -1;
    }

    QuicAddQueue(quic, qb);
    WPacketCleanup(pkt);
    return 0;
}
 
static QBUFF *
QuicFrameBufferNew(uint32_t pkt_type, size_t buf_len, WPacket *pkt)
{
    QBUFF *qb = NULL;

    qb = QBuffNew(pkt_type, buf_len);
    if (qb == NULL) {
        return NULL;
    }

    WPacketStaticBufInit(pkt, QBuffHead(qb), QBuffLen(qb));
    return qb;
}


static QBUFF *QuicBuffQueueAndNext(QUIC *quic, uint32_t pkt_type, WPacket *pkt,
                                    QBUFF *qb, size_t buf_len, size_t max_hlen)
{
    if (QUIC_GT(WPacket_get_space(pkt), max_hlen)) {
        return qb;
    }

    if (QuicFrameAddQueue(quic, pkt, qb) < 0) {
        QUIC_LOG("Add frame queue failed\n");
        return NULL;
    }

    WPacketCleanup(pkt);
    return QuicFrameBufferNew(pkt_type, buf_len, pkt);
}

static QBUFF *QuicFrameCryptoSplit(QUIC *quic, uint32_t pkt_type, WPacket *pkt,
                QBUFF *qb, uint8_t *data, size_t len)
{
    uint64_t offset = 0;
    size_t buf_len = 0;
    int wlen = 0;

    buf_len = QBuffLen(qb);
    offset = 0;
    while (QUIC_LT(offset, len)) {
        qb = QuicBuffQueueAndNext(quic, pkt_type, pkt, qb, buf_len,
                                QUIC_FRAME_CRYPTO_HEADER_MAX_LEN);
        if (qb == NULL) {
            return NULL;
        }

        wlen = QuicFrameCryptoBuild(quic, pkt, offset, &data[offset],
                                        len - offset);
        if (wlen <= 0) {
            QUIC_LOG("Build failed\n");
            goto err;
        }

        offset += wlen;
    }

    WPacketCleanup(pkt);

    return qb;
err:
    QBuffFree(qb);
    return NULL;
}

static size_t QuicFrameGetBuffLen(QUIC *quic, uint32_t pkt_type)
{
    size_t buf_len = 0;
    size_t head_tail_len = 0;
    uint32_t mss = 0;

    mss = quic->mss;

    head_tail_len = QBufPktComputeTotalLenByType(quic, pkt_type, mss) - mss;
    buf_len = mss - head_tail_len;
    assert(QUIC_GT(buf_len, 0));

    return buf_len;
}

int
QuicFrameBuild(QUIC *quic, uint32_t pkt_type, QuicFrameNode *node, size_t num)
{
    QuicFrameBuilder builder = NULL;
    QUIC_CRYPTO *c = NULL;
    QuicFrameNode *n = NULL;
    QBUFF *qb = NULL;
    WPacket pkt = {};
    size_t i = 0;
    size_t buf_len = 0;
    uint64_t type = 0;
    uint64_t flags = 0;
    int ret = -1;

    buf_len = QuicFrameGetBuffLen(quic, pkt_type);
    qb = QuicFrameBufferNew(pkt_type, buf_len, &pkt);
    if (qb == NULL) {
        goto out;
    }

    for (i = 0; i < num; i++) {
        n = &node[i];
        type = n->type;
        if (type >= QUIC_FRAME_TYPE_MAX) {
            QUIC_LOG("Unknown type(%lx)", type);
            continue;
        }
        
        if (QuicVariableLengthWrite(&pkt, type) < 0) {
            goto out;
        }

        flags = frame_handler[type].flags;
        if (flags & QUIC_FRAME_FLAGS_NO_BODY) {
            continue;
        }

        c = QuicCryptoGet(quic, pkt_type);
        builder = frame_handler[type].builder;
        assert(builder != NULL);
        if (builder(quic, &pkt, c, node->arg, node->larg) < 0) {
            QUIC_LOG("Build %lu failed\n", type);
            goto out;
        }
    }

    if (WPacket_get_written(&pkt)) {
        if (QuicFrameAddQueue(quic, &pkt, qb) < 0) {
            QUIC_LOG("Add frame queue failed\n");
            goto out;
        }
        qb = NULL;
    }

    ret = 0;
out:
    WPacketCleanup(&pkt);
    QBuffFree(qb);
    return ret;
}

static int QuicFrameStreamWrite(QUIC *quic, uint32_t pkt_type, WPacket *pkt,
                                int64_t id, uint8_t *data, size_t len,
                                QBUFF **qb, size_t buf_len, bool fin, bool last)
{
    QBUFF *nqb = *qb;
    uint64_t offset = 0;
    int wlen = 0;

    while (QUIC_LT(offset, len)) {
        nqb = QuicBuffQueueAndNext(quic, pkt_type, pkt, nqb, buf_len,
                QUIC_FRAME_STREAM_HEADER_MAX_LEN);
        if (nqb == NULL) {
            return -1;
        }

        *qb = nqb;

        wlen = QuicFrameStreamBuild(quic, pkt, id, &data[offset], len - offset,
                                    fin, last);
        if (wlen < 0) {
            return -1;
        }

        offset += wlen;
    }

    return 0;
}

int QuicStreamFrameBuild(QUIC *quic, QUIC_STREAM_IOVEC *iov, size_t num)
{
    QUIC_STREAM_IOVEC *v = NULL;
    QUIC_CRYPTO *c = NULL;
    QBUFF *qb = NULL;
    WPacket pkt = {};
    size_t i = 0;
    size_t buf_len = 0;
    uint64_t flags = 0;
    uint32_t pkt_type = QUIC_PKT_TYPE_1RTT;
    int ret = -1;

    buf_len = QuicFrameGetBuffLen(quic, pkt_type);
    qb = QuicFrameBufferNew(pkt_type, buf_len, &pkt);
    if (qb == NULL) {
        goto out;
    }

    c = QuicCryptoGet(quic, pkt_type);
    if (QuicFrameAckSendCheck(c) == 0) {
        if (QuicVariableLengthWrite(&pkt, QUIC_FRAME_TYPE_ACK) < 0) {
            return -1;
        }

        if (QuicFrameAckGen(quic, &pkt, c) < 0) {
            goto out;
        }
    }

    for (i = 0; i < num; i++) {
        v = &iov[i];
        if (QuicFrameStreamWrite(quic, pkt_type, &pkt, v->handle,
                    v->iov_base, v->data_len, &qb, buf_len,
                    flags & QUIC_STREAM_DATA_FLAGS_FIN,
                    i < num - 1) < 0) {
            goto out;
        }
   }

    if (WPacket_get_written(&pkt)) {
        if (QuicFrameAddQueue(quic, &pkt, qb) < 0) {
            QUIC_LOG("Add frame queue failed\n");
            goto out;
        }
        qb = NULL;
    }

    ret = 0;
out:
    WPacketCleanup(&pkt);
    QBuffFree(qb);
    return ret;
}

int QuicCryptoFrameBuild(QUIC *quic, uint32_t pkt_type)
{
    QUIC_BUFFER *buf = QUIC_TLS_BUFFER(quic);
    QBUFF *qb = NULL;
    uint8_t *data = NULL;
    WPacket pkt = {};
    size_t buf_len = 0;
    size_t len = 0;
    int ret = -1;

    data = QUIC_BUFFER_HEAD(buf);
    len = QuicBufGetDataLength(buf);

    buf_len = QuicFrameGetBuffLen(quic, pkt_type);
    qb = QuicFrameBufferNew(pkt_type, buf_len, &pkt);
    if (qb == NULL) {
        goto out;
    }

    qb = QuicFrameCryptoSplit(quic, pkt_type, &pkt, qb, data, len);
    if (qb == NULL) {
        goto out;
    }

    if (WPacket_get_written(&pkt)) {
        if (QuicFrameAddQueue(quic, &pkt, qb) < 0) {
            QUIC_LOG("Add frame queue failed\n");
            goto out;
        }
        qb = NULL;
    }

    ret = 0;
out:
    WPacketCleanup(&pkt);
    QBuffFree(qb);
    return ret;
}

int QuicAckFrameBuild(QUIC *quic, uint32_t pkt_type)
{
    QUIC_CRYPTO *c = NULL;
    QuicFrameNode frame = {};

    c = QuicCryptoGet(quic, pkt_type);
    if (c == NULL) {
        return -1;
    }

    if (QuicFrameAckSendCheck(c) < 0) {
        return -1;
    }

    frame.type = QUIC_FRAME_TYPE_ACK;

    return QuicFrameBuild(quic, pkt_type, &frame, 1);
}

