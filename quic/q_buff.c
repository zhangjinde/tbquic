/*
 * Remy Lewis(remyknight1119@gmail.com)
 */

#include "q_buff.h"

#include <assert.h>

#include "mem.h"
#include "format.h"
#include "common.h"

static const QBuffPktMethod QuicBuffPktMethod[QUIC_PKT_TYPE_MAX] = {
    [QUIC_PKT_TYPE_INITIAL] = {
        .build_pkt = QuicInitialPacketBuild,
        .get_totallen = QuicInitialPacketGetTotalLen,
    },
    [QUIC_PKT_TYPE_HANDSHAKE] = {
        .build_pkt = QuicHandshakePacketBuild,
        .get_totallen = QuicHandshakePacketGetTotalLen,
    },
};

void QBuffQueueHeadInit(QBuffQueueHead *h)
{
    INIT_LIST_HEAD(&h->queue);
}

QBUFF *QBuffNew(size_t len, uint32_t pkt_type)
{
    QBUFF *qb = NULL;

    if (QUIC_GE(pkt_type, QUIC_PKT_TYPE_MAX)) {
        return NULL;
    }

    qb = QuicMemCalloc(sizeof(*qb));
    if (qb == NULL) {
        return NULL;
    }

    qb->buff = QuicMemMalloc(len);
    if (qb->buff == NULL) {
        QBuffFree(qb);
        return NULL;
    }

    qb->buff_len = len;
    qb->method = &QuicBuffPktMethod[pkt_type];

    return qb;
}

void QBuffFree(QBUFF *qb)
{
    QuicMemFree(qb->buff);
    QuicMemFree(qb);
}

void *QBuffHead(QBUFF *qb)
{
    return qb->buff;
}

void *QBuffTail(QBUFF *qb)
{
    return (uint8_t *)qb->buff + qb->data_len;
}

size_t QBuffLen(QBUFF *qb)
{
    return qb->buff_len;
}

size_t QBuffGetDataLen(QBUFF *qb)
{
    return qb->data_len;
}

size_t QBuffSpace(QBUFF *qb)
{
    assert(QUIC_GE(qb->buff_len, qb->data_len));

    return qb->buff_len - qb->data_len;
}

int QBuffSetDataLen(QBUFF *qb, size_t len)
{
    if (QUIC_LT(qb->buff_len, len)) {
        return -1;
    }

    qb->data_len = len;

    return 0;
}

int QBuffAddDataLen(QBUFF *qb, size_t len)
{
    return QBuffSetDataLen(qb, qb->data_len + len);
}

int QBuffBuildPkt(QUIC *quic, WPacket *pkt, QBUFF *qb, bool last)
{
    return qb->method->build_pkt(quic, pkt, qb, last);
}

void QBuffQueueAdd(QBuffQueueHead *h, QBUFF *qb)
{
    list_add_tail(&qb->node, &h->queue);
}

void QBuffQueueDestroy(QBuffQueueHead *h)
{
    QBUFF *qb = NULL;
    QBUFF *n = NULL;

    list_for_each_entry_safe(qb, n, &h->queue, node) {
        list_del(&qb->node);
        QBuffFree(qb);
    }
}

