#ifndef TBQUIC_QUIC_FRAME_H_
#define TBQUIC_QUIC_FRAME_H_

#include <tbquic/types.h>

#include "packet_local.h"
    
typedef int (*QuicFrameParser)(QUIC *, RPacket *);

enum QuicFrameType {
    QUIC_FRAME_TYPE_PADDING = 0x00,
    QUIC_FRAME_TYPE_PING = 0x01,
    QUIC_FRAME_TYPE_ACK = 0x02,
    QUIC_FRAME_TYPE_ACK_ECN_COUNTS = 0x03,
    QUIC_FRAME_TYPE_RESET_STREAM = 0x04,
    QUIC_FRAME_TYPE_STOP_SENDING = 0x05,
    QUIC_FRAME_TYPE_CRYPTO = 0x06,
    QUIC_FRAME_TYPE_NEW_TOKEN = 0x07,
    QUIC_FRAME_TYPE_STREAM = 0x08,
    QUIC_FRAME_TYPE_MAX_DATA = 0x10,
    QUIC_FRAME_TYPE_MAX_STREAM_DATA = 0x11,
    QUIC_FRAME_TYPE_MAX_STREAMS = 0x12,
    QUIC_FRAME_TYPE_DATA_BLOCKED = 0x14,
    QUIC_FRAME_TYPE_STREAM_DATA_BLOCKED = 0x15,
    QUIC_FRAME_TYPE_STREAMS_BLOCKED = 0x16,
    QUIC_FRAME_TYPE_NEW_CONNECTION_ID = 0x18,
    QUIC_FRAME_TYPE_RETIRE_CONNECTION_ID = 0x19,
    QUIC_FRAME_TYPE_PATH_CHALLENGE = 0x1a,
    QUIC_FRAME_TYPE_PATH_RESPONSE = 0x1b,
    QUIC_FRAME_TYPE_CONNECTION_CLOSE = 0x1c,
    QUIC_FRAME_TYPE_HANDSHAKE_DONE = 0x1e,
    QUIC_FRAME_TYPE_MAX,
};

int QuicFrameDoParser(QUIC *, RPacket *);
int QuicFramePaddingBuild(WPacket *, size_t);
int QuicFramePingBuild(WPacket *);
int QuicFrameCryptoBuild(QUIC *, WPacket *);

#endif
