/*
 * (C) 2026 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

/* diaspora_c.h — C bindings for the Diaspora Stream API (producer side).
 *
 * STATUS: EXPERIMENTAL (v0, pre-1.0). ABI may change between minor releases.
 *
 * The Diaspora Stream API is C++17; long-lived C codebases (e.g. Darshan)
 * cannot link it directly. This is a minimal, driver-agnostic C facade: one
 * set of bindings gives any C program access to every Diaspora driver
 * (mofka, files, kafka, ...), selected by name at runtime.
 *
 * Scope (v0): driver / topic / producer lifecycle, push, bounded flush.
 * The consumer API is out of scope for v0.
 *
 * Invariants:
 *   - No C++ exception ever crosses this boundary; errors surface as NULL
 *     returns or negative codes, with detail via diaspora_c_last_error().
 *   - No diaspora C++ type appears in this header.
 *
 * Typical use:
 *   diaspora_driver_t*   d = diaspora_driver_create("mofka", opts_json);
 *   diaspora_topic_t*    t = diaspora_topic_open(d, "my-topic");
 *   diaspora_producer_t* p = diaspora_producer_create(t, "app", 0, 0,
 *                                                     DIASPORA_C_ORDERING_LOOSE);
 *   diaspora_producer_push(p, "{\"k\":1}", NULL, 0);
 *   diaspora_producer_flush_timeout(p, -1);
 *   diaspora_producer_destroy(p);
 *   diaspora_topic_destroy(t);
 *   diaspora_driver_destroy(d);
 */
#ifndef DIASPORA_API_C_H
#define DIASPORA_API_C_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIASPORA_C_API_VERSION 1

/* ---- return codes ---------------------------------------------------- */
#define DIASPORA_C_OK       0
#define DIASPORA_C_TIMEOUT  1   /* flush deadline elapsed; data may be lost */
#define DIASPORA_C_ERR    (-1)  /* see diaspora_c_last_error()             */

/* ---- opaque handles --------------------------------------------------- */
typedef struct diaspora_driver   diaspora_driver_t;
typedef struct diaspora_topic    diaspora_topic_t;
typedef struct diaspora_producer diaspora_producer_t;

typedef enum {
    DIASPORA_C_ORDERING_LOOSE  = 0,  /* recommended for telemetry          */
    DIASPORA_C_ORDERING_STRICT = 1   /* limits producer parallelism        */
} diaspora_c_ordering_t;

/* Last error for the calling thread. Never NULL ("" if no error). Valid
 * until the next failing diaspora_c call on the same thread. */
const char* diaspora_c_last_error(void);

/* ---- driver ------------------------------------------------------------
 * driver_name:  e.g. "mofka". Resolved by the Diaspora driver factory.
 * options_json: driver-specific JSON object, e.g.
 *               {"group_file":"/path/to/mofka.json"}. NULL means "{}".
 * Returns NULL on failure (broker unreachable, unknown driver, ...).
 * Creation performs network I/O and may take O(100ms). */
diaspora_driver_t* diaspora_driver_create(const char* driver_name,
                                          const char* options_json);
void diaspora_driver_destroy(diaspora_driver_t* d);

/* ---- topic ------------------------------------------------------------ */
diaspora_topic_t* diaspora_topic_open(diaspora_driver_t* d,
                                      const char* topic_name);
void diaspora_topic_destroy(diaspora_topic_t* t);

/* ---- producer ----------------------------------------------------------
 * batch_size:      0 = adaptive batching (recommended); N>0 = fixed size.
 * max_num_batches: 0 = library default. Bounds client memory
 *                  AND sets backpressure: push() MAY BLOCK while this many
 *                  batches are pending on an unresponsive broker.
 * producer_name:   informational; give all producers of one application the
 *                  same name (per Diaspora docs). NULL = "diaspora-c".
 */
diaspora_producer_t* diaspora_producer_create(diaspora_topic_t* t,
                                              const char* producer_name,
                                              size_t batch_size,
                                              size_t max_num_batches,
                                              diaspora_c_ordering_t ordering);
void diaspora_producer_destroy(diaspora_producer_t* p);

/* Push one event. Fire-and-forget: returns once the event is enqueued in
 * the local batch (no broker round-trip on the happy path).
 *
 * metadata_json: required JSON object. Serialized immediately — the caller's
 *                buffer may be reused on return.
 * data/data_len: OPTIONAL bulk payload ((NULL,0) for metadata-only).
 *                NON-OWNING and may be sent asynchronously (RDMA): the buffer
 *                MUST stay valid and unmodified until a subsequent
 *                diaspora_producer_flush_timeout() returns DIASPORA_C_OK.
 */
int diaspora_producer_push(diaspora_producer_t* p,
                           const char* metadata_json,
                           const void* data, size_t data_len);

/* Like diaspora_producer_push, but metadata_json is stored VERBATIM (not parsed):
 * the bytes are carried as-is and, when the topic uses the "raw" serializer, written
 * straight to the wire without a parse+dump round-trip. The consumer still receives a
 * parsed JSON object. metadata_json must already be a well-formed JSON object; it is
 * not validated here. Same fire-and-forget / buffer-reuse semantics as _push. */
int diaspora_producer_push_raw(diaspora_producer_t* p,
                               const char* metadata_json,
                               const void* data, size_t data_len);

/* Age, in seconds, of the OLDEST push not yet acknowledged by the broker;
 * 0.0 if nothing is pending; -1.0 on internal error. Non-blocking.
 *
 * For stall detection: push() can block once max_num_batches are pending on
 * an unresponsive broker, so a caller may poll this and back off (its own
 * policy) when the age exceeds a threshold. One future is tracked at a time,
 * replaced only after it completes. */
double diaspora_producer_oldest_pending_age(diaspora_producer_t* p);

/* Drain all pending batches. timeout_ms < 0 blocks until complete.
 * Returns DIASPORA_C_OK, DIASPORA_C_TIMEOUT, or DIASPORA_C_ERR.
 *
 * Shutdown note: if this returns TIMEOUT and the process is about to exit,
 * prefer LEAKING the handles (skip the destroys) — destructors may re-contact
 * a dead broker and hang exit; the OS reclaims everything anyway. */
int diaspora_producer_flush_timeout(diaspora_producer_t* p, int timeout_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DIASPORA_API_C_H */
