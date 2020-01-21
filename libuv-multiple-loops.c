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
#include <pthread.h>
#include <sys/syscall.h>

#include <uv.h>

#define UNUSED(e, ...)      (void) ((void) (e), ##__VA_ARGS__)

#define LOG(fmt, ...)                                       \
    (void) printf("[thread: %#lx tid: %ld] " fmt "\n",      \
        uv_thread_self(), syscall(SYS_gettid), ##__VA_ARGS__)

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

/* bclr stands for byte clear */
#define bclr(ptr, size)          (void) memset(ptr, 0, size)
#define bclr_sizeof(ptr)         bclr(ptr, sizeof(*(ptr)))

typedef struct {
    uv_loop_t loop;
    uv_async_t async;
} loop_async_t;

static void timer_cb(uv_timer_t *handle)
{
    int e;
    loop_async_t *la;

    la = (loop_async_t *) handle->data;
    assert_nonnull(la);

    LOG("Timer expired, async send to thread");

    la->async.data = la;
    /* Notify the other thread */
    e = uv_async_send(&la->async);
    assert_eq(e, 0, "%d");
}

static void thread_entry(void *data)
{
    LOG("(Consumer thread going to run event loop)");

    uv_loop_t *thread_loop = (uv_loop_t *) data;
    assert_nonnull(thread_loop);
    int e = uv_run(thread_loop, UV_RUN_DEFAULT);
    assert_eq(e, 0, "%d");

    LOG("(Consumer event loop done)");
}

static void work_cb(uv_work_t* req)
{
    assert_nonnull(req);
    LOG("<New thread from threadpool>");

    /* see: libuv/src/unix/thread.c */
    pthread_exit(NULL);
}

static void done_work_cb(uv_work_t* req, int status)
{
    UNUSED(req);
    LOG("done work called  status: %d", status);
}

static void async_cb(uv_async_t *handle)
{
    int e;
    loop_async_t *la;
    uv_work_t req;

    LOG("(Going to enqueue a work)");

    assert_nonnull(handle);
    la = (loop_async_t *) handle->data;
    assert_nonnull(la);

    bclr_sizeof(&req);
    e = uv_queue_work(&la->loop, &req, work_cb, done_work_cb);
    assert_eq(e, 0, "%d");

    LOG("(Work enqueued)");
}

int main(void)
{
    int e;

#ifdef DEBUG
    // see: https://stackoverflow.com/questions/35239938/should-i-set-stdout-and-stdin-to-be-unbuffered-in-c
    e = setvbuf(stdout, NULL, _IONBF, 0);
    assert_eq(e, 0, "%d");
    LOG("Set stdout unbuffered");
#endif

    LOG("libuv version: %s", uv_version_string());

    e = setenv("UV_THREADPOOL_SIZE", "128", 1);
    assertf(e == 0, "setenv() fail  errno: %d", errno);
    LOG("UV_THREADPOOL_SIZE: %s", getenv("UV_THREADPOOL_SIZE"));

    loop_async_t la;
    bclr_sizeof(&la);

    e = uv_loop_init(&la.loop);
    assert_eq(e, 0, "%d");
    LOG("thread loop fd: %d", la.loop.backend_fd);

    e = uv_async_init(&la.loop, &la.async, async_cb);
    assert_eq(e, 0, "%d");

    uv_thread_t thread;
    e = uv_thread_create(&thread, thread_entry, &la.loop);
    assert_eq(e, 0, "%d");

    /* Main thread will run default loop */
    uv_loop_t *main_loop = uv_default_loop();
    uv_timer_t timer_req;
    e = uv_timer_init(main_loop, &timer_req);
    assert_eq(e, 0, "%d");
    LOG("main loop fd: %d", main_loop->backend_fd);

    /* Timer callback needs async so it knows where to send messages */
    timer_req.data = &la;
    e = uv_timer_start(&timer_req, timer_cb, 0, 1000);
    assert_eq(e, 0, "%d");

    LOG("Starting main loop\n");
    e = uv_run(main_loop, UV_RUN_DEFAULT);
    assert_eq(e, 0, "%d");
    LOG("Main loop done");

    e = uv_thread_join(&thread);
    assert_eq(e, 0, "%d");

    e = uv_loop_close(main_loop);
    assert_eq(e, 0, "%d");

    return 0;
}

