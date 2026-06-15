/* diaspora_c.h — C bindings for the Diaspora Stream API (producer side).
 *
 * STATUS: EXPERIMENTAL (v0, pre-1.0). ABI may change between minor releases.
 *
 * Rationale: the Diaspora Stream API is C++17. Long-lived C codebases
 * (instrumentation libraries such as Darshan, legacy simulation codes)
 * cannot link a C++ API directly. This header is a minimal, driver-agnostic
 * C facade: one set of bindings gives every C program access to every
 * Diaspora driver (mofka, files, kafka, ...) selected by name at runtime.
 *
 * Scope (v0): driver / topic / producer lifecycle, push, bounded flush.
 * Consumer API is deliberately OUT OF SCOPE for v0.
 *
 * Invariants:
 *   - No C++ exception ever crosses this boundary. Errors surface as
 *     NULL returns or negative codes, with diaspora_c_last_error().
 *   - No diaspora C++ type appears in this header.
 */
#ifndef DIASPORA_C_H
#define DIASPORA_C_H

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

/* Last error message for the calling thread. Never NULL ("" if no error).
 * The pointer is valid until the next failing diaspora_c call on the same
 * thread. */
const char* diaspora_c_last_error(void);

/* ---- driver ------------------------------------------------------------
 * driver_name:  e.g. "mofka". Resolved by the Diaspora driver factory.
 * options_json: driver-specific options as a JSON object, e.g.
 *               {"group_file":"/path/to/mofka.json"}. NULL means "{}".
 * Returns NULL on failure (broker unreachable, unknown driver, ...).
 * NOTE: creation performs network I/O and may take O(100ms); on an
 * unreachable broker it fails within the transport's retry window. */
diaspora_driver_t* diaspora_driver_create(const char* driver_name,
                                          const char* options_json);
void diaspora_driver_destroy(diaspora_driver_t* d);

/* ---- topic ------------------------------------------------------------ */
diaspora_topic_t* diaspora_topic_open(diaspora_driver_t* d,
                                      const char* topic_name);
void diaspora_topic_destroy(diaspora_topic_t* t);

/* ---- producer ----------------------------------------------------------
 * batch_size:      0 = adaptive batching (recommended); N>0 = fixed size.
 * max_num_batches: 0 = library default (currently 2). This bounds client
 *                  memory AND is the backpressure limit: push() MAY BLOCK
 *                  while this many batches are pending on an unresponsive
 *                  broker. Size it accordingly for hot paths.
 * producer_name:   informational; give all producers of one application
 *                  the same name (per Diaspora docs). NULL = "diaspora-c".
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
 * metadata_json: required; a JSON object. Serialized into the batch
 *                immediately — the caller's buffer may be reused on return.
 * data/data_len: OPTIONAL bulk payload ((NULL,0) for metadata-only).
 *                NON-OWNING, may be transferred asynchronously (RDMA):
 *                the buffer MUST remain valid and unmodified until a
 *                subsequent diaspora_producer_flush_timeout() returns
 *                DIASPORA_C_OK. (Same contract as C++ DataView.)
 */
int diaspora_producer_push(diaspora_producer_t* p,
                           const char* metadata_json,
                           const void* data, size_t data_len);

/* Age, in seconds, of the OLDEST push on this producer that has not yet
 * been acknowledged by the broker; 0.0 if nothing is pending; -1.0 on
 * internal error (see diaspora_c_last_error()). Non-blocking.
 *
 * Purpose: instrumentation-grade stall detection. push() can block once
 * max_num_batches batches are pending against an unresponsive broker; a
 * caller can poll this before pushing and stop pushing (its own policy)
 * when the age exceeds its threshold. Tracking is adopt-oldest: one
 * future is tracked at a time, replaced only after it completes. */
double diaspora_producer_oldest_pending_age(diaspora_producer_t* p);

/* Drain all pending batches. timeout_ms < 0 blocks until complete.
 * Returns DIASPORA_C_OK, DIASPORA_C_TIMEOUT, or DIASPORA_C_ERR.
 *
 * Shutdown guidance (learned the hard way in Darshan): if this returns
 * TIMEOUT and the process is about to exit, prefer LEAKING the handles
 * (skip the destroys) — destructors may re-contact a dead broker and
 * hang process exit. The OS reclaims everything at exit anyway. */
int diaspora_producer_flush_timeout(diaspora_producer_t* p, int timeout_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DIASPORA_C_H */
