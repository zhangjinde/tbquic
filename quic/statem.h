#ifndef TBQUIC_QUIC_STATEM_H_
#define TBQUIC_QUIC_STATEM_H_

#include <stddef.h>
#include <tbquic/types.h>

#include "base.h"
#include "types.h"
#include "packet_local.h"
#include "format.h"

#define QUIC_FLOW_STATEM_NOTHING(s) (s == QUIC_FLOW_NOTHING)
#define QUIC_FLOW_STATEM_READING(s) (s == QUIC_FLOW_READING)
#define QUIC_FLOW_STATEM_WRITING(s) (s == QUIC_FLOW_WRITING)
#define QUIC_FLOW_STATEM_FINISHED(s) (s == QUIC_FLOW_FINISHED)

typedef enum {
    QUIC_FLOW_RET_ERROR,
    QUIC_FLOW_RET_STOP,
    QUIC_FLOW_RET_NEXT,
    QUIC_FLOW_RET_WANT_READ,
    QUIC_FLOW_RET_WANT_WRITE,
    QUIC_FLOW_RET_CONTINUE,
    QUIC_FLOW_RET_FINISH,
    QUIC_FLOW_RET_DROP,
    QUIC_FLOW_RET_END,
} QuicFlowReturn;

typedef QuicFlowReturn (*QuicStatemRead)(QUIC *, RPacket *, QuicPacketFlags);
typedef int (*QuicStatemPreWork)(QUIC *);

typedef enum {
	QUIC_STATEM_INITIAL = 0,
    QUIC_STATEM_TLS_ST_OK,
    QUIC_STATEM_TLS_ST_CW_CLIENT_HELLO,
    QUIC_STATEM_TLS_ST_CW_CLIENT_CERTIFICATE,
    QUIC_STATEM_TLS_ST_CW_CERT_VERIFY,
    QUIC_STATEM_TLS_ST_CW_FINISHED,
    /*TLS Read state must in order */
    QUIC_STATEM_TLS_ST_CR_SERVER_HELLO,
    QUIC_STATEM_TLS_ST_CR_ENCRYPTED_EXTENSIONS,
    QUIC_STATEM_TLS_ST_CR_CERT_REQUEST,
    QUIC_STATEM_TLS_ST_CR_SERVER_CERTIFICATE,
    QUIC_STATEM_TLS_ST_CR_CERT_VERIFY,
    QUIC_STATEM_TLS_ST_CR_FINISHED,
    QUIC_STATEM_TLS_ST_CR_NEW_SESSION_TICKET,
    QUIC_STATEM_TLS_ST_SR_CLIENT_HELLO,
    QUIC_STATEM_TLS_ST_SR_CLIENT_CERTIFICATE,
    QUIC_STATEM_TLS_ST_SR_CERT_VERIFY,
    QUIC_STATEM_TLS_ST_SR_FINISHED,
    QUIC_STATEM_TLS_ST_SW_SERVER_HELLO,
    QUIC_STATEM_TLS_ST_SW_ENCRYPTED_EXTENSIONS,
    QUIC_STATEM_TLS_ST_SW_CERT_REQUEST,
    QUIC_STATEM_TLS_ST_SW_SERVER_CERTIFICATE,
    QUIC_STATEM_TLS_ST_SW_CERT_VERIFY,
    QUIC_STATEM_TLS_ST_SW_FINISHED,
    QUIC_STATEM_TLS_ST_SW_NEW_SESSION_TICKET,
    QUIC_STATEM_TLS_ST_SW_HANDSHAKE_DONE,
    QUIC_STATEM_HANDSHAKE,
	QUIC_STATEM_HANDSHAKE_DONE,
	QUIC_STATEM_CLOSING,
	QUIC_STATEM_DRAINING,
	QUIC_STATEM_CLOSED,
	QUIC_STATEM_MAX,
} QuicStatem;

/* Read-Write state */
typedef enum {
	QUIC_NOTHING = 0,
	QUIC_READING,
	QUIC_WRITING,
    QUIC_ASYNC_PAUSED,
	QUIC_FINISHED,
} QuicReadWriteState;

/* Read state */
typedef enum {
	QUIC_WANT_DATA = 0,
	QUIC_DATA_READY,
} QuicReadState;

/* Flow state */
typedef enum {
	QUIC_FLOW_NOTHING = 0,
	QUIC_FLOW_READING,
	QUIC_FLOW_WRITING,
	QUIC_FLOW_FINISHED,
} QuicFlowState;

typedef enum {
    TLS_MT_HELLO_REQUEST = 0,
    TLS_MT_CLIENT_HELLO = 1,
    TLS_MT_SERVER_HELLO = 2,
    TLS_MT_HELLO_VERIFY_REQUEST = 3,
    TLS_MT_NEW_SESSION_TICKET = 4,
    TLS_MT_END_OF_EARLY_DATA = 5,
    TLS_MT_HELLO_RETRY_REQUEST = 6,
    TLS_MT_ENCRYPTED_EXTENSIONS = 8,
    TLS_MT_CERTIFICATE = 11,
    TLS_MT_SERVER_KEY_EXCHANGE = 12,
    TLS_MT_CERTIFICATE_REQUEST = 13,
    TLS_MT_SERVER_HELLO_DONE = 14,
    TLS_MT_CERTIFICATE_VERIFY = 15,
    TLS_MT_CLIENT_KEY_EXCHANGE = 16,
    TLS_MT_FINISHED = 20,
    TLS_MT_CERTIFICATE_URL = 21,
    TLS_MT_CERTIFICATE_STATUS = 22,
    TLS_MT_SUPPLEMENTAL_DATA = 23,
    TLS_MT_KEY_UPDATE = 24,
    TLS_MT_MESSAGE_HASH = 254,
    TLS_MT_MESSAGE_TYPE_MAX,
} TlsMessageType;

typedef struct {
    QuicStatem state;
    QuicStatem next_state;
    QuicReadWriteState rw_state;
    TlsMessageType msg_type;
    QuicFlowReturn (*handler)(QUIC *, void *);
    QuicFlowReturn (*handshake)(TLS *, void *);
    int (*post_work)(QUIC *);
    int (*skip_check)(QUIC *);
    QuicFlowReturn (*pkt_proc)(QUIC *);
} QuicStatemMachine;

typedef struct {
    QuicStatemPreWork pre_work;
    QuicStatemRead recv;
} QuicStatemFlow;

QuicFlowReturn QuicInitialRecv(QUIC *, RPacket *, QuicPacketFlags);
QuicFlowReturn QuicPacketClosingRecv(QUIC *, RPacket *, QuicPacketFlags);
QuicFlowReturn QuicPacketDrainingRecv(QUIC *, RPacket *, QuicPacketFlags);
QuicFlowReturn QuicPacketRead(QUIC *, RPacket *, QuicPacketFlags);
int QuicInitialSend(QUIC *);
int QuicStateMachineAct(QUIC *, const QuicStatemFlow *, size_t);
int QuicConnect(QUIC *);
int QuicAccept(QUIC *);
int QuicStatemReadBytes(QUIC *, RPacket *);

#endif
