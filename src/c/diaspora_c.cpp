/* diaspora_c.cpp — implementation of the Diaspora Stream API C bindings.
 *
 * Ported (generalized) from a runtime-proven integration: the darshan-mofka
 * module's darshan-mofka-impl.cpp, which exercised driver/topic/producer
 * setup, metadata push under load, and flush-with-timeout against live
 * bedrock daemons on ANL Bebop. Everything driver- or schema-specific was
 * removed; this file knows nothing about any particular client.
 *
 * VERIFICATION MAP — every "VERIFY-n" below must be checked against the
 * actual headers before first build. Ground truth locations:
 *   A) cloned repo:  ~/upstream/diaspora-stream-api/include/diaspora/*.hpp
 *   B) installed:    $SPACK_VIEW/include/diaspora/*.hpp
 *   C) usage truth:  ~/upstream/diaspora-stream-api/bin/  (diaspora-ctl
 *      creates drivers BY NAME — mirror exactly what it does)
 *   D) docs:         ~/diaspora-stream-api-docs/docs/usage/*.rst
 * See VERIFY_CHECKLIST.md for the per-marker procedure.
 */

#include <diaspora/diaspora_c.h>      /* VERIFY-0: include path/style per
                                         repo convention once placed under
                                         include/diaspora/ */

/* VERIFY-1: exact header names. Known-good from the darshan build:
 * BatchParams.hpp holds BatchSize and MaxNumBatches (NOT BatchSize.hpp —
 * that mistake cost an hour once). The rest are best-effort; `ls` the
 * include dir and fix. Ordering may live in its own header or inside
 * Producer/TopicHandle. DataView may be Data.hpp or DataView.hpp. */
#include <diaspora/Driver.hpp>
#include <diaspora/TopicHandle.hpp>
#include <diaspora/Producer.hpp>
#include <diaspora/Metadata.hpp>
#include <diaspora/DataView.hpp>
#include <diaspora/BatchParams.hpp>
#include <diaspora/Ordering.hpp>

#include <chrono>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

/* ---------------------------------------------------------------------- */
/* thread-local error channel                                              */
/* ---------------------------------------------------------------------- */
namespace {

thread_local std::string g_last_error;

void set_err(const char* where, const std::exception* e) noexcept {
    try {
        g_last_error.assign(where);
        g_last_error += ": ";
        g_last_error += e ? e->what() : "unknown (non-std exception)";
    } catch (...) { /* never throw from the error path */ }
}

} /* namespace */

extern "C" const char* diaspora_c_last_error(void) {
    return g_last_error.c_str();
}

/* ---------------------------------------------------------------------- */
/* opaque handle definitions                                               */
/* VERIFY-2: the API-layer types. Diaspora handle types (TopicHandle,      */
/* Producer) are value types with shared_ptr internals — copy/move is      */
/* cheap and safe (Future.hpp shows the same pattern: defaulted copy/move).*/
/* Confirm the driver-side type: is the factory result diaspora::Driver,   */
/* std::shared_ptr<diaspora::DriverInterface>, or similar? Adjust the      */
/* member type to WHATEVER THE FACTORY RETURNS — use auto + decltype if    */
/* unsure.                                                                 */
/* ---------------------------------------------------------------------- */
struct diaspora_driver   { diaspora::Driver      impl; };
struct diaspora_topic    { diaspora::TopicHandle impl; };
struct diaspora_producer {
    diaspora::Producer impl;
    /* Oldest-unacked-push tracking for diaspora_producer_oldest_pending_age.
     * Rules (hard-won in the darshan integration):
     *  - adopt a future only when none is tracked (tracks the OLDEST);
     *  - guard completed() behind the Future's operator bool — calling
     *    completed() on an invalid Future THROWS (Future.hpp);
     *  - never perform I/O while holding the mutex.
     * VERIFY-8: the exact Future specialization push() returns —
     * Future<std::optional<EventID>> per producer.rst; fix the template
     * args here if the header disagrees. Future is copy/move-assignable
     * (defaulted ctors, Future.hpp:32-52). */
    std::mutex mtx;
    std::optional<diaspora::Future<std::optional<diaspora::EventID>>> oldest;
    std::chrono::steady_clock::time_point oldest_t0{};
};

