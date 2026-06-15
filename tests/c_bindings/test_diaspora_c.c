/*
 * test_diaspora_c.c — smoke test for the diaspora_c C bindings.
 *
 * Pure C (compiled with the C compiler, not C++). Exercises the
 * full happy path against whatever driver the test environment
 * provides via env vars:
 *
 *   DIASPORA_C_TEST_DRIVER_NAME   e.g. "mofka" or "files"
 *   DIASPORA_C_TEST_DRIVER_JSON   driver options as a JSON string,
 *                                 e.g. {"group_file":"/path/to/mofka.json"}
 *   DIASPORA_C_TEST_TOPIC_NAME    topic to push into (e.g. "test_c_bindings")
 *
 * Behaviour:
 *   1. create driver, open topic, create producer
 *   2. push 100 metadata-only events
 *   3. push 10 events with a small data payload each
 *   4. flush with 10s timeout
 *   5. destroy producer, topic, driver
 *
 * Verdict:
 *   exit 0  -> all 110 pushes returned DIASPORA_C_OK + flush returned OK
 *   exit 1  -> any push failed; diaspora_c_last_error() printed to stderr
 *   exit 2  -> setup failure (driver/topic/producer creation)
 *   exit 3  -> flush returned TIMEOUT (broker unresponsive or too slow)
 *   exit 4  -> flush returned ERR
 *
 * The driver-side LIVE check (whether events actually landed at the
 * broker) is out of scope for this test -- that's the host project's
 * (e.g. Darshan's p04_drain_confirm.sh) job. This test confirms only
 * that the binding surface itself does not crash, does not throw across
 * the C boundary, and that the documented return codes happen.
 *
 * Compile: this is built with the C compiler (set in tests/c_bindings/
 * CMakeLists.txt) and linked against diaspora-c (which transitively
 * pulls libstdc++ and the C++ diaspora-stream-api).
 */

#include <diaspora/diaspora_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_META_ONLY  100
#define N_WITH_DATA   10
#define PAYLOAD_SIZE  64
#define FLUSH_MS   10000

static void
die(int rc, const char *where)
{
    fprintf(stderr, "[test_diaspora_c] FAIL at %s: %s\n",
            where, diaspora_c_last_error());
    exit(rc);
}

int
main(void)
{
    const char *driver_name = getenv("DIASPORA_C_TEST_DRIVER_NAME");
    const char *driver_json = getenv("DIASPORA_C_TEST_DRIVER_JSON");
    const char *topic_name  = getenv("DIASPORA_C_TEST_TOPIC_NAME");

    if (!driver_name || !topic_name) {
        fprintf(stderr,
                "[test_diaspora_c] SKIP -- need env vars: "
                "DIASPORA_C_TEST_DRIVER_NAME, DIASPORA_C_TEST_TOPIC_NAME"
                " (and optionally DIASPORA_C_TEST_DRIVER_JSON)\n");
        /* CTest convention: nonzero with a known message means skip,
         * but most simple harnesses treat any nonzero as failure. We
         * exit 77 (autotools skip convention) -- CMake's SKIP_RETURN_CODE
         * is wired in CMakeLists.txt below. */
        return 77;
    }

    fprintf(stderr, "[test_diaspora_c] driver=%s topic=%s opts=%s\n",
            driver_name, topic_name,
            driver_json ? driver_json : "(default: {})");

    /* --- 1. create driver / topic / producer ----------------------- */
    diaspora_driver_t *drv = diaspora_driver_create(driver_name, driver_json);
    if (!drv) die(2, "driver_create");

    diaspora_topic_t *top = diaspora_topic_open(drv, topic_name);
    if (!top) die(2, "topic_open");

    /* batch_size=0 (adaptive), max_num_batches=0 (library default),
     * Loose ordering -- the recommended defaults for telemetry. */
    diaspora_producer_t *prod = diaspora_producer_create(
        top, "diaspora-c-smoketest", 0, 0, DIASPORA_C_ORDERING_LOOSE);
    if (!prod) die(2, "producer_create");

    /* --- 2. push 100 metadata-only events --------------------------- */
    for (int i = 0; i < N_META_ONLY; i++) {
        char meta[160];
        snprintf(meta, sizeof(meta),
                 "{\"kind\":\"meta_only\",\"seq\":%d,\"src\":\"test_diaspora_c\"}",
                 i);
        int rc = diaspora_producer_push(prod, meta, NULL, 0);
        if (rc != DIASPORA_C_OK) {
            fprintf(stderr, "[test_diaspora_c] push meta #%d rc=%d\n", i, rc);
            die(1, "push(metadata-only)");
        }
    }
    fprintf(stderr, "[test_diaspora_c] pushed %d metadata-only events\n",
            N_META_ONLY);

    /* --- 3. push 10 events with data payload ------------------------ */
    unsigned char payload[PAYLOAD_SIZE];
    for (size_t i = 0; i < PAYLOAD_SIZE; i++) payload[i] = (unsigned char)i;

    for (int i = 0; i < N_WITH_DATA; i++) {
        char meta[160];
        snprintf(meta, sizeof(meta),
                 "{\"kind\":\"with_data\",\"seq\":%d,\"len\":%d}",
                 i, PAYLOAD_SIZE);
        int rc = diaspora_producer_push(prod, meta, payload, PAYLOAD_SIZE);
        if (rc != DIASPORA_C_OK) {
            fprintf(stderr, "[test_diaspora_c] push data #%d rc=%d\n", i, rc);
            die(1, "push(with-data)");
        }
    }
    fprintf(stderr, "[test_diaspora_c] pushed %d events with %d-byte payload\n",
            N_WITH_DATA, PAYLOAD_SIZE);

    /* Total: 110 pushes (per master plan P2.6). */

    /* --- 4. flush with bounded timeout ------------------------------ */
    int frc = diaspora_producer_flush_timeout(prod, FLUSH_MS);
    if (frc == DIASPORA_C_TIMEOUT) {
        fprintf(stderr,
                "[test_diaspora_c] flush TIMEOUT after %d ms -- broker "
                "unresponsive or topic backlogged\n", FLUSH_MS);
        /* Per the C header's shutdown guidance: if flush timed out and
         * we're about to exit, prefer leaking (skip destroys). For the
         * test we DO call destroy() to exercise the path -- but if it
         * hangs, that's a known mofka-side teardown issue (S1b in the
         * darshan project's notes). */
        diaspora_producer_destroy(prod);
        diaspora_topic_destroy(top);
        diaspora_driver_destroy(drv);
        return 3;
    }
    if (frc == DIASPORA_C_ERR) {
        diaspora_producer_destroy(prod);
        diaspora_topic_destroy(top);
        diaspora_driver_destroy(drv);
        die(4, "flush(ERR)");
    }
    fprintf(stderr, "[test_diaspora_c] flush OK\n");

    /* --- 5. destroy ------------------------------------------------- */
    diaspora_producer_destroy(prod);
    diaspora_topic_destroy(top);
    diaspora_driver_destroy(drv);

    fprintf(stderr,
            "[test_diaspora_c] PASS -- %d pushes (%d meta-only + %d with data),"
            " flush OK, destroyed cleanly\n",
            N_META_ONLY + N_WITH_DATA, N_META_ONLY, N_WITH_DATA);
    return 0;
}
