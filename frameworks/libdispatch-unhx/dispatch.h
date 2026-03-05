/*
 * frameworks/libdispatch/dispatch.h — Grand Central Dispatch API
 *
 * Mach port-backed queues. Each queue owns a Mach port; worker threads
 * receive work items as Mach messages.
 */

#ifndef DISPATCH_H
#define DISPATCH_H

#include <stdint.h>
#include <stddef.h>

#ifndef BOOL
typedef int8_t BOOL;
#define YES ((BOOL)1)
#define NO  ((BOOL)0)
#endif

typedef struct dispatch_queue_s   *dispatch_queue_t;
typedef struct dispatch_group_s   *dispatch_group_t;
typedef void (*dispatch_function_t)(void *);

/* Initialize dispatch subsystem (call after foundation_init) */
void dispatch_init(void);

/* Queue API */
dispatch_queue_t dispatch_get_main_queue(void);
dispatch_queue_t dispatch_queue_create(const char *label, void *attr);

void dispatch_async_f(dispatch_queue_t queue,
                      void *context, dispatch_function_t work);
void dispatch_sync_f(dispatch_queue_t queue,
                     void *context, dispatch_function_t work);

/* Group API */
dispatch_group_t dispatch_group_create(void);
void dispatch_group_enter(dispatch_group_t group);
void dispatch_group_leave(dispatch_group_t group);
void dispatch_group_wait(dispatch_group_t group);
void dispatch_group_notify_f(dispatch_group_t group,
                              dispatch_queue_t queue,
                              void *context, dispatch_function_t work);
void dispatch_group_async_f(dispatch_group_t group,
                             dispatch_queue_t queue,
                             void *context, dispatch_function_t work);

#endif /* DISPATCH_H */
