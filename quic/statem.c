/*
 * Remy Lewis(remyknight1119@gmail.com)
 */

#include "statem.h"

#include <assert.h>
#include <openssl/err.h>

#include "quic_local.h"
#include "format.h"
#include "rand.h"
#include "datagram.h"
#include "q_buff.h"
#include "mem.h"
#include "common.h"
#include "log.h"

int QuicStatemReadBytes(QUIC *quic, RPacket *pkt)
{
    QUIC_DATA *buf = quic->read_buf;
    int rlen = 0;

    assert(buf != NULL);
    rlen = QuicDatagramRecv(quic, buf->data, buf->len);
    if (rlen < 0) {
        return -1;
    }

    RPacketBufInit(pkt, buf->data, rlen);
    return 0;
}

static int
QuicReadStateMachine(QUIC *quic, const QuicStatemFlow *statem, size_t num)
{
    const QuicStatemFlow *sm = NULL;
    QUIC_STATEM *st = &quic->statem;
    RPacket pkt = {};
    QuicPacketFlags flags;
    QuicFlowReturn ret = QUIC_FLOW_RET_ERROR;
    int rlen = 0;

    st->read_state = QUIC_WANT_DATA;
    while (ret != QUIC_FLOW_RET_FINISH || RPacketRemaining(&pkt)) {
        if (st->read_state == QUIC_WANT_DATA && !RPacketRemaining(&pkt)) {
            rlen = quic->method->read_bytes(quic, &pkt);
            if (rlen < 0) {
                return -1;
            }

            st->read_state = QUIC_DATA_READY;
        } else {
            RPacketUpdate(&pkt);
        }

        assert(st->state >= 0 && st->state < num);
        sm = &statem[st->state];

        if (QuicGetPktFlags(&flags, &pkt) < 0) {
            return -1;
        }

        ret = sm->recv(quic, &pkt, flags);
        switch (ret) {
            case QUIC_FLOW_RET_WANT_READ:
                st->read_state = QUIC_WANT_DATA;
                continue;
            case QUIC_FLOW_RET_FINISH:
                break;
            default:
                return -1;
        }
    }

    return 0;
}

int
QuicStateMachineAct(QUIC *quic, const QuicStatemFlow *statem, size_t num)
{
    const QuicStatemFlow *sm = NULL;
    QUIC_STATEM *st = &quic->statem;
    int ret = 0;

    do {
        sm = &statem[st->state];
        if (sm->pre_work != NULL) {
            ret = sm->pre_work(quic);
        }

        if (QuicSendPacket(quic) < 0) {
            return -1;
        }

        if (QuicWantWrite(quic)) {
            return -1;
        }

        if (ret < 0) {
            return -1;
        }

        ret = QuicReadStateMachine(quic, statem, num);
        if (QuicSendPacket(quic) < 0) {
            return -1;
        }

        if (QuicWantWrite(quic)) {
            return -1;
        }

        if (ret < 0) {
            return -1;
        }
    } while (st->state != QUIC_STATEM_HANDSHAKE_DONE);

    return 0;
}

static QuicFlowReturn
QuicHandshakeRead(QUIC *quic, const QuicStatemMachine *sm)
{
    return QUIC_FLOW_RET_FINISH;
}

static QuicFlowReturn
QuicHandshakeWrite(QUIC *quic, const QuicStatemMachine *sm)
{
    return QUIC_FLOW_RET_FINISH;
}

int
QuicDoStateMachine(QUIC *quic, const QuicStatemMachine *statem, size_t num)
{
    QUIC_STATEM *st = &quic->statem;
    const QuicStatemMachine *sm = NULL;
    QuicStatem state = QUIC_STATEM_INITIAL;
    QuicFlowReturn ret = QUIC_FLOW_RET_ERROR;

    while (st->state != QUIC_STATEM_HANDSHAKE_DONE) {
        state = st->state;
        sm = &statem[state];
        switch (sm->rw_state) {
            case QUIC_NOTHING:
                ret = QUIC_FLOW_RET_FINISH;
                break;
            case QUIC_READING:
                ret = QuicHandshakeRead(quic, sm);
                break;
            case QUIC_WRITING:
                ret = QuicHandshakeWrite(quic, sm);
                break;
            default:
                QUIC_LOG("Unknown state(%d)\n", sm->rw_state);
                return -1;
        }

        if (ret == QUIC_FLOW_RET_STOP) {
            st->rwstate = sm->rw_state;
            return -1;
        }

        if (ret == QUIC_FLOW_RET_ERROR) {
            goto err;
        }

        if (sm->post_work != NULL && sm->post_work(quic) < 0) {
            goto err;
        }

        if (state == st->state) {
            st->state = sm->next_state;
        }
        assert(st->state >= 0 && st->state < num);
    }

    return 0;
err:
    st->rwstate = QUIC_NOTHING;
    return -1;
}

