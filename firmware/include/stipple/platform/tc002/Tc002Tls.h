// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace stipple {
namespace platform {
namespace tc002 {

/// TLS for outbound fetches, using the OpenSSL already on the device.
///
/// **Loaded with `dlopen`, not linked.** The device carries
/// `/lib/libssl.so.1.1` and `/lib/libcrypto.so.1.1` — measured, not assumed —
/// but nothing else in this project may depend on them: the host tests, the
/// WASM emulator and CI have no such library, and ADR 0012 keeps the build
/// dependency-free. So there are no OpenSSL headers here, every handle is a
/// `void*`, and a device without the library gets a clear refusal instead of
/// a binary that will not start. Same pattern as Tc002Audio and the LED HAL.
///
/// **Verification is not optional and there is no flag to turn it off.**
/// A switch like that gets set once while debugging and then ships. Three
/// things must all hold before a byte of the request goes out:
///
///   - the chain verifies against the trusted roots in `kCaBundlePath`;
///   - the certificate actually names the host we asked for
///     (`SSL_set1_host`) — without this, a valid certificate for *any*
///     domain would pass, which is the classic way a hand-rolled TLS client
///     is wrong while appearing to work;
///   - TLS 1.2 or better.
///
/// **The roots are a file we install, because the device has none.** There is
/// no `/etc/ssl` on a TC002 and no `ca-certificates` package, so
/// `SSL_CTX_set_default_verify_paths` would find nothing and every
/// connection would fail with "unable to get local issuer certificate". The
/// bundle ships with Stipple and lives in `/data`, where it can be replaced
/// when a root expires without reflashing anything.
class Tls {
public:
    /// Where the trusted roots are looked for, in order.
    ///
    /// The same shape as the application itself: something in `/data` wins,
    /// what was flashed is the fallback. Roots expire and get added, so the
    /// set has to be replaceable without reflashing - and a device that has
    /// never been updated still needs to be able to fetch.
    ///
    /// A single PEM file rather than a hashed directory, because the device
    /// has no `c_rehash` and a directory OpenSSL cannot index is one it
    /// silently ignores.
    static constexpr const char* kCaBundlePath = "/data/stipple/ca-certificates.crt";
    static constexpr const char* kCaBundleFallback = "/res/lib/ca-certificates.crt";

    /// The one instance, built on first use.
    ///
    /// One `SSL_CTX` for the process, because loading it parses every root in
    /// the bundle — about 150 of them — and doing that per request would put
    /// a tenth of a second on every fetch to no purpose. OpenSSL 1.1 allows
    /// one context to be shared across threads, which is what makes this safe
    /// from the fetch worker.
    static Tls& instance();

    /// Whether a TLS connection can be attempted at all.
    bool usable() const noexcept { return context_ != nullptr; }

    /// Why not, when `usable()` is false. Written for a 52-pixel panel: short,
    /// and naming the thing to fix.
    std::string_view problem() const noexcept { return problem_; }

    /// Wrap a connected socket and complete the handshake.
    ///
    /// Returns null on any failure, leaving `problem` describing it — and a
    /// verification failure is reported by name (`X509_verify_cert_error_string`)
    /// rather than as a generic refusal, because "certificate has expired"
    /// and "self signed certificate" need completely different responses from
    /// whoever is reading the panel.
    void* connect(int fd, const std::string& host, std::string& problem);

    /// Negative on error, zero at end of stream.
    int read(void* session, void* buffer, int length);
    int write(void* session, const void* buffer, int length);
    void close(void* session);

private:
    Tls();

    Tls(const Tls&) = delete;
    Tls& operator=(const Tls&) = delete;

    /// Resolve every symbol, or fail as a whole.
    ///
    /// All or nothing on purpose: a half-resolved TLS client is one that
    /// crashes at the worst moment rather than declining at the first.
    bool resolve();

    /// Turn OpenSSL's error queue into something that fits on the panel.
    std::string describeError(void* session, int result);

    void* ssl_ = nullptr;     ///< libssl handle
    void* crypto_ = nullptr;  ///< libcrypto handle, for the error strings
    void* context_ = nullptr;  ///< SSL_CTX*, null when unusable
    std::string problem_;

    // OpenSSL, as function pointers. `void*` throughout so no header is
    // needed; the signatures come from the documented 1.1 ABI.
    const void* (*clientMethod_)() = nullptr;
    void* (*ctxNew_)(const void*) = nullptr;
    void (*ctxFree_)(void*) = nullptr;
    int (*ctxLoadVerify_)(void*, const char*, const char*) = nullptr;
    void (*ctxSetVerify_)(void*, int, void*) = nullptr;
    long (*ctxCtrl_)(void*, int, long, void*) = nullptr;
    void* (*sslNew_)(void*) = nullptr;
    void (*sslFree_)(void*) = nullptr;
    int (*sslSetFd_)(void*, int) = nullptr;
    long (*sslCtrl_)(void*, int, long, void*) = nullptr;
    int (*sslSet1Host_)(void*, const char*) = nullptr;
    int (*sslConnect_)(void*) = nullptr;
    int (*sslRead_)(void*, void*, int) = nullptr;
    int (*sslWrite_)(void*, const void*, int) = nullptr;
    long (*sslVerifyResult_)(const void*) = nullptr;
    int (*sslShutdown_)(void*) = nullptr;
    const char* (*verifyErrorString_)(long) = nullptr;
    int (*initSsl_)(unsigned long long, const void*) = nullptr;
    int (*sslGetError_)(const void*, int) = nullptr;
    unsigned long (*errGetError_)() = nullptr;
    void (*errClearError_)() = nullptr;
    void (*errStringN_)(unsigned long, char*, std::size_t) = nullptr;
};

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
