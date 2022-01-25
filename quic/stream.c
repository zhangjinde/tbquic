/*
 * Remy Lewis(remyknight1119@gmail.com)
 */

#include "stream.h"

#include <assert.h>
#include <tbquic/quic.h>
#include "quic_local.h"
#include "tls.h"
#include "packet_local.h"
#include "frame.h"
#include "mem.h"
#include "common.h"
#include "log.h"

static uint64_t QuicStreamComputeMaxId(uint64_t num, bool uni)
{
    uint64_t base = num & QUIC_STREAM_ID_MASK;
    uint64_t top = num & ~QUIC_STREAM_ID_MASK;

    if (uni) {
        base |= QUIC_STREAM_UNIDIRECTIONAL;
    }

    return ((top << 1) | base | QUIC_STREAM_INITIATED_BY_SERVER);
}

static bool QuicStreamPeerOpened(int64_t id, bool server)
{
    return (server && !(id & QUIC_STREAM_INITIATED_BY_SERVER)) ||
                (!server && (id & QUIC_STREAM_INITIATED_BY_SERVER));
}

static void
QuicStreamInstanceInit(QuicStreamInstance *si, int64_t id, bool server)
{
    if (!QuicStreamPeerOpened(id, server) &&
            (id & QUIC_STREAM_UNIDIRECTIONAL)) {
        si->recv_state = QUIC_STREAM_STATE_DISABLE;
    } else {
        si->recv_state = QUIC_STREAM_STATE_START;
    }

    if (QuicStreamPeerOpened(id, server) &&
            (id & QUIC_STREAM_UNIDIRECTIONAL)) {
        si->send_state = QUIC_STREAM_STATE_DISABLE;
    } else {
        si->send_state = QUIC_STREAM_STATE_START;
    }
}

static void QuicStreamInstanceRecvOpen(QuicStreamInstance *si)
{
    if (si->recv_state == QUIC_STREAM_STATE_START) {
        si->recv_state = QUIC_STREAM_STATE_RECV;
    }
}

static int QuicStreamConfInit(QuicStreamConf *scf, uint64_t max_stream_bidi,
                        uint64_t max_stream_uni, bool server)
{
    int64_t id = 0;

    if (scf->stream != NULL) {
        QUIC_LOG("Stream initialized\n");
        return -1;
    }

    scf->max_bidi_stream_id = QuicStreamComputeMaxId(max_stream_bidi, false);
    scf->max_uni_stream_id = QuicStreamComputeMaxId(max_stream_uni, true);
    scf->max_id_value = QUIC_MAX(scf->max_bidi_stream_id, scf->max_uni_stream_id);
    scf->stream = QuicMemCalloc(sizeof(*scf->stream)*scf->max_id_value);
    if (scf->stream == NULL) {
        return -1;
    }

    for (id = 0; id < scf->max_id_value; id++) {
        QuicStreamInstanceInit(&scf->stream[id], id, server);
    }

    return 0;
}

void QuicStreamConfDeInit(QuicStreamConf *scf)
{
    QuicMemFree(scf->stream);
}

static int QuicStreamIdCheck(QuicStreamConf *scf, int64_t id)
{
    if (id & QUIC_STREAM_UNIDIRECTIONAL) {
        if (id >= scf->max_uni_stream_id) {
            //send a STREAMS_BLOCKED frame (type=0x17)
            return -1;
        }
    } else {
        if (id >= scf->max_bidi_stream_id) {
            //send a STREAMS_BLOCKED frame (type=0x16)
            return -1;
        }
    }

    if (id >= scf->max_id_value) {
        QUIC_LOG("id = %ld, max id = %lu\n", id, scf->max_id_value);
        return -1;
    }

    return 0;
}

static int64_t QuicStreamIdGen(QuicStreamConf *scf, bool server, bool uni)
{
    uint64_t *alloced = NULL;
    int64_t id = 0;

    if (uni) {
        id = scf->uni_id_alloced;
        id = (id << QUIC_STREAM_ID_MASK_BITS) | QUIC_STREAM_UNIDIRECTIONAL;
        alloced = &scf->uni_id_alloced;
    } else {
        id = scf->bidi_id_alloced;
        id = (id << QUIC_STREAM_ID_MASK_BITS);
        alloced = &scf->bidi_id_alloced;
    }

    if (server) {
        id |= QUIC_STREAM_INITIATED_BY_SERVER;
    }

    if (QuicStreamIdCheck(scf, id) < 0) {
        return -1;
    }

    (*alloced)++;
    return id;
}

