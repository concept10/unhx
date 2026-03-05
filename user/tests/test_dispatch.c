/*
 * user/tests/test_dispatch.c — Phase 4 milestone: libdispatch test
 *
 * Verifies:
 *   1. dispatch_init + dispatch_get_main_queue
 *   2. dispatch_async_f (work item executed on worker thread)
 *   3. dispatch_queue_create (custom queue)
 *   4. dispatch_sync_f (blocks until done)
 *
 * Expected output:
 *   [dispatch] init OK
 *   [dispatch] async work executed
 *   [dispatch] dispatch_async_f OK
 *   [dispatch] sync work executed
 *   [dispatch] dispatch_sync_f OK
 *   [dispatch] custom queue work executed
 *   [dispatch] custom queue OK
 *   [dispatch] ALL TESTS PASSED
 */

#include "objc/runtime.h"
#include "Foundation.h"
#include "dispatch.h"
#include "stdio.h"

static volatile int async_done = 0;
static volatile int sync_done  = 0;
static volatile int custom_done = 0;
static int failures = 0;

static void async_work(void *ctx)
{
    (void)ctx;
    printf("[dispatch] async work executed\n");
    async_done = 1;
}

static void sync_work(void *ctx)
{
    (void)ctx;
    printf("[dispatch] sync work executed\n");
    sync_done = 1;
}

static void custom_queue_work(void *ctx)
{
    (void)ctx;
    printf("[dispatch] custom queue work executed\n");
    custom_done = 1;
}

int main(void)
{
    /* Initialize runtime + Foundation + dispatch */
    objc_runtime_init();
    foundation_init();
    dispatch_init();
    printf("[dispatch] init OK\n");

    /* 1. dispatch_async_f on main queue */
    {
        dispatch_queue_t mq = dispatch_get_main_queue();
        if (!mq) {
            printf("[dispatch] FAIL: main queue is null\n");
            failures++;
        } else {
            dispatch_async_f(mq, 0, async_work);

            /* Spin-wait for async completion (worker thread will execute it) */
            volatile int spin = 0;
            while (!async_done && spin < 1000000)
                spin++;

            if (async_done)
                printf("[dispatch] dispatch_async_f OK\n");
            else {
                printf("[dispatch] FAIL: dispatch_async_f timed out\n");
                failures++;
            }
        }
    }

    /* 2. dispatch_sync_f on main queue */
    {
        dispatch_queue_t mq = dispatch_get_main_queue();
        dispatch_sync_f(mq, 0, sync_work);

        if (sync_done)
            printf("[dispatch] dispatch_sync_f OK\n");
        else {
            printf("[dispatch] FAIL: dispatch_sync_f\n");
            failures++;
        }
    }

    /* 3. Custom queue */
    {
        dispatch_queue_t q = dispatch_queue_create("com.unhox.test", 0);
        if (!q) {
            printf("[dispatch] FAIL: queue_create returned null\n");
            failures++;
        } else {
            dispatch_sync_f(q, 0, custom_queue_work);

            if (custom_done)
                printf("[dispatch] custom queue OK\n");
            else {
                printf("[dispatch] FAIL: custom queue work not executed\n");
                failures++;
            }
        }
    }

    /* Summary */
    if (failures == 0)
        printf("[dispatch] ALL TESTS PASSED\n");
    else
        printf("[dispatch] %d TESTS FAILED\n", failures);

    return failures;
}
