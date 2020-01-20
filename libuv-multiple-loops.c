/*
 * libuv multiple loops + thread communication example.
 * Written by Kristian Evensen <kristian.evensen@gmail.com>
 *
 * see:
 *  https://github.com/kristrev/libuv-multiple-loops
 *  http://nikhilm.github.io/uvbook/multiple.html
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <sys/syscall.h>

#include <uv.h>

#define LOG(fmt, ...)       \
    (void) printf("[tid: %#lx] " fmt "\n", syscall(SYS_gettid), ##__VA_ARGS__)

#define assert_nonnull(ptr)         assert(ptr != NULL)
#define assert_eq(v1, v2)           assert(v1 == v2)

typedef struct {
    uv_loop_t loop;
    uv_async_t async;
} loop_async_t;

static void work_cb(uv_work_t* req)
{
    assert_nonnull(req);
    LOG("threadpoll called  %#lx", uv_thread_self());
}

static void timer_cb(uv_timer_t *handle)
{
    int e;

    loop_async_t *la = (loop_async_t *) handle->data;
    assert_nonnull(la);
    LOG("Timer expired, enqueue a work.");

    uv_work_t req;
    (void) memset(&req, 0, sizeof(req));
    req.data = NULL;
    e = uv_queue_work(&la->loop, &req, work_cb, NULL);
    assert_eq(e, 0);
}

static void thread_entry(void *data)
{
    LOG("(Thread will start event loop)");

    uv_loop_t *thread_loop = (uv_loop_t *) data;
    int e = uv_run(thread_loop, UV_RUN_DEFAULT);
    assert_eq(e, 0);

    LOG("(Thread event loop done)");
}

int main(void)
{
    int e;

    // see: https://stackoverflow.com/questions/35239938/should-i-set-stdout-and-stdin-to-be-unbuffered-in-c
    e = setvbuf(stdout, NULL, _IONBF, 0);
    assert_eq(e, 0);
    LOG("Set stdout unbuffered");

    loop_async_t la;

    e = uv_loop_init(&la.loop);
    assert_eq(e, 0);
    LOG("thread loop fd: %d", la.loop.backend_fd);

    (void) memset(&la.async, 0, sizeof(la.async));
    e = uv_async_init(&la.loop, &la.async, NULL);
    assert_eq(e, 0);

    uv_thread_t thread;
    e = uv_thread_create(&thread, thread_entry, &la.loop);
    assert_eq(e, 0);

    /* Main thread will run default loop */
    uv_loop_t *main_loop = uv_default_loop();
    uv_timer_t timer_req;
    e = uv_timer_init(main_loop, &timer_req);
    assert_eq(e, 0);
    LOG("main loop fd: %d", main_loop->backend_fd);

    /* Timer callback needs async so it knows where to send messages */
    timer_req.data = &la;
    e = uv_timer_start(&timer_req, timer_cb, 0, 1000);
    assert_eq(e, 0);

    LOG("Starting main loop\n");
    e = uv_run(main_loop, UV_RUN_DEFAULT);
    assert_eq(e, 0);

    e = uv_thread_join(&thread);
    assert_eq(e, 0);

    e = uv_loop_close(main_loop);
    assert_eq(e, 0);

    return 0;
}