static int
QuicLongPktParse(QUIC *quic, RPacket *pkt, QuicPacketFlags flags, uint8_t type,
                    QUIC_DATA *new_dcid, bool *update_dcid)
{
    if (!QUIC_PACKET_IS_LONG_PACKET(flags)) {
        QUIC_LOG("Not Long packet\n");
        return -1;
    }

    if (flags.lh.lpacket_type != type) {
        QUIC_LOG("Type not match\n");
        return -1;
    }

    if (QuicLPacketHeaderParse(quic, pkt, new_dcid, update_dcid) < 0) {
        QUIC_LOG("Header Parse failed\n");
        return -1;
    }

    return 0;
}

QuicFlowReturn
QuicInitialRecv(QUIC *quic, RPacket *pkt, QuicPacketFlags flags)
{
    QUIC_DATA new_dcid = {};
    bool update_dcid = false;

    if (QuicLongPktParse(quic, pkt, flags, QUIC_LPACKET_TYPE_INITIAL,
                &new_dcid, &update_dcid) < 0) {
        return QUIC_FLOW_RET_ERROR;
    }

    if (QuicInitPacketParse(quic, pkt, &quic->initial) < 0) {
        return QUIC_FLOW_RET_ERROR;
    }

    if (update_dcid && QuicUpdateDcid(quic, &new_dcid,
                QUIC_LPACKET_TYPE_INITIAL) < 0) {
        QUIC_LOG("Update DCID failed\n");
        return QUIC_FLOW_RET_ERROR;
    }

    return QUIC_FLOW_RET_FINISH;
}

int QuicInitialSend(QUIC *quic)
{
    QuicFlowReturn ret = QUIC_FLOW_RET_FINISH; 

    ret = TlsDoHandshake(&quic->tls);
    if (ret == QUIC_FLOW_RET_ERROR) {
        QUIC_LOG("TLS handshake failed\n");
        return -1;
    }

    return 0;
}

QuicFlowReturn QuicPacketRead(QUIC *quic, RPacket *pkt, QuicPacketFlags flags)
{
    const uint8_t *stateless_reset_token = NULL;
    QUIC_DATA new_dcid = {};
    bool update_dcid = false;
    uint64_t offset = 0;
    uint32_t type = 0;

    offset = RPacketRemaining(pkt) - QUIC_STATELESS_RESET_TOKEN_LEN;
    if (QUIC_GT(offset, 0)) {
        stateless_reset_token = RPacketData(pkt) + offset;
    }

    if (QuicPktHeaderParse(quic, pkt, flags, &type, &new_dcid,
                            &update_dcid) < 0) {
        QUIC_LOG("Header parse failed\n");
        goto err;
    }

    if (QuicPktBodyParse(quic, pkt, type) < 0) {
        QUIC_LOG("Body parse failed\n");
        goto err;
    }

    if (update_dcid && QuicUpdateDcid(quic, &new_dcid, type) < 0) {
        QUIC_LOG("Update DCID failed\n");
        return QUIC_FLOW_RET_ERROR;
    }

    return QUIC_FLOW_RET_FINISH;
err:

    QuicCheckStatelessResetToken(quic, stateless_reset_token);
    return QUIC_FLOW_RET_ERROR;
}

QuicFlowReturn
QuicPacketClosingRecv(QUIC *quic, RPacket *pkt, QuicPacketFlags flags)
{
    return QUIC_FLOW_RET_FINISH;
}

QuicFlowReturn
QuicPacketDrainingRecv(QUIC *quic, RPacket *pkt, QuicPacketFlags flags)
{
    return QUIC_FLOW_RET_FINISH;
}

