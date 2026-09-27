// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace stipple {
namespace platform {
namespace tc002 {

/// TLS for outbound fetches, using BearSSL.
///
/// **Not the device's OpenSSL, because that cannot do TLS.** The TC002 carries
/// OpenSSL 1.1.0i built by OpenWrt in 2018 with every protocol version
/// compiled out — a crypto library with a stub SSL layer. Measured rather than
/// assumed: `SSL_connect` answered `NO_PROTOCOLS_AVAILABLE` for every floor
/// including none at all, and `TLSv1_2_client_method` and its siblings are
/// absent from its symbol table. An earlier version of this class loaded that
/// library with `dlopen` and got exactly nowhere.
///
/// BearSSL instead: MIT, C99, no dependencies, and it allocates nothing of its
/// own — every buffer is one the caller hands it, which is the constraint
/// blueprint §38 already puts on this codebase. It links into the TC002
/// adapter only, so `stipple_core` stays dependency-free (ADR 0012) and the
/// host tests and the WASM emulator never see a byte of it.
///
/// **Verification is not optional and there is no flag to turn it off.** A
/// switch like that gets set once while debugging and then ships. Four things
/// must hold before a byte of the request goes out:
///
///   - the chain verifies against the trusted roots in the bundle;
///   - the certificate names the host we asked for — BearSSL's minimal X.509
///     engine takes that name from `br_ssl_client_reset`, so unlike OpenSSL
///     it cannot be forgotten as a separate step;
///   - the certificate is valid *now*, which needs a clock the device
///     believes in. Without one this refuses rather than skipping the check;
///   - TLS 1.2, which is both the floor set here and the ceiling BearSSL 0.6
///     offers.
class Tls {
public:
    /// Where the trusted roots are looked for, in order.
    ///
    /// The same shape as the application itself: something in `/data` wins,
    /// what was flashed is the fallback. Roots expire and get added, so the
    /// set has to be replaceable without reflashing — and a device that has
    /// never been updated still needs to be able to fetch.
    static constexpr const char* kCaBundlePath = "/data/stipple/ca-certificates.crt";
    static constexpr const char* kCaBundleFallback = "/res/lib/ca-certificates.crt";

    /// Largest bundle read. Mozilla's is about 290 KB; this leaves room for it
    /// to grow without leaving room for a runaway file to exhaust the device.
    static constexpr std::size_t kMaxBundleBytes = 1024u * 1024u;

    /// The one instance, built on first use.
    ///
    /// The bundle is parsed once — about 150 roots, each an X.509 decode — and
    /// doing that per request would put a visible pause on every fetch.
    static Tls& instance();

    /// Whether a TLS connection can be attempted at all.
    bool usable() const noexcept { return anchorCount() > 0; }

    /// Why not, when `usable()` is false. Written for a 52-pixel panel: short,
    /// and naming the thing to fix.
    std::string_view problem() const noexcept { return problem_; }

    /// Roots loaded, for diagnostics. A bundle that parsed to three anchors
    /// when it should have been a hundred and fifty is one that is truncated,
    /// and nothing else on the device would say so.
    int anchorCount() const noexcept;

    /// Wrap a connected socket and complete the handshake.
    ///
    /// Returns null on any failure, leaving `problem` naming it. BearSSL's
    /// error codes tell an expired certificate from an unknown issuer from a
    /// name mismatch, and those need completely different responses from
    /// whoever is reading the panel.
    void* connect(int fd, const std::string& host, std::string& problem);

    /// Bytes read, 0 at a clean end of stream, negative on error.
    int read(void* session, void* buffer, int length);

    /// `length` on success, negative on failure.
    ///
    /// Flushes before returning: a request left sitting in BearSSL's output
    /// buffer is a request the server never sees, and the symptom of
    /// forgetting it is a read that times out rather than an error.
    int write(void* session, const void* buffer, int length);

    void close(void* session);

private:
    Tls();
    ~Tls();

    Tls(const Tls&) = delete;
    Tls& operator=(const Tls&) = delete;

    /// One trusted root, and the storage BearSSL's view of it points into.
    ///
    /// Heap-allocated and never moved, because `br_x509_trust_anchor` holds
    /// raw pointers into these buffers. A `vector<Anchor>` that reallocated
    /// would leave every earlier anchor pointing at freed memory — and it
    /// would only start doing so once the bundle outgrew the initial
    /// capacity, which is the kind of fault that survives every test written
    /// against a three-certificate bundle.
    struct Anchor {
        std::vector<unsigned char> dn;
        std::vector<unsigned char> keyA;  ///< RSA modulus, or the EC point
        std::vector<unsigned char> keyB;  ///< RSA exponent; unused for EC
    };

    /// The BearSSL anchor array, behind a forward declaration.
    ///
    /// Deliberately not spelled out here: naming the type would drag
    /// `bearssl_x509.h` into everything that includes this header, and the
    /// point of linking BearSSL PRIVATE to the adapter is that it does not
    /// escape. The definition lives in the .cpp.
    struct AnchorList;

    bool loadBundle(const char* path);

    /// Decode one DER certificate into an anchor. False for anything that is
    /// not a usable CA, which is skipped rather than failing the whole
    /// bundle: one unparseable root out of a hundred and fifty should cost
    /// that root, not the device's ability to fetch anything.
    bool addAnchor(const unsigned char* der, std::size_t length);

    std::string problem_;
    std::vector<std::unique_ptr<Anchor>> storage_;
    std::unique_ptr<AnchorList> anchors_;
};

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