/* ---------------------------------------------------------------------- */
/* driver                                                                  */
/* ---------------------------------------------------------------------- */
extern "C" diaspora_driver_t*
diaspora_driver_create(const char* driver_name, const char* options_json) {
    if (!driver_name) { g_last_error = "driver_create: driver_name is NULL"; return nullptr; }
    try {
        diaspora::Metadata options{options_json ? options_json : "{}"};
        /* VERIFY-3 (resolved 2026-06-15, MX-2 reconciliation): use the
         * public Driver::New facade, NOT DriverFactory::create directly.
         * The Factory signature is Factory<...>::create(const std::string&,
         * Args&&...) at Factory.hpp:31 — std::string_view does NOT convert
         * implicitly to const std::string&, so the bundle's original call
         * `DriverFactory::create(string_view{driver_name}, options)` will
         * not compile. Driver::New(const char*, const Metadata&) at
         * Driver.hpp:182 takes const char* and internally invokes
         * DriverFactory::create, letting the const char*->std::string
         * conversion happen at one well-defined site. This is also what
         * the diaspora-ctl tools do (bin/diaspora-ctl/fifo_commands.cpp:54,
         * forward_commands.cpp:319, topic_commands.cpp:63,155 — every
         * upstream caller uses Driver::New). One factory facade, one
         * conversion point, one set of behaviours to verify. */
        auto drv = diaspora::Driver::New(driver_name, options);
        return new diaspora_driver{std::move(drv)};
    } catch (const std::exception& e) { set_err("driver_create", &e); return nullptr; }
      catch (...)                     { set_err("driver_create", nullptr); return nullptr; }
}

extern "C" void diaspora_driver_destroy(diaspora_driver_t* d) {
    try { delete d; } catch (...) { /* destructor must not throw out */ }
}

/* ---------------------------------------------------------------------- */
/* topic                                                                   */
/* ---------------------------------------------------------------------- */
extern "C" diaspora_topic_t*
diaspora_topic_open(diaspora_driver_t* d, const char* topic_name) {
    if (!d || !topic_name) { g_last_error = "topic_open: NULL argument"; return nullptr; }
    try {
        /* openTopic(name) — documented pattern (producer.rst). */
        auto th = d->impl.openTopic(std::string_view{topic_name});
        return new diaspora_topic{std::move(th)};
    } catch (const std::exception& e) { set_err("topic_open", &e); return nullptr; }
      catch (...)                     { set_err("topic_open", nullptr); return nullptr; }
}

extern "C" void diaspora_topic_destroy(diaspora_topic_t* t) {
    try { delete t; } catch (...) {}
}

/* ---------------------------------------------------------------------- */
/* producer                                                                */
/* ---------------------------------------------------------------------- */
extern "C" diaspora_producer_t*
diaspora_producer_create(diaspora_topic_t* t,
                         const char* producer_name,
                         size_t batch_size,
                         size_t max_num_batches,
                         diaspora_c_ordering_t ordering) {
    if (!t) { g_last_error = "producer_create: topic is NULL"; return nullptr; }
    try {
        diaspora::BatchSize bs = (batch_size == 0)
            ? diaspora::BatchSize::Adaptive()
            : diaspora::BatchSize{batch_size};
        diaspora::MaxNumBatches mnb{max_num_batches == 0 ? (size_t)2
                                                         : max_num_batches};
        /* 2 is the library default — TopicHandle.hpp (GetArgOrDefault). */
        diaspora::Ordering ord = (ordering == DIASPORA_C_ORDERING_STRICT)
            ? diaspora::Ordering::Strict
            : diaspora::Ordering::Loose;

        /* VERIFY-4: TopicHandle::producer uses named-argument style
         * (GetArgOrDefault), so a subset of args in any order is accepted
         * and thread_pool, when omitted, falls back to the driver default
         * pool (producer.rst). Confirm against TopicHandle.hpp. The name
         * is wrapped in string_view deliberately (see VERIFY-3 note). */
        auto prod = t->impl.producer(
            std::string_view{producer_name ? producer_name : "diaspora-c"},
            bs, mnb, ord);
        return new diaspora_producer{std::move(prod)};
    } catch (const std::exception& e) { set_err("producer_create", &e); return nullptr; }
      catch (...)                     { set_err("producer_create", nullptr); return nullptr; }
}

