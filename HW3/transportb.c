/*
 * transport.c 
 *
 * CS244a HW#3 (Reliable Transport)
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file. 
 *
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <netinet/in.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"

#define MAX_SEND_WINDOW_SIZE 3027
#define MAX_PAYLOAD_SIZE 536
#define MAX_RETRANS_TRY 5

enum { CSTATE_CLOSED, CSTATE_LISTEN, CSTATE_SYN_SENT, CSTATE_SYN_RCVD, CSTATE_ESTABLISHED, CSTATE_FIN_WAIT_1, CSTATE_FIN_WAIT_2, CSTATE_CLOSE_WAIT, CSTATE_LAST_ACK};    /* obviously you should have more states */

typedef struct
{
    struct tcphdr header;
    char payload[MAX_PAYLOAD_SIZE];
} segment;

/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;
    tcp_seq next_sequence_num;        /*for sender*/
    tcp_seq expect_sequence_num[100];   /*for receiver*/
    tcp_seq send_base[100];              /*for sender's window*/
    clock_t transmitt_time;              /*for retransmitt*/
    clock_t transmitt_time_fin;           /*for closing retranmitt*/
    segment send_segment_array[100];     /*for sender*/
    segment recv_segment_array[100];      /*for receiver's buffer*/
    struct timespec *estimate_RTT;        /*timeout value*/
    int retransmitt_try[100];            /*for sender*/
    int send_size;                      /*num of segment of send*/
    int recv_size;                       /*num of segment of recv*/
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);
bool_t is_FIN(uint8_t flags);          
bool_t is_SYN(uint8_t flags);
bool_t is_ACK(uint8_t flags);

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;
    unsigned int result;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);
    ctx->estimate_RTT = (struct timespec *) calloc(1, sizeof(struct timespec));
    assert(ctx->estimate_RTT);               /*malloc*/

    generate_initial_seq_num(ctx);
    ctx->next_sequence_num = ctx->initial_sequence_num;
    ctx->send_base[0] = ctx->initial_sequence_num;
    ctx->connection_state = CSTATE_LISTEN;
    ctx->retransmitt_try[0] = 0;
    ctx->estimate_RTT->tv_nsec = 0;         /*initialize*/

    if (is_active == TRUE) {                /*client*/
        segment *synsend;
        synsend = (segment *)calloc(1, sizeof(segment));
        assert(synsend);
        segment *synackrecv;
        synackrecv = (segment *)calloc(1, sizeof(segment));
        assert(synackrecv);
        segment *acksend;
        acksend = (segment *)calloc(1, sizeof(segment));
        assert(acksend);
        synsend->header.th_seq = htonl(ctx->initial_sequence_num);
        synsend->header.th_flags |= TH_SYN;
        stcp_network_send(sd, synsend, sizeof(segment), NULL);  /*send syn*/
        ctx->connection_state = CSTATE_SYN_SENT;
        while(1) {
            clock_gettime(CLOCK_REALTIME, ctx->estimate_RTT);  /*wait synack*/
            ctx->estimate_RTT->tv_sec += 1;
            result = stcp_wait_for_event(sd, 2, ctx->estimate_RTT);
            if (result == 2) {
                stcp_network_recv(sd, synackrecv, sizeof(segment));
                if (is_SYN(synackrecv->header.th_flags) && is_ACK(synackrecv->header.th_flags) && ntohl(synackrecv->header.th_ack) == ntohl(synsend->header.th_seq) + 1) {
                    ctx->retransmitt_try[0] = 0;
                    acksend->header.th_ack = htonl(ntohl(synackrecv->header.th_seq) + 1);
                    acksend->header.th_flags |= TH_ACK;
                    ctx->next_sequence_num = ntohl(synackrecv->header.th_ack);
                    ctx->expect_sequence_num[0] = ntohl(acksend->header.th_ack);
                    stcp_network_send(sd, acksend, sizeof(segment), NULL);
                    ctx->connection_state = CSTATE_ESTABLISHED;
                    break;
                }
            }
            stcp_network_send(sd, synsend, sizeof(segment), NULL);  /*retransmitt*/
            ctx->retransmitt_try[0]++;
            if (ctx->retransmitt_try[0] == MAX_RETRANS_TRY) {
                fprintf(stderr, "transmitt total 6 times\n");
                fflush(stderr);
                exit(0);
            }
        }
        free(synsend);
        free(synackrecv);
        free(acksend);
    }
    else {                                      /*server*/
        segment *synrecv;
        synrecv = (segment *)calloc(1, sizeof(segment));
        assert(synrecv);
        segment *synacksend;
        synacksend = (segment *)calloc(1, sizeof(segment));
        assert(synacksend);
        segment *ackrecv;
        ackrecv = (segment *)calloc(1, sizeof(segment));
        assert(ackrecv);
        while(1) {
            stcp_wait_for_event(sd, 2, NULL);          /*wait syn*/
            stcp_network_recv(sd, synrecv, sizeof(segment));
            if (is_SYN(synrecv->header.th_flags)) {
                synacksend->header.th_seq = htonl(ctx->initial_sequence_num);
                synacksend->header.th_ack = htonl(ntohl(synrecv->header.th_seq) + 1);
                synacksend->header.th_flags |= TH_SYN;
                synacksend->header.th_flags |= TH_ACK;
                stcp_network_send(sd, synacksend, sizeof(segment), NULL);
                ctx->connection_state = CSTATE_SYN_RCVD;      /*send synack*/
                break;
            }
        }
        while(1) {
            clock_gettime(CLOCK_REALTIME, ctx->estimate_RTT);
            ctx->estimate_RTT->tv_sec += 1;          /*wait ack*/
            result = stcp_wait_for_event(sd, 2, ctx->estimate_RTT);
            if (result == 2) {
                stcp_network_recv(sd, ackrecv, sizeof(segment));
                if (is_ACK(ackrecv->header.th_flags) && ntohl(ackrecv->header.th_ack) == ntohl(synacksend->header.th_seq) + 1) {
                    ctx->retransmitt_try[0] = 0;
                    ctx->connection_state = CSTATE_ESTABLISHED;
                    ctx->next_sequence_num = ntohl(ackrecv->header.th_ack);
                    ctx->expect_sequence_num[0] = ntohl(synacksend->header.th_ack);
                    break;
                }
            }
            stcp_network_send(sd, synacksend, sizeof(segment), NULL);
            ctx->retransmitt_try[0]++;  /*retransmitt synack for recv ack*/
            if (ctx->retransmitt_try[0] == MAX_RETRANS_TRY) {
                fprintf(stderr, "transmitt total 6 times\n");
                fflush(stderr);
                exit(0);
            }
        }
        free(synrecv);
        free(synacksend);
        free(ackrecv);
    }

    stcp_unblock_application(sd);

    control_loop(sd, ctx);

    free(ctx->estimate_RTT);
    free(ctx);
}


