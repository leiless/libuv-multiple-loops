/*
 * libuv multiple loops + thread communication example.
 * Written by Kristian Evensen <kristian.evensen@gmail.com>
 *
 * Slightly modified by leiless
 *
 * see:
 *  https://github.com/kristrev/libuv-multiple-loops
 *  http://nikhilm.github.io/uvbook/multiple.html
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

#include <unistd.h>
#include <sys/syscall.h>

#include <uv.h>

#define UNUSED(e, ...)      (void) ((void) (e), ##__VA_ARGS__)

#define LOG(fmt, ...)       \
    (void) printf("[tid: %#lx] " fmt "\n", syscall(SYS_gettid), ##__VA_ARGS__)

/* Macro taken from macOS/Frameworks/Kernel/sys/cdefs.h */
#define __printflike(fmtarg, firstvararg) \
                __attribute__((__format__(__printf__, fmtarg, firstvararg)))

static void __assertf(int, const char *, ...) __printflike(2, 3);

/**
 * Formatted version of assert()
 *
 * @param expr  Expression to assert with
 * @param fmt   Format string when assertion failed
 * @param ...   Format string arguments
 */
static void __assertf(int expr, const char *fmt, ...)
{
    int n;
    va_list ap;

    if (!expr) {
        va_start(ap, fmt);
        n = vfprintf(stderr, fmt, ap);
        assert(n > 0);  /* Should never fail! */
        va_end(ap);

        abort();
    }
}

#define assertf(e, fmt, ...)                                        \
    __assertf(!!(e), "Assert (%s) failed: " fmt "  %s@%s()#%d\n",   \
                #e, ##__VA_ARGS__, __BASE_FILE__, __func__, __LINE__)

#define panicf(fmt, ...)            assertf(0, fmt, ##__VA_ARGS__)

#define assert_nonnull(ptr)         assertf(ptr != NULL, "")

#define __assert_cmp(v1, v2, fmt, op)   \
    assertf((v1) op (v2), "left: " fmt " right: " fmt, (v1), (typeof(v1)) (v2))

#define assert_eq(v1, v2, fmt)   __assert_cmp(v1, v2, fmt, ==)
#define assert_ne(v1, v2, fmt)   __assert_cmp(v1, v2, fmt, !=)
#define assert_le(v1, v2, fmt)   __assert_cmp(v1, v2, fmt, <=)
#define assert_ge(v1, v2, fmt)   __assert_cmp(v1, v2, fmt, >=)
#define assert_lt(v1, v2, fmt)   __assert_cmp(v1, v2, fmt, <)
#define assert_gt(v1, v2, fmt)   __assert_cmp(v1, v2, fmt, >)

static void timer_callback(uv_timer_t *handle)
{
    uv_async_t *other_thread_notifier = (uv_async_t *) handle->data;
    assert_nonnull(other_thread_notifier);
    LOG("Timer expired, notifying other thread");

    /* Notify the other thread */
    int e = uv_async_send(other_thread_notifier);
    assert_eq(e, 0, "%d");
}

static void child_thread(void *data)
{
    LOG("(Consumer thread will start event loop)");

    uv_loop_t *thread_loop = (uv_loop_t *) data;
    int e= uv_run(thread_loop, UV_RUN_DEFAULT);
    assert_eq(e, 0, "%d");

    LOG("(Consumer event loop done)");
}

static void consumer_notify(uv_async_t *handle)
{
    LOG("(Got notify from the other thread  fd: %d data: %p)\n",
            handle->loop->backend_fd, handle->data);
}

int main(int argc, char *argv[])
{
    UNUSED(argc, argv);

    uv_thread_t thread;
    uv_async_t async;
    int e;

#ifdef DEBUG
    // see: https://stackoverflow.com/questions/35239938/should-i-set-stdout-and-stdin-to-be-unbuffered-in-c
    e = setvbuf(stdout, NULL, _IONBF, 0);
    assert_eq(e, 0, "%d");
    LOG("Set stdout unbuffered");
#endif

    /* Create and set up the consumer thread */
    uv_loop_t *thread_loop = uv_loop_new();
    (void) memset(&async, 0, sizeof(async));
    e = uv_async_init(thread_loop, &async, consumer_notify);
    assert_eq(e, 0, "%d");
    e = uv_thread_create(&thread, child_thread, thread_loop);
    assert_eq(e, 0, "%d");
    LOG("thread loop fd: %d", thread_loop->backend_fd);

    /* Main thread will run default loop */
    uv_loop_t *main_loop = uv_default_loop();
    uv_timer_t timer_req;
    e = uv_timer_init(main_loop, &timer_req);
    assert_eq(e, 0, "%d");
    LOG("main loop fd: %d", main_loop->backend_fd);

    /* Timer callback needs async so it knows where to send messages */
    timer_req.data = &async;
    e = uv_timer_start(&timer_req, timer_callback, 0, 5000);
    assert_eq(e, 0, "%d");

    LOG("Starting main loop\n");
    e = uv_run(main_loop, UV_RUN_DEFAULT);
    assert_eq(e, 0, "%d");

    e = uv_thread_join(&thread);
    assert_eq(e, 0, "%d");

    e = uv_loop_close(main_loop);
    assert_eq(e, 0, "%d");

    return 0;
}