extern "C" void diaspora_producer_destroy(diaspora_producer_t* p) {
    try { delete p; } catch (...) {}
}

extern "C" int
diaspora_producer_push(diaspora_producer_t* p,
                       const char* metadata_json,
                       const void* data, size_t data_len) {
    if (!p)             { g_last_error = "push: producer is NULL"; return DIASPORA_C_ERR; }
    if (!metadata_json) { g_last_error = "push: metadata_json is NULL"; return DIASPORA_C_ERR; }
    try {
        diaspora::Metadata md{metadata_json};
        /* Future captured for adopt-oldest tracking; otherwise this is
         * still fire-and-forget (dropping is sanctioned by producer.rst). */
        auto adopt = [p](auto&& fut) {
            std::lock_guard<std::mutex> lk(p->mtx);
            if (!p->oldest) {
                p->oldest = std::forward<decltype(fut)>(fut);
                p->oldest_t0 = std::chrono::steady_clock::now();
            }
        };
        if (data && data_len > 0) {
            /* VERIFY-5: DataView{ptr, size} ctor (producer.rst shows
             * exactly this shape). Check constness — if it takes
             * non-const (RDMA registration often does), keep the
             * const_cast; the C header already states the caller's
             * buffer must stay untouched until flush. */
            diaspora::DataView dv{const_cast<void*>(data), data_len};
            adopt(p->impl.push(md, dv));
        } else {
            /* metadata-only overload — runtime-proven by the darshan
             * module (thousands of events on live bedrock). */
            adopt(p->impl.push(md));
        }
        return DIASPORA_C_OK;
    } catch (const std::exception& e) { set_err("push", &e); return DIASPORA_C_ERR; }
      catch (...)                     { set_err("push", nullptr); return DIASPORA_C_ERR; }
}

extern "C" double
diaspora_producer_oldest_pending_age(diaspora_producer_t* p) {
    if (!p) { g_last_error = "oldest_pending_age: producer is NULL"; return -1.0; }
    try {
        std::lock_guard<std::mutex> lk(p->mtx);
        if (!p->oldest) return 0.0;
        /* operator bool guards completed(): completed() on an invalid
         * Future throws (Future.hpp ~:80). An engaged optional holding an
         * invalid Future should be impossible (we only adopt push()
         * returns) — treat it as "nothing pending" defensively. */
        if (!static_cast<bool>(*p->oldest) || p->oldest->completed()) {
            p->oldest.reset();
            return 0.0;
        }
        std::chrono::duration<double> age =
            std::chrono::steady_clock::now() - p->oldest_t0;
        return age.count();
    } catch (const std::exception& e) { set_err("oldest_pending_age", &e); return -1.0; }
      catch (...)                     { set_err("oldest_pending_age", nullptr); return -1.0; }
}

extern "C" int
diaspora_producer_flush_timeout(diaspora_producer_t* p, int timeout_ms) {
    if (!p) { g_last_error = "flush: producer is NULL"; return DIASPORA_C_ERR; }
    try {
        auto fut = p->impl.flush();           /* non-blocking, returns Future */
        /* VERIFY-6 (resolved 2026-06-15, MX-2 reconciliation): the
         * empty-optional-on-timeout behaviour is a DRIVER-side contract
         * (the WaitFn callback the driver installs into the Future at
         * Future.hpp:90-93), NOT an API-layer guarantee. The Future
         * interface itself (Future.hpp:71-75) declares
         * `ResultType wait(int timeout_ms)` with no special timeout
         * sentinel; the empty-optional convention is what the mofka
         * driver chose to implement (mofka 0.5.7/0.9.x verified). If
         * a future non-mofka driver is wrapped through these C bindings,
         * its empty-optional-on-timeout semantics must be re-verified
         * before relying on res.has_value() to discriminate timeout
         * from success. wait(-1) blocks; that part is universal. */
        auto res = fut.wait(timeout_ms);
        return res.has_value() ? DIASPORA_C_OK : DIASPORA_C_TIMEOUT;
    } catch (const std::exception& e) { set_err("flush", &e); return DIASPORA_C_ERR; }
      catch (...)                     { set_err("flush", nullptr); return DIASPORA_C_ERR; }
}
