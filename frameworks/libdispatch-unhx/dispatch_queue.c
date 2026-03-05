/*
 * frameworks/libdispatch/dispatch_queue.c — Mach port-backed dispatch queues
 *
 * Each queue owns a Mach port (via SYS_PORT_ALLOC). A worker thread loops
 * on mach_msg_recv_user(). dispatch_async_f sends a work message to the
 * queue's port. dispatch_sync_f sends + blocks on a reply port.
 */

#include "dispatch.h"
#include "mach_msg.h"
#include "thread.h"
#include "malloc.h"
#include "string.h"

/* Work item message sent to queue port */
#define DISPATCH_MSG_WORK  0x4400
#define DISPATCH_MSG_REPLY 0x4401

struct dispatch_work_msg {
    mach_msg_header_t    header;
    dispatch_function_t  work;
    void                *context;
    mach_port_name_t     reply_port;  /* 0 for async, port name for sync */
};

struct dispatch_reply_msg {
    mach_msg_header_t    header;
};

struct dispatch_queue_s {
    const char          *label;
    mach_port_name_t     port;
    BOOL                 is_main;
};

static struct dispatch_queue_s main_queue;
static int dispatch_initialized = 0;

/* Worker thread: receive and execute work items */
static void queue_worker(void *arg)
{
    struct dispatch_queue_s *q = (struct dispatch_queue_s *)arg;
    char buf[256];
    mach_msg_size_t out_size;

    for (;;) {
        kern_return_t kr = mach_msg_recv_user(q->port, buf, sizeof(buf),
                                               &out_size);
        if (kr != 0)
            continue;

        mach_msg_header_t *hdr = (mach_msg_header_t *)buf;
        if (hdr->msgh_id != DISPATCH_MSG_WORK)
            continue;

        struct dispatch_work_msg *wmsg = (struct dispatch_work_msg *)buf;

        /* Execute the work item */
        if (wmsg->work)
            wmsg->work(wmsg->context);

        /* If sync, send reply */
        if (wmsg->reply_port != MACH_PORT_NULL) {
            struct dispatch_reply_msg reply;
            memset(&reply, 0, sizeof(reply));
            reply.header.msgh_bits = 0;
            reply.header.msgh_size = sizeof(reply);
            reply.header.msgh_id   = DISPATCH_MSG_REPLY;
            mach_msg_send_user(&reply.header, sizeof(reply), wmsg->reply_port);
        }
    }
}

void dispatch_init(void)
{
    if (dispatch_initialized)
        return;
    dispatch_initialized = 1;

    main_queue.label   = "com.unhox.main";
    main_queue.port    = mach_port_allocate_user();
    main_queue.is_main = YES;

    /* Main queue worker runs on a dedicated thread */
    unhx_thread_create(queue_worker, &main_queue, 0);
}

dispatch_queue_t dispatch_get_main_queue(void)
{
    return &main_queue;
}

dispatch_queue_t dispatch_queue_create(const char *label, void *attr)
{
    (void)attr;

    struct dispatch_queue_s *q = (struct dispatch_queue_s *)
        calloc(1, sizeof(*q));
    if (!q)
        return 0;

    q->label   = label;
    q->port    = mach_port_allocate_user();
    q->is_main = NO;

    /* Start a worker thread for this queue */
    unhx_thread_create(queue_worker, q, 0);

    return q;
}

void dispatch_async_f(dispatch_queue_t queue,
                      void *context, dispatch_function_t work)
{
    if (!queue || !work)
        return;

    struct dispatch_work_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.msgh_bits = 0;
    msg.header.msgh_size = sizeof(msg);
    msg.header.msgh_id   = DISPATCH_MSG_WORK;
    msg.work             = work;
    msg.context          = context;
    msg.reply_port       = MACH_PORT_NULL;

    mach_msg_send_user(&msg.header, sizeof(msg), queue->port);
}

void dispatch_sync_f(dispatch_queue_t queue,
                     void *context, dispatch_function_t work)
{
    if (!queue || !work)
        return;

    /* Allocate a reply port */
    mach_port_name_t reply = mach_port_allocate_user();
    if (reply == MACH_PORT_NULL) {
        /* Fallback: just run inline */
        work(context);
        return;
    }

    struct dispatch_work_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.msgh_bits = 0;
    msg.header.msgh_size = sizeof(msg);
    msg.header.msgh_id   = DISPATCH_MSG_WORK;
    msg.work             = work;
    msg.context          = context;
    msg.reply_port       = reply;

    mach_msg_send_user(&msg.header, sizeof(msg), queue->port);

    /* Block until reply arrives */
    char buf[64];
    mach_msg_size_t out_size;
    mach_msg_recv_user(reply, buf, sizeof(buf), &out_size);
}
