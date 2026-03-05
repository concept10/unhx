/*
 * frameworks/libdispatch/dispatch_group.c — Dispatch groups
 *
 * Atomic counter + notify port. enter increments, leave decrements
 * (sends notify on zero). wait blocks on notify port.
 */

#include "dispatch.h"
#include "mach_msg.h"
#include "malloc.h"
#include "string.h"

#define GROUP_NOTIFY_MSG_ID 0x4500

struct dispatch_group_s {
    int32_t               count;         /* enter/leave counter */
    mach_port_name_t      notify_port;   /* port signaled on count→0 */
    /* Pending notify callback */
    dispatch_queue_t      notify_queue;
    dispatch_function_t   notify_work;
    void                 *notify_context;
    BOOL                  has_notify;
};

dispatch_group_t dispatch_group_create(void)
{
    struct dispatch_group_s *g = (struct dispatch_group_s *)
        calloc(1, sizeof(*g));
    if (!g) return 0;

    g->count       = 0;
    g->notify_port = mach_port_allocate_user();
    g->has_notify  = NO;
    return g;
}

void dispatch_group_enter(dispatch_group_t group)
{
    if (!group) return;
    group->count++;
}

void dispatch_group_leave(dispatch_group_t group)
{
    if (!group) return;
    group->count--;

    if (group->count <= 0) {
        /* Signal the notify port */
        mach_msg_header_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.msgh_bits = 0;
        msg.msgh_size = sizeof(msg);
        msg.msgh_id   = GROUP_NOTIFY_MSG_ID;
        mach_msg_send_user(&msg, sizeof(msg), group->notify_port);

        /* Fire notify callback if registered */
        if (group->has_notify && group->notify_work) {
            dispatch_async_f(group->notify_queue,
                           group->notify_context,
                           group->notify_work);
            group->has_notify = NO;
        }
    }
}

void dispatch_group_wait(dispatch_group_t group)
{
    if (!group) return;
    if (group->count <= 0) return;

    /* Block on notify port */
    char buf[64];
    mach_msg_size_t out_size;
    mach_msg_recv_user(group->notify_port, buf, sizeof(buf), &out_size);
}

void dispatch_group_notify_f(dispatch_group_t group,
                              dispatch_queue_t queue,
                              void *context, dispatch_function_t work)
{
    if (!group || !queue || !work)
        return;

    if (group->count <= 0) {
        /* Already done — fire immediately */
        dispatch_async_f(queue, context, work);
        return;
    }

    group->notify_queue   = queue;
    group->notify_work    = work;
    group->notify_context = context;
    group->has_notify     = YES;
}

/* Convenience: group_async wraps function in enter/leave */
struct group_async_ctx {
    dispatch_group_t    group;
    dispatch_function_t work;
    void               *context;
};

static void group_async_wrapper(void *raw)
{
    struct group_async_ctx *ctx = (struct group_async_ctx *)raw;
    ctx->work(ctx->context);
    dispatch_group_leave(ctx->group);
    free(ctx);
}

void dispatch_group_async_f(dispatch_group_t group,
                             dispatch_queue_t queue,
                             void *context, dispatch_function_t work)
{
    if (!group || !queue || !work)
        return;

    struct group_async_ctx *ctx = (struct group_async_ctx *)
        malloc(sizeof(*ctx));
    if (!ctx) return;

    ctx->group   = group;
    ctx->work    = work;
    ctx->context = context;

    dispatch_group_enter(group);
    dispatch_async_f(queue, ctx, group_async_wrapper);
}