/* generate random initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);

#ifdef FIXED_INITNUM
    /* please don't change this! */
    ctx->initial_sequence_num = 1;
#else
    /* you have to fill this up */
    srand(time(NULL));
    ctx->initial_sequence_num = rand() % 256;   /*generate 0~255*/
#endif
}


/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    int i, j, result;
    tcp_seq temp;
    assert(ctx);
    ctx->send_base[0] = ctx->next_sequence_num;

    while (!ctx->done)
    {
        unsigned int event;

        /*this for retransmission of FIN segment in CLOSING STATE*/
        if ((ctx->connection_state == CSTATE_FIN_WAIT_1 || ctx->connection_state == CSTATE_LAST_ACK) && (long)(clock()-ctx->transmitt_time_fin)*1000000000/CLOCKS_PER_SEC > 1000000000) {
            segment *sendFIN_segment;
            sendFIN_segment = (segment *)calloc(1, sizeof(segment));
            assert(sendFIN_segment);
            sendFIN_segment->header.th_flags |= TH_FIN;
            stcp_network_send(sd, sendFIN_segment, sizeof(segment), NULL);
            ctx->transmitt_time_fin = clock();
            free(sendFIN_segment);
            ctx->retransmitt_try[99]++;
            if (ctx->retransmitt_try[99] == MAX_RETRANS_TRY) {
                fprintf(stderr, "transmitt total 6 times\n");
                fflush(stderr);
                exit(0);
            }
        }

        /*this for retransmission of segment because of timeout in window*/
        if (ctx->send_size != 0 && (long)(clock()-ctx->transmitt_time)*1000000000/CLOCKS_PER_SEC > 1000000000) {
            for (i = 0; i < ctx->send_size; i++) {
                stcp_network_send(sd, &ctx->send_segment_array[i], sizeof(segment), NULL);
                ctx->retransmitt_try[i]++;
                if (ctx->retransmitt_try[i] == MAX_RETRANS_TRY) {
                    fprintf(stderr, "transmitt total 6 times\n");
                    fflush(stderr);
                    exit(0);
                }
            }
            ctx->transmitt_time = clock();
        }

        /*wait for event*/
        event = stcp_wait_for_event(sd, ANY_EVENT, ctx->estimate_RTT);
        
        /*send FIN segment to other side when myclose()*/
        if (event & APP_CLOSE_REQUESTED)
        {
            segment *sendFIN_segment;
            sendFIN_segment = (segment *)calloc(1, sizeof(segment));
            assert(sendFIN_segment);
            if (ctx->connection_state == CSTATE_ESTABLISHED) {
                sendFIN_segment->header.th_flags |= TH_FIN;
                stcp_network_send(sd, sendFIN_segment, sizeof(segment), NULL);
                ctx->connection_state = CSTATE_FIN_WAIT_1;
            }
            else if (ctx->connection_state == CSTATE_CLOSE_WAIT) {
                sendFIN_segment->header.th_flags |= TH_FIN;
                stcp_network_send(sd, sendFIN_segment, sizeof(segment), NULL);
                ctx->connection_state = CSTATE_LAST_ACK; 
            }
            ctx->transmitt_time_fin = clock();
            free(sendFIN_segment);
        }

        /*First, when recv FIN segment of ACK segment in CLOSING STATE, do appropriate work. When recv ACK bigger than send_base, move window by moving send_base[0]. Other case, when recv data, if there is no data in recv buffer, then just send to app layer. If there is some data, decide save this data in buffer or send to app layer by comparing expect seq num. When recv expect seq number, compare after buffer for send to app layer.*/
        else if (event & NETWORK_DATA)
        {
            segment *recv_segment;
            recv_segment = (segment *)calloc(1, sizeof(segment));
            assert(recv_segment);
            segment *sendACK_segment;
            sendACK_segment = (segment *)calloc(1, sizeof(segment));
            assert(sendACK_segment);
            stcp_network_recv(sd, recv_segment, sizeof(segment));
            if (is_FIN(recv_segment->header.th_flags) && (ctx->connection_state == CSTATE_ESTABLISHED || ctx->connection_state == CSTATE_CLOSE_WAIT)) {
                sendACK_segment->header.th_flags |= TH_ACK;
                stcp_network_send(sd, sendACK_segment, sizeof(segment), NULL);
                stcp_fin_received(sd);
                ctx->connection_state = CSTATE_CLOSE_WAIT;
            }
            else if (is_FIN(recv_segment->header.th_flags) && ctx->connection_state == CSTATE_FIN_WAIT_2) {
                sendACK_segment->header.th_flags |= TH_ACK;
                stcp_network_send(sd, sendACK_segment, sizeof(segment), NULL);
                result = stcp_wait_for_event(sd, 2, ctx->estimate_RTT);
                if (result == 2) {
                    stcp_network_send(sd, sendACK_segment, sizeof(segment), NULL);
                }
                stcp_fin_received(sd);
                ctx->connection_state = CSTATE_CLOSED;          
                ctx->done = TRUE;
            }
            else if (is_ACK(recv_segment->header.th_flags) && ctx->connection_state == CSTATE_ESTABLISHED) {
                if (ntohl(recv_segment->header.th_ack) > ctx->send_base[0]) {
                    for (i = 1; i < ctx->send_size + 1; i++) {
                        if (ntohl(recv_segment->header.th_ack) == ctx->send_base[i]) {
                            result = 0;
                            break;
                        }
                        if (i == ctx->send_size) result = 1;
                    }
                    if (result == 0) {
                        for (j = 0; j < ctx->send_size - i; j++) {
                            ctx->send_segment_array[j] = ctx->send_segment_array[j + i];
                            ctx->retransmitt_try[j] = ctx->retransmitt_try[j + i];
                            ctx->send_base[j] = ctx->send_base[j + i];
                        }
                        ctx->send_size -= i;
                        ctx->transmitt_time = clock();
                    }
                }
            }
            else if (is_ACK(recv_segment->header.th_flags) && ctx->connection_state == CSTATE_FIN_WAIT_1) {
                ctx->connection_state = CSTATE_FIN_WAIT_2;
                ctx->retransmitt_try[99] = 0;
            }
            else if (is_ACK(recv_segment->header.th_flags) && ctx->connection_state == CSTATE_LAST_ACK) {
                ctx->connection_state = CSTATE_CLOSED;
                ctx->done = TRUE;
            }
            else {
                ctx->recv_size++;
                /*send data to app layer*/
                if (ntohl(recv_segment->header.th_seq) == ctx->expect_sequence_num[0]) {
                    stcp_app_send(sd, recv_segment->payload, MIN(MAX_PAYLOAD_SIZE, strlen(recv_segment->payload)));
                    ctx->recv_size--;
                    ctx->expect_sequence_num[0] = ctx->expect_sequence_num[0] + strlen(recv_segment->payload);
                    temp = ctx->expect_sequence_num[0];
                    while (ntohl(ctx->recv_segment_array[0].header.th_seq) == temp){
                        stcp_app_send(sd, ctx->recv_segment_array[0].payload, MIN(MAX_PAYLOAD_SIZE, strlen(ctx->recv_segment_array[0].payload)));
                        ctx->expect_sequence_num[0] = ctx->expect_sequence_num[0] + strlen(ctx->recv_segment_array[0].payload);
                        temp = ctx->expect_sequence_num[0];
                        ctx->recv_size--;
                        for (j = 0; j < ctx->recv_size; j++) {
                            ctx->recv_segment_array[j] = ctx->recv_segment_array[j + 1];
                        }
                        for (j = 1; j < ctx->recv_size; j++) {
                            ctx->expect_sequence_num[j] = ctx->expect_sequence_num[j + 1];
                        }
                    } 
                    sendACK_segment->header.th_flags |= TH_ACK;
                    sendACK_segment->header.th_ack = htonl(ctx->expect_sequence_num[0]);
                    stcp_network_send(sd, sendACK_segment, sizeof(segment), NULL);
                }
                /*save in recv buffer*/
                else {
                    if (ctx->recv_size == 1) {
                        ctx->expect_sequence_num[1] = ntohl(recv_segment->header.th_seq);
                        memcpy(&ctx->recv_segment_array[0], recv_segment, sizeof(segment));
                    }
                    else {
                        for (i = 0; i < ctx->recv_size - 1; i++) {
                            if (ntohl(ctx->recv_segment_array[i].header.th_seq) > ntohl(recv_segment->header.th_seq)) break;
                        }

                        for (j = ctx->recv_size - 1; j > i; j--) {
                            ctx->recv_segment_array[j] = ctx->recv_segment_array[j - 1];             
                        }
                        for (j = ctx->recv_size; j > i + 1; j--) {
                            ctx->expect_sequence_num[j] = ctx->expect_sequence_num[j - 1];             
                        }
                        memcpy(&ctx->recv_segment_array[i], recv_segment, sizeof(segment));
                        ctx->expect_sequence_num[i + 1] = ntohl(recv_segment->header.th_seq);
                    }
                    sendACK_segment->header.th_flags |= TH_ACK;
                    sendACK_segment->header.th_ack = htonl(ctx->expect_sequence_num[0]);
                    stcp_network_send(sd, sendACK_segment, sizeof(segment), NULL);
                }
            }
            free(recv_segment);
            free(sendACK_segment);
        }

        /*send data with header to network layer and save data in send buffer. If there is timeout, we use this buffer for retransmitt.*/
        else if (event & APP_DATA)
        {
            if (ctx->next_sequence_num + sizeof(segment) < ctx->send_base[0] + MAX_SEND_WINDOW_SIZE) {
                segment *send_segment;
                send_segment = (segment *)calloc(1, sizeof(segment));
                assert(send_segment);
                stcp_app_recv(sd, send_segment->payload, MAX_PAYLOAD_SIZE);
                send_segment->header.th_seq = htonl(ctx->next_sequence_num);
                memcpy(&ctx->send_segment_array[ctx->send_size], send_segment, sizeof(segment));
                ctx->retransmitt_try[ctx->send_size] = 0;
                if (ctx->send_size == 0) ctx->transmitt_time = clock();
                ctx->next_sequence_num += strlen(send_segment->payload);
                ctx->send_size++;
                ctx->send_base[ctx->send_size] = ctx->next_sequence_num;
                stcp_network_send(sd, send_segment, sizeof(segment), NULL);
                free(send_segment);
            }
        }
    }
}

/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}

bool_t is_FIN(uint8_t flags)     /*determine FIN flag is true*/
{
    if (flags % 2 == 1) return TRUE;
    return FALSE;
}

bool_t is_SYN(uint8_t flags)     /*determine SYN flag is true*/
{
    if (flags % 4 > 1) return TRUE;
    return FALSE;
}

bool_t is_ACK(uint8_t flags)     /*determine ACK flag is ture*/
{
    if (flags % 32 > 15) return TRUE;
    return FALSE;
}