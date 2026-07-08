/* diaspora_c.cpp — C bindings for the Diaspora Stream API (producer side).
 *
 * See diaspora_c.h for the public contract. Rule enforced throughout: no
 * C++ exception crosses the C boundary — failures become NULL/negative
 * returns, with detail available from diaspora_c_last_error().
 */

#include <diaspora/diaspora_c.h>

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

namespace {

thread_local std::string g_last_error;

/* noexcept so it is safe to call outside a try block (e.g. NULL-argument
 * guards): a std::string assignment can itself throw, and no exception may
 * cross the C boundary. */
void set_msg(const char* msg) noexcept {
    try { g_last_error.assign(msg); } catch (...) {}
}

void set_err(const char* where, const std::exception* e) noexcept {
    try {
        g_last_error.assign(where);
        g_last_error += ": ";
        g_last_error += e ? e->what() : "unknown (non-std exception)";
    } catch (...) {}
}

} /* namespace */

extern "C" const char* diaspora_c_last_error(void) {
    return g_last_error.c_str();
}

/* Opaque handles: each wraps one value-type Diaspora object (shared_ptr
 * internals, so copy/move is cheap). */
struct diaspora_driver   { diaspora::Driver      impl; };
struct diaspora_topic    { diaspora::TopicHandle impl; };
struct diaspora_producer {
    diaspora::Producer impl;
    std::mutex mtx;
    std::optional<diaspora::Future<std::optional<diaspora::EventID>>> oldest;
    std::chrono::steady_clock::time_point oldest_t0{};
};

extern "C" diaspora_driver_t*
diaspora_driver_create(const char* driver_name, const char* options_json) {
    if (!driver_name) { set_msg("driver_create: driver_name is NULL"); return nullptr; }
    try {
        diaspora::Metadata options{options_json ? options_json : "{}"};
        auto drv = diaspora::Driver::New(driver_name, options);
        return new diaspora_driver{std::move(drv)};
    } catch (const std::exception& e) { set_err("driver_create", &e); return nullptr; }
      catch (...)                     { set_err("driver_create", nullptr); return nullptr; }
}

extern "C" void diaspora_driver_destroy(diaspora_driver_t* d) {
    try { delete d; } catch (...) {}
}

extern "C" diaspora_topic_t*
diaspora_topic_open(diaspora_driver_t* d, const char* topic_name) {
    if (!d || !topic_name) { set_msg("topic_open: NULL argument"); return nullptr; }
    try {
        auto th = d->impl.openTopic(std::string_view{topic_name});
        return new diaspora_topic{std::move(th)};
    } catch (const std::exception& e) { set_err("topic_open", &e); return nullptr; }
      catch (...)                     { set_err("topic_open", nullptr); return nullptr; }
}

extern "C" void diaspora_topic_destroy(diaspora_topic_t* t) {
    try { delete t; } catch (...) {}
}

extern "C" diaspora_producer_t*
diaspora_producer_create(diaspora_topic_t* t,
                         const char* producer_name,
                         size_t batch_size,
                         size_t max_num_batches,
                         diaspora_c_ordering_t ordering) {
    if (!t) { set_msg("producer_create: topic is NULL"); return nullptr; }
    try {
        diaspora::BatchSize bs = (batch_size == 0)
            ? diaspora::BatchSize::Adaptive()
            : diaspora::BatchSize{batch_size};
        diaspora::Ordering ord = (ordering == DIASPORA_C_ORDERING_STRICT)
            ? diaspora::Ordering::Strict
            : diaspora::Ordering::Loose;
        std::string_view name{producer_name ? producer_name : "diaspora-c"};
        /* producer() takes options by type, in any order, filling omitted
         * ones with the library's own defaults. Pass MaxNumBatches only when
         * the caller specifies it, so 0 tracks the library default rather
         * than a value copied here. */
        auto prod = (max_num_batches == 0)
            ? t->impl.producer(name, bs, ord)
            : t->impl.producer(name, bs, diaspora::MaxNumBatches{max_num_batches}, ord);
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
    if (!p)             { set_msg("push: producer is NULL"); return DIASPORA_C_ERR; }
    if (!metadata_json) { set_msg("push: metadata_json is NULL"); return DIASPORA_C_ERR; }
    try {
        diaspora::Metadata md{metadata_json};
        /* Track only the oldest still-unacked push, for oldest_pending_age(). */
        auto adopt = [p](auto&& fut) {
            std::lock_guard<std::mutex> lk(p->mtx);
            if (!p->oldest) {
                p->oldest = std::forward<decltype(fut)>(fut);
                p->oldest_t0 = std::chrono::steady_clock::now();
            }
        };
        if (data && data_len > 0) {
            diaspora::DataView dv{const_cast<void*>(data), data_len};
            adopt(p->impl.push(md, dv));
        } else {
            adopt(p->impl.push(md));
        }
        return DIASPORA_C_OK;
    } catch (const std::exception& e) { set_err("push", &e); return DIASPORA_C_ERR; }
      catch (...)                     { set_err("push", nullptr); return DIASPORA_C_ERR; }
}

extern "C" double
diaspora_producer_oldest_pending_age(diaspora_producer_t* p) {
    if (!p) { set_msg("oldest_pending_age: producer is NULL"); return -1.0; }
    try {
        std::lock_guard<std::mutex> lk(p->mtx);
        if (!p->oldest) return 0.0;
        /* completed() throws unless the future has actually completed
         * (operator bool is true for a merely-valid future too, so it is
         * NOT a sufficient guard). Treat a throw as "still pending". */
        bool done = false;
        if (static_cast<bool>(*p->oldest)) {
            try { done = p->oldest->completed(); } catch (...) { done = false; }
        } else {
            done = true;  /* invalid future: nothing to wait on */
        }
        if (done) {
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
    if (!p) { set_msg("flush: producer is NULL"); return DIASPORA_C_ERR; }
    try {
        auto fut = p->impl.flush();
        auto res = fut.wait(timeout_ms);   /* empty optional == timeout (driver contract) */
        return res.has_value() ? DIASPORA_C_OK : DIASPORA_C_TIMEOUT;
    } catch (const std::exception& e) { set_err("flush", &e); return DIASPORA_C_ERR; }
      catch (...)                     { set_err("flush", nullptr); return DIASPORA_C_ERR; }
}
