/*
 * Remy Lewis(remyknight1119@gmail.com)
 */

#include "quic_test.h"

#include <stdio.h>
#include <getopt.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <tbquic/quic.h>
#include <tbquic/stream.h>
#include <tbquic/dispenser.h>

#include "common.h"

#define SEVER_TEST_IOV_NUM 10

static QuicTestBuff QuicServerIovBuf[SEVER_TEST_IOV_NUM];
static const char *program_version = "1.0.0";//PACKAGE_STRING;

static const struct option long_opts[] = {
    {"help", 0, 0, 'H'},
    {"address", 0, 0, 'a'},
    {"port", 0, 0, 'p'},
    {"certificate", 0, 0, 'c'},
    {"key", 0, 0, 'k'},
    {0, 0, 0, 0}
};

static const char *options[] = {
    "--address      		-a	IP address for QUIC communication\n",	
    "--port         		-p	Port for QUIC communication\n",	
    "--certificate  		-c	certificate file\n",	
    "--key      		    -k	key file\n",	
    "--help         		-H	Print help information\n",	
};

static uint8_t h3[] =
    "\x00\x04\x19\x01\x80\x01\x00\x00\x06\x80\x00\x40\x00\x07\x40\x64"
    "\xc0\x00\x00\x04\xd7\x92\xfe\xec\xb6\x99\xd0\x12\xc0\x00\x00\x0d"
    "\xcc\xa6\x0b\x11\x00";

static QuicTestData stream_data[] = {
    {
        .data = h3,
        .len = sizeof(h3) - 1,
    },
};

#define STREAM_DATA_NUM ARRAY_SIZE(stream_data)

static uint8_t appdata1[] =
    "\x00\x04\x19\x01\x80\x01\x00\x00\x06\x80\x02\x00\x00\x07\x40\x64"
    "\xc0\x00\x00\x09\x03\x15\xe8\x23\xa8\x51\x09\x24\xc0\x00\x00\x04"
    "\x2f\x0c\x7e\x1f\x02\xf6\x12";

static void help(void)
{
    int     index;

    fprintf(stdout, "Version: %s\n", program_version);

    fprintf(stdout, "\nOptions:\n");
    for (index = 0; index < ARRAY_SIZE(options); index++) {
        fprintf(stdout, "  %s", options[index]);
    }
}

static const char *optstring = "Ha:p:c:k:";


static int QuicServer(struct sockaddr_in *addr, char *cert, char *key)
{
    QUIC_CTX *ctx = NULL;
    QUIC *quic = NULL;
    QUIC_DISPENSER *dis = NULL;
    QuicTestData *data = stream_data;
    QUIC_STREAM_HANDLE h = -1;
    struct epoll_event ev = {};
    struct epoll_event events[QUIC_TEST_EVENT_MAX_NUM] = {};
    QUIC_STREAM_IOVEC iov[SEVER_TEST_IOV_NUM] = {};
    bool new = false;
    uint32_t mss = QUIC_RECORD_MSS_LEN;
    int sockfd = 0;
    int reuse = 1;
    int epfd = 0;
    int nfds = 0;
    int efd = 0;
    int i = 0;
    int j = 0;
    int index = 0;
    int cnt = 0;
    int err = 0;
    int ret = 0;

    QuicTestStreamIovecInit(iov, QuicServerIovBuf, SEVER_TEST_IOV_NUM);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(sockfd, (struct sockaddr *)addr, sizeof(*addr)) == -1) {
        perror("bind");
        return -1;
    }

    epfd = epoll_create(1);
    if (epfd < 0) {
        close(sockfd);
        return -1;
    }
    AddEpollEvent(epfd, &ev, sockfd);

    ctx = QuicCtxNew(QuicDispenserMethod());
    if (ctx == NULL) {
        goto out;
    }

    if (QuicCtxUsePrivateKeyFile(ctx, key, QUIC_FILE_TYPE_PEM) < 0) {
        printf("Use Private Key file %s failed\n", key);
        goto out;
    }

    if (QuicCtxUseCertificateFile(ctx, cert, QUIC_FILE_TYPE_PEM) < 0) {
        printf("Use Private Cert file %s failed\n", cert);
        goto out;
    }

    if (QuicCtxCtrl(ctx, QUIC_CTRL_SET_MSS, &mss, 0) < 0) {
        goto out;
    }

    QUIC_CTX_set_keylog_callback(ctx, QuicKeyLog);
    if (QuicCtxServerExtensionSet(ctx) < 0) {
        printf("Set Extension failed\n");
        goto out;
    }

    QUIC_CTX_set_max_early_data(ctx, 0xFFFA);

    dis = QuicCreateDispenser(sockfd);
    if (dis == NULL) {
        printf("Create dispenser failed\n");
        goto out;
    }

    while (1) {
        nfds = epoll_wait(epfd, events, QUIC_TEST_EVENT_MAX_NUM, -1);
        for (i = 0; i < nfds; i++) {
            if (events[i].events & EPOLLIN) {
                if ((efd = events[i].data.fd) < 0) {
                    continue;
                }

                if (efd == sockfd) {
                    quic = QuicDoDispense(dis, ctx, &new);
                    if (quic == NULL) {
                        goto next;
                    }

                    if (new) {
                        index = 0;
                        printf("new QUIC\n");
                        data = &data[index]; 
                        ret = QuicStreamSendEarlyData(quic, &h, true,
                                                data->data, data->len);
                        if (ret < 0) {
                            err = QUIC_get_error(quic, ret);
                            if (err != QUIC_ERROR_WANT_READ) {
                                printf("Error\n");
                                goto out;
                            }

                            continue;
                        }
                        goto next;
                    }
                    printf("old QUIC\n");
                    cnt = QuicStreamReadV(quic, iov, SEVER_TEST_IOV_NUM);
                    printf("cnt = %d\n", cnt);
                    if (cnt < 0) {
                        err = QUIC_get_error(quic, ret);
                        if (err != QUIC_ERROR_WANT_READ) {
                            goto out;
                        }
                    }
                    for (j = 0; j < cnt; j++) {
                        printf("Stream ID: %lu\t", iov[j].handle);
                        QuicPrint(iov[j].iov_base, iov[j].data_len);
                    }
                    //bzero(buf, sizeof(buf));
                    /* 接收客户端的消息 */

                    h = QuicStreamOpen(quic, false);
                    if (h < 0) {
                        goto out;
                    }

                    if (QuicStreamSend(quic, h, appdata1, sizeof(appdata1) - 1) < 0) {
                        goto out;
                    }
next:
                    AddEpollEvent(epfd, &ev, sockfd);
                    continue;
                }
            }
        }
    }

out:
    QuicFree(quic);
    QuicDestroyDispenser(dis);
    QuicCtxFree(ctx);
    close(epfd);
    close(sockfd);

    return 0;
}

int main(int argc, char **argv)  
{
    char *ip = NULL;
    char *port = NULL;
    char *cert = NULL;
    char *key = NULL;
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
    };
    int c = 0;

    while ((c = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1) {
        switch (c) {
            case 'H':
                help();
                return 0;
            case 'a':
                ip = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'c':
                cert = optarg;
                break;
            case 'k':
                key = optarg;
                break;

            default:
                help();
                return -1;
        }
    }

    if (ip == NULL) {
        fprintf(stderr, "Please input IP by -a!\n");
        return -1;
    }

    if (port == NULL) {
        fprintf(stderr, "Please input port by -p!\n");
        return -1;
    }

    QuicInit();

    addr.sin_port = htons(atoi(port));
    addr.sin_addr.s_addr = inet_addr(ip);

    return QuicServer(&addr, cert, key);
}
