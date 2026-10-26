/* SPDX-License-Identifier: GPL-3.0-only
 * Host publication/ownership test for the MONO2K worker protocol.
 * cc -std=c11 -O1 -g -fsanitize=address,undefined -pthread
 * tests/audio_worker_test.c -o /tmp/audio_worker_test
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

static _Atomic uint32_t start_worker, stop_worker, entered, release_job;
static uint32_t ticks(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint32_t)t.tv_nsec / 1000000u);
}
static void wait_flag(_Atomic uint32_t *flag)
{
    uint32_t start = ticks();
    while (!atomic_load_explicit(flag, memory_order_acquire))
        assert((uint32_t)(ticks() - start) < 5000u);
}
static void idle(void)
{
    if (atomic_load_explicit(&stop_worker, memory_order_acquire))
        pthread_exit(0);
}
#define AW_WORD _Atomic uint32_t
#define AW_LOAD(p) atomic_load_explicit((p), memory_order_acquire)
#define AW_STORE(p,v) atomic_store_explicit((p), (v), memory_order_release)
#define AW_TICKS() ticks()
#define AW_TIMEOUT_TICKS 5000u
#define AW_FAULT() assert(!"worker timeout")
#define AW_RAM_LOOP
#define AW_IDLE() idle()
#include "../firmware/src/audio_worker.h"

struct payload {
    uint32_t sequence, calls, output[256];
    int hold;
};
static void job(void *context)
{
    struct payload *p = context;
    assert(p->sequence == p->calls + 1u);
    p->calls++;
    if (p->hold) {
        AW_STORE(&entered, 1);
        wait_flag(&release_job);
    }
    for (unsigned i = 0; i < 256; i++)
        p->output[i] = p->output[i] * 1664525u + 1013904223u + p->sequence;
}
static void *run(void *unused)
{
    (void)unused;
    wait_flag(&start_worker);
    audio_worker_loop();
    return 0;
}
int main(void)
{
    pthread_t thread;
    struct payload actual = {0}, rejected = {0};
    uint32_t expected[256] = {0};
    assert(pthread_create(&thread, 0, run, 0) == 0);
    /* Platform contract: online remains false until the ready handshake.
     * Hold startup so this test cannot accidentally run after readiness. */
    assert(!AW_LOAD(&audio_worker.ready));
    assert(!audio_worker_submit(job, &actual));
    assert(!AW_LOAD(&audio_worker.request) && actual.calls == 0);
    AW_STORE(&start_worker, 1);
    wait_flag(&audio_worker.ready);
    assert(!audio_worker_submit(job, &actual)); /* ready alone is not online */
    audio_worker_online = 1;
    for (unsigned n = 1; n <= 10000; n++) {
        actual.sequence = n;
        actual.hold = n == 1;
        assert(audio_worker_submit(job, &actual));
        if (n == 1) {
            wait_flag(&entered);
            assert(!audio_worker_submit(job, &rejected));
            assert(audio_worker.context == &actual);
            assert(AW_LOAD(&audio_worker.request) == 1);
            assert(AW_LOAD(&audio_worker.complete) == 0);
            AW_STORE(&release_job, 1);
        }
        audio_worker_join();
        assert(actual.calls == n && rejected.calls == 0);
        for (unsigned i = 0; i < 256; i++) {
            expected[i] = expected[i] * 1664525u + 1013904223u + n;
            assert(actual.output[i] == expected[i]);
        }
        assert(audio_worker.jobs == n);
    }
    assert(!audio_worker.timeouts);
    assert(AW_LOAD(&audio_worker.request) == AW_LOAD(&audio_worker.complete));
    audio_worker_online = 0;
    assert(!audio_worker_submit(job, &rejected));
    AW_STORE(&stop_worker, 1);
    assert(pthread_join(thread, 0) == 0);
    puts("worker: 10000 jobs exactly once; publication, startup, busy/offline refusal pass");
    return 0;
}
