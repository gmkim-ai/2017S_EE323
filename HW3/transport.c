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

#define MAX_PAYLOAD_SIZE 536

enum { CSTATE_CLOSED, CSTATE_LISTEN, CSTATE_SYN_SENT, CSTATE_SYN_RCVD, CSTATE_ESTABLISHED, CSTATE_FIN_WAIT_1, CSTATE_FIN_WAIT_2, CSTATE_CLOSE_WAIT, CSTATE_LAST_ACK};  

/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;

} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);
bool_t is_FIN(uint8_t flags);
bool_t is_SYN(uint8_t flags);
bool_t is_ACK(uint8_t flags);

typedef struct
{
    struct tcphdr header;
    char payload[MAX_PAYLOAD_SIZE];
} segment;


/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);

    generate_initial_seq_num(ctx);
    ctx->connection_state = CSTATE_LISTEN;   /*malloc*/

    if (is_active == TRUE) {        /*client*/
        segment *synsend;
        synsend = (segment *)calloc(1, sizeof(segment));
        segment *synackrecv;
        synackrecv = (segment *)calloc(1, sizeof(segment));
        segment *acksend;
        acksend = (segment *)calloc(1, sizeof(segment));
        synsend->header.th_seq = htonl(ctx->initial_sequence_num);
        synsend->header.th_flags |= TH_SYN;
        stcp_network_send(sd, synsend, sizeof(segment), NULL);
        ctx->connection_state = CSTATE_SYN_SENT;       /*send syn*/
        while(1) {
            stcp_wait_for_event(sd, 2, NULL);    /*wait*/     
            stcp_network_recv(sd, synackrecv, sizeof(segment));
            if (is_SYN(synackrecv->header.th_flags) && is_ACK(synackrecv->header.th_flags) && ntohl(synackrecv->header.th_ack) == ntohl(synsend->header.th_seq) + 1) {    /*recv synack*/
                acksend->header.th_ack = htonl(ntohl(synackrecv->header.th_seq) + 1);
                acksend->header.th_flags |= TH_ACK;    /*send ack*/
                stcp_network_send(sd, acksend, sizeof(segment), NULL);
                ctx->connection_state = CSTATE_ESTABLISHED;
                break;
            }
        }
        free(synsend);
        free(synackrecv);
        free(acksend);
    }
    else {                        /*server*/
        segment *synrecv;
        synrecv = (segment *)calloc(1, sizeof(segment));
        segment *synacksend;
        synacksend = (segment *)calloc(1, sizeof(segment));
        segment *ackrecv;
        ackrecv = (segment *)calloc(1, sizeof(segment));
        while(1) {
            stcp_wait_for_event(sd, 2, NULL);       /*wait syn*/
            stcp_network_recv(sd, synrecv, sizeof(segment));
            if (is_SYN(synrecv->header.th_flags)) {      /*recv syn*/
                synacksend->header.th_seq = htonl(ctx->initial_sequence_num);
                synacksend->header.th_ack = htonl(ntohl(synrecv->header.th_seq) + 1);
                synacksend->header.th_flags |= TH_SYN;
                synacksend->header.th_flags |= TH_ACK;
                stcp_network_send(sd, synacksend, sizeof(segment), NULL);
                ctx->connection_state = CSTATE_SYN_RCVD;  /*send synack*/
                break;
            }
        }
        while(1) {
            stcp_wait_for_event(sd, 2, NULL);        /*wait ack*/
            stcp_network_recv(sd, ackrecv, sizeof(segment));
            if (is_ACK(ackrecv->header.th_flags) && ntohl(ackrecv->header.th_ack) == ntohl(synacksend->header.th_seq) + 1) {
                ctx->connection_state = CSTATE_ESTABLISHED;
                break;
            }
        }
        free(synrecv);
        free(synacksend);
        free(ackrecv);
    }
    stcp_unblock_application(sd);

    control_loop(sd, ctx);
    
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
    ctx->initial_sequence_num = rand() % 256;
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
    assert(ctx);

    while (!ctx->done)
    {
        unsigned int event;

        /*wait for segment*/
        event = stcp_wait_for_event(sd, ANY_EVENT, NULL);

        /*when CLOSING STATE, send FIN segment*/
        if (event & APP_CLOSE_REQUESTED)
        {
            segment *sendFIN_segment;
            sendFIN_segment = (segment *)calloc(1, sizeof(segment));
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
            free(sendFIN_segment);
        }

        /*When recv from network, if that segment is FIN or ACK, do appropriate work for CLOSING STATE. If that segment is data, just send to app layer and don't care about seq num or ack num. Because This is reliable enviroment. */
         else if (event & NETWORK_DATA)
        {
            segment *recv_segment;
            recv_segment = (segment *)calloc(1, sizeof(segment));
            segment *sendACK_segment;
            sendACK_segment = (segment *)calloc(1, sizeof(segment));
            stcp_network_recv(sd, recv_segment, sizeof(segment));
            if (is_FIN(recv_segment->header.th_flags) && ctx->connection_state == CSTATE_ESTABLISHED) {
                sendACK_segment->header.th_flags |= TH_ACK;
                stcp_network_send(sd, sendACK_segment, sizeof(segment), NULL);
                stcp_fin_received(sd);
                ctx->connection_state = CSTATE_CLOSE_WAIT;
            }
            else if (is_FIN(recv_segment->header.th_flags) && ctx->connection_state == CSTATE_FIN_WAIT_2) {
                sendACK_segment->header.th_flags |= TH_ACK;
                stcp_network_send(sd, sendACK_segment, sizeof(segment), NULL);
                stcp_fin_received(sd);
                ctx->connection_state = CSTATE_CLOSED;          
                ctx->done = TRUE;
            } 
            else if (is_ACK(recv_segment->header.th_flags) && ctx->connection_state == CSTATE_FIN_WAIT_1) {
                ctx->connection_state = CSTATE_FIN_WAIT_2;
            }
            else if (is_ACK(recv_segment->header.th_flags) && ctx->connection_state == CSTATE_LAST_ACK) {
                ctx->connection_state = CSTATE_CLOSED;
                ctx->done = TRUE;
            }
            /*just send because reliable*/
            else {
                stcp_app_send(sd, recv_segment->payload, MIN(MAX_PAYLOAD_SIZE, strlen(recv_segment->payload)));
            }
            free(recv_segment);
            free(sendACK_segment);
        }

        /*just send to network layer with header, no set seq num or ack num in header because this is reliable environment*/
        else if (event & APP_DATA)
        {
            segment *send_segment;
            send_segment = (segment *)calloc(1, sizeof(segment));
            stcp_app_recv(sd, send_segment->payload, MAX_PAYLOAD_SIZE);
            stcp_network_send(sd, send_segment, sizeof(segment), NULL);
            free(send_segment);
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

bool_t is_FIN(uint8_t flags)      /*determine FIN flag is true*/
{
    if (flags % 2 == 1) return TRUE;
    return FALSE;
}

bool_t is_SYN(uint8_t flags)     /*determine SYN flag is true*/
{
    if (flags % 4 > 1) return TRUE;
    return FALSE;
}

bool_t is_ACK(uint8_t flags)      /*determine ACK flag is true*/
{
    if (flags % 32 > 15) return TRUE;
    return FALSE;
}