QUIC_STREAM_HANDLE QuicStreamOpen(QUIC *quic, bool uni)
{
    QuicStreamConf *scf = &quic->stream;
    QuicStreamInstance *si = NULL;
    int64_t id = -1;

    if (scf->stream == NULL) {
        QUIC_LOG("Stream not initialized\n");
        return -1;
    }

    id = QuicStreamIdGen(scf, quic->quic_server, uni);
    if (id < 0) {
        return -1;
    }

    si = &scf->stream[id];

    assert(si->send_state == QUIC_STREAM_STATE_START);

    si->send_state = QUIC_STREAM_STATE_READY;
    if (!uni) {
        QuicStreamInstanceRecvOpen(si);
    }

    return id;
}

void QuicStreamClose(QUIC_STREAM_HANDLE h)
{
}

QuicStreamInstance *QuicStreamGetInstance(QUIC *quic, QUIC_STREAM_HANDLE h)
{
    QuicStreamConf *scf = &quic->stream;
    QuicStreamInstance *si = NULL;
    int64_t id = h;
    int64_t i = 0;

    if (scf->stream == NULL) {
        QUIC_LOG("Stream not initialized\n");
        return NULL;
    }

    if (QuicStreamIdCheck(scf, id) < 0) {
        return NULL;
    }

    si = &scf->stream[id];

    if (!QuicStreamPeerOpened(id, quic->quic_server) &&
            si->send_state == QUIC_STREAM_STATE_START) {
        return NULL;
    }

    if (id > scf->max_id_opened) {
        for (i = scf->max_id_opened + 1; i <= id; i++) {
            QuicStreamInstanceRecvOpen(&scf->stream[i]);
        }

        scf->max_id_opened = id;
    }

    return si;
}

int QuicStreamSendEarlyData(QUIC *quic, QUIC_STREAM_HANDLE *h, bool uni,
                                void *data, size_t len)
{
    TlsState handshake_state;
    int64_t id = 0;
    int ret = 0;

    ret = QuicDoHandshake(quic);
    handshake_state = quic->tls.handshake_state; 
    if (handshake_state == TLS_ST_SR_FINISHED ||
            handshake_state == TLS_ST_HANDSHAKE_DONE) {
            QUIC_LOG("Build Stream frame\n");
        id = QuicStreamOpen(quic, uni);
        if (id < 0) {
            QUIC_LOG("Open Stream failed\n");
            return -1;
        }

        *h = id;
        if (QuicStreamFrameBuild(quic, id, data, len) < 0) {
            QUIC_LOG("Build Stream frame failed\n");
            return -1;
        }

        if (QuicSendPacket(quic) < 0) {
            return -1;
        }

        return len;
    }

    return ret;
}

int QuicStreamSend(QUIC *quic, QUIC_STREAM_HANDLE h, void *data, size_t len)
{
    if (QuicStreamFrameBuild(quic, h, data, len) < 0) {
        QUIC_LOG("Build Stream frame failed\n");
        return -1;
    }

    if (QuicSendPacket(quic) < 0) {
        return -1;
    }

    return len;
}

int QuicStreamRecv(QUIC *quic, void *data, size_t len)
{
    RPacket pkt = {};
    QuicPacketFlags flags;
    QuicFlowReturn ret = QUIC_FLOW_RET_ERROR;
    uint32_t flag = 0;
    int rlen = 0;

    rlen = quic->method->read_bytes(quic, &pkt);
    if (rlen < 0) {
        return -1;
    }

    while (RPacketRemaining(&pkt)) {
        if (RPacketGet1(&pkt, &flag) < 0) {
            return -1;
        }

        flags.value = flag;
        ret = QuicPacketRead(quic, &pkt, flags);
        if (ret == QUIC_FLOW_RET_ERROR) {
            return -1;
        }

        RPacketUpdate(&pkt);
    }

    return 0;
}

int QuicStreamInit(QUIC *quic)
{
    QuicTransParams *param = &quic->negoed_param;

    return QuicStreamConfInit(&quic->stream, param->initial_max_stream_bidi,
                                param->initial_max_stream_uni,
                                quic->quic_server);
}

