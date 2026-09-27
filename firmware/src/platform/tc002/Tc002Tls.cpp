// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/platform/tc002/Tc002Tls.h"

#include <dlfcn.h>
#include <sys/stat.h>

namespace stipple {
namespace platform {
namespace tc002 {
namespace {

// The handful of OpenSSL constants this needs, spelled out rather than
// included. Every one is ABI, not API: they are baked into compiled callers
// and cannot change without an soname bump, which is exactly why copying them
// is safe here and copying a struct layout would not be.
constexpr int kVerifyPeer = 0x01;                  // SSL_VERIFY_PEER
constexpr int kSetTlsExtHostname = 55;             // SSL_CTRL_SET_TLSEXT_HOSTNAME
constexpr long kNameTypeHostName = 0;              // TLSEXT_NAMETYPE_host_name
constexpr int kSetMinProtoVersion = 123;           // SSL_CTRL_SET_MIN_PROTO_VERSION
constexpr long kTls12 = 0x0303;                    // TLS1_2_VERSION
constexpr long kVerifyOk = 0;                      // X509_V_OK

/// SSL_R_NO_PROTOCOLS_AVAILABLE. See describeError for why this one is
/// singled out.
constexpr unsigned long kReasonNoProtocols = 191;

// Without these, OpenSSL has no reason strings loaded and every error prints
// as "reason(NNN)" - which is what the panel showed for a whole debugging
// round, and is the difference between an error you can act on and a number.
constexpr unsigned long long kInitLoadSslStrings = 0x00200000ULL;
constexpr unsigned long long kInitLoadCryptoStrings = 0x00000002ULL;

bool fileExists(const char* path) noexcept {
    struct stat info;
    return ::stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

}  // namespace

std::string Tls::describeError(void* session, int result) {
    // Two sources, because they answer different questions. SSL_get_error
    // says what kind of failure it was - a clean EOF, a syscall error, a
    // protocol alert - and the error queue says which one specifically.
    const int kind = sslGetError_(session, result);
    const unsigned long queued = errGetError_();

    if (queued != 0) {
        char buffer[256] = {};
        errStringN_(queued, buffer, sizeof(buffer));
        // OpenSSL formats these as "error:NNNNNNNN:lib:func:reason". Only the
        // reason fits on a 52-pixel panel, and it is the only part that says
        // anything - so take what follows the last colon.
        std::string text(buffer);
        const std::size_t last = text.rfind(':');
        if (last != std::string::npos && last + 1 < text.size()) {
            text = text.substr(last + 1);
        }
        // A reason OpenSSL has no string for prints as "reason(191)", and
        // that number alone is a trap: every OpenSSL sub-library numbers its
        // reasons from 1, so 191 means one thing in the SSL table and
        // something unrelated in the X509 or PEM one. Chasing 191 through
        // sslerr.h cost an afternoon before that sank in.
        //
        // The packed code carries both - library in the high byte, reason in
        // the low twelve bits - and eight hex characters fit on the panel
        // where a sentence does not.
        // The one code worth naming, because it is not a network problem and
        // no amount of retrying will change it.
        //
        // The TC002 carries OpenSSL 1.1.0i built by OpenWrt with every TLS
        // protocol version compiled out - a crypto library with a stub SSL
        // layer on top, presumably because the vendor only ever wanted the
        // hashing. Measured: SSL_connect returns NO_PROTOCOLS_AVAILABLE for
        // every floor including none at all, and the per-version methods
        // (TLSv1_2_client_method and friends) are not in its symbol table.
        //
        // It is worth naming here rather than leaving as a hex code because
        // the answer is "this device cannot do https until Stipple carries
        // its own TLS", which is a very different thing to tell somebody
        // than "handshake failed".
        if ((queued & 0xFFFu) == kReasonNoProtocols) {
            return "openssl has no tls";
        }
        if (text.rfind("reason(", 0) == 0) {
            static const char* kHex = "0123456789abcdef";
            std::string code;
            for (int shift = 28; shift >= 0; shift -= 4) {
                code += kHex[(queued >> shift) & 0xFu];
            }
            return "ssl " + code;
        }
        return text;
    }

    switch (kind) {
        case 5:  // SSL_ERROR_SYSCALL
            return "tls connection cut";
        case 6:  // SSL_ERROR_ZERO_RETURN
            return "tls closed early";
        default:
            break;
    }
    return "tls handshake failed";
}

Tls& Tls::instance() {
    // Function-local static: initialised once, and the standard makes that
    // thread-safe, which matters because the first caller is a fetch worker.
    static Tls one;
    return one;
}

Tls::Tls() {
    // libcrypto first and RTLD_GLOBAL, because the error-string helper lives
    // there and libssl's own dependency on it would otherwise be loaded into
    // a scope dlsym cannot search from our handle.
    crypto_ = ::dlopen("libcrypto.so.1.1", RTLD_NOW | RTLD_GLOBAL);
    if (crypto_ == nullptr) {
        crypto_ = ::dlopen("libcrypto.so", RTLD_NOW | RTLD_GLOBAL);
    }
    ssl_ = ::dlopen("libssl.so.1.1", RTLD_NOW | RTLD_GLOBAL);
    if (ssl_ == nullptr) {
        ssl_ = ::dlopen("libssl.so", RTLD_NOW | RTLD_GLOBAL);
    }

    if (ssl_ == nullptr || crypto_ == nullptr) {
        problem_ = "no openssl";
        return;
    }
    if (!resolve()) {
        problem_ = "openssl too old";
        return;
    }

    // The roots, before anything else. Without them every handshake would
    // fail with "unable to get local issuer certificate", which reads as a
    // problem with the server rather than with this device.
    const char* bundle = kCaBundlePath;
    if (!fileExists(bundle)) {
        bundle = kCaBundleFallback;
    }
    if (!fileExists(bundle)) {
        problem_ = "no ca bundle";
        return;
    }

    // Explicit, and only for the strings. OpenSSL 1.1 initialises itself on
    // first use, so this changes nothing about how it works - it changes what
    // it can tell us when something goes wrong.
    initSsl_(kInitLoadSslStrings | kInitLoadCryptoStrings, nullptr);

    const void* method = clientMethod_();
    if (method == nullptr) {
        problem_ = "no tls method";
        return;
    }
    void* context = ctxNew_(method);
    if (context == nullptr) {
        problem_ = "no tls context";
        return;
    }

    if (ctxLoadVerify_(context, bundle, nullptr) != 1) {
        ctxFree_(context);
        problem_ = "bad ca bundle";
        return;
    }

    // Verification on. Belt and braces with the explicit
    // SSL_get_verify_result check in connect(): SSL_VERIFY_PEER already makes
    // the handshake fail, and checking again costs nothing and means a future
    // edit to either one cannot silently disable both.
    ctxSetVerify_(context, kVerifyPeer, nullptr);

    // TLS 1.2 floor, *if the library can offer it*.
    //
    // SSL_CTX_set_min_proto_version returns 0 when the floor it is asked for
    // leaves nothing enabled, and the consequence of ignoring that return is
    // the failure this cost an afternoon: every handshake died with
    // SSL_R_NO_PROTOCOLS_AVAILABLE (191), which reads as a network problem
    // and is not one.
    //
    // A library too old to offer 1.2 is one whose best is already deprecated
    // everywhere, so the honest answer is to say TLS is unusable rather than
    // to quietly negotiate 1.0 with somebody's weather API.
    if (ctxCtrl_(context, kSetMinProtoVersion, kTls12, nullptr) != 1) {
        ctxFree_(context);
        problem_ = "openssl has no tls 1.2";
        return;
    }

    context_ = context;
}

bool Tls::resolve() {
    clientMethod_ = reinterpret_cast<const void* (*)()>(::dlsym(ssl_, "TLS_client_method"));
    ctxNew_ = reinterpret_cast<void* (*)(const void*)>(::dlsym(ssl_, "SSL_CTX_new"));
    ctxFree_ = reinterpret_cast<void (*)(void*)>(::dlsym(ssl_, "SSL_CTX_free"));
    ctxLoadVerify_ = reinterpret_cast<int (*)(void*, const char*, const char*)>(
        ::dlsym(ssl_, "SSL_CTX_load_verify_locations"));
    ctxSetVerify_ =
        reinterpret_cast<void (*)(void*, int, void*)>(::dlsym(ssl_, "SSL_CTX_set_verify"));
    ctxCtrl_ = reinterpret_cast<long (*)(void*, int, long, void*)>(
        ::dlsym(ssl_, "SSL_CTX_ctrl"));
    sslNew_ = reinterpret_cast<void* (*)(void*)>(::dlsym(ssl_, "SSL_new"));
    sslFree_ = reinterpret_cast<void (*)(void*)>(::dlsym(ssl_, "SSL_free"));
    sslSetFd_ = reinterpret_cast<int (*)(void*, int)>(::dlsym(ssl_, "SSL_set_fd"));
    sslCtrl_ =
        reinterpret_cast<long (*)(void*, int, long, void*)>(::dlsym(ssl_, "SSL_ctrl"));
    sslSet1Host_ = reinterpret_cast<int (*)(void*, const char*)>(::dlsym(ssl_, "SSL_set1_host"));
    sslConnect_ = reinterpret_cast<int (*)(void*)>(::dlsym(ssl_, "SSL_connect"));
    sslRead_ = reinterpret_cast<int (*)(void*, void*, int)>(::dlsym(ssl_, "SSL_read"));
    sslWrite_ =
        reinterpret_cast<int (*)(void*, const void*, int)>(::dlsym(ssl_, "SSL_write"));
    sslVerifyResult_ =
        reinterpret_cast<long (*)(const void*)>(::dlsym(ssl_, "SSL_get_verify_result"));
    sslShutdown_ = reinterpret_cast<int (*)(void*)>(::dlsym(ssl_, "SSL_shutdown"));
    verifyErrorString_ = reinterpret_cast<const char* (*)(long)>(
        ::dlsym(crypto_, "X509_verify_cert_error_string"));
    initSsl_ = reinterpret_cast<int (*)(unsigned long long, const void*)>(
        ::dlsym(ssl_, "OPENSSL_init_ssl"));
    sslGetError_ = reinterpret_cast<int (*)(const void*, int)>(::dlsym(ssl_, "SSL_get_error"));
    errGetError_ = reinterpret_cast<unsigned long (*)()>(::dlsym(crypto_, "ERR_get_error"));
    errClearError_ = reinterpret_cast<void (*)()>(::dlsym(crypto_, "ERR_clear_error"));
    errStringN_ = reinterpret_cast<void (*)(unsigned long, char*, std::size_t)>(
        ::dlsym(crypto_, "ERR_error_string_n"));

    // All or nothing. A half-resolved TLS client is one that crashes at the
    // worst moment rather than declining at the first, and SSL_set1_host in
    // particular is the difference between checking the hostname and not.
    return clientMethod_ != nullptr && ctxNew_ != nullptr && ctxFree_ != nullptr &&
           ctxLoadVerify_ != nullptr && ctxSetVerify_ != nullptr && ctxCtrl_ != nullptr &&
           sslNew_ != nullptr && sslFree_ != nullptr && sslSetFd_ != nullptr &&
           sslCtrl_ != nullptr && sslSet1Host_ != nullptr && sslConnect_ != nullptr &&
           sslRead_ != nullptr && sslWrite_ != nullptr && sslVerifyResult_ != nullptr &&
           sslShutdown_ != nullptr && verifyErrorString_ != nullptr &&
           sslGetError_ != nullptr && errGetError_ != nullptr && errStringN_ != nullptr &&
           initSsl_ != nullptr && errClearError_ != nullptr;
}

void* Tls::connect(int fd, const std::string& host, std::string& problem) {
    if (context_ == nullptr) {
        problem = problem_;
        return nullptr;
    }

    void* session = sslNew_(context_);
    if (session == nullptr) {
        problem = "no tls session";
        return nullptr;
    }
    if (sslSetFd_(session, fd) != 1) {
        sslFree_(session);
        problem = "tls socket refused";
        return nullptr;
    }

    // SNI. Without it a shared host answers with whichever certificate it
    // considers default, which then fails the name check below - so the
    // symptom of forgetting this is a name mismatch on a server that is
    // perfectly configured.
    sslCtrl_(session, kSetTlsExtHostname, kNameTypeHostName,
             const_cast<char*>(host.c_str()));

    // And the name check itself. This is the one that matters: a chain can
    // verify perfectly against a real root and still be a certificate for
    // somebody else's domain, which is precisely what an interceptor
    // presents.
    if (sslSet1Host_(session, host.c_str()) != 1) {
        sslFree_(session);
        problem = "bad host name";
        return nullptr;
    }

    // Empty the queue first.
    //
    // OpenSSL's error queue is per-thread and cumulative: anything that
    // failed earlier on this thread is still sitting in it, and the first
    // thing ERR_get_error hands back is the *oldest* entry. Reporting that
    // after a failed handshake means reporting a stale error as though it
    // were the cause - which is precisely what happened here, and it sent
    // this chasing "no protocols available" through the wrong table for an
    // afternoon. OpenSSL's own documentation says to do this before any call
    // whose error you intend to read.
    errClearError_();

    const int handshake = sslConnect_(session);
    if (handshake != 1) {
        // Ask why before tearing anything down, and ask in the right order.
        //
        // A rejected certificate has a name - "certificate has expired",
        // "self signed certificate" - and those need completely different
        // responses from whoever is reading the panel. But a handshake that
        // failed at the protocol level never got as far as a certificate, and
        // then SSL_get_verify_result still says X509_V_OK, so checking only
        // that reports every such failure as a bare "handshake failed" and
        // tells nobody anything. Learned by shipping exactly that.
        const long verdict = sslVerifyResult_(session);
        if (verdict != kVerifyOk) {
            const char* why = verifyErrorString_(verdict);
            problem = why != nullptr ? why : "certificate rejected";
        } else {
            problem = describeError(session, handshake);
        }
        sslFree_(session);
        return nullptr;
    }

    if (sslVerifyResult_(session) != kVerifyOk) {
        // Should be unreachable with SSL_VERIFY_PEER set, and checked anyway:
        // this is the last point at which an unverified connection could be
        // used, and the cost of being sure is one function call.
        const char* why = verifyErrorString_(sslVerifyResult_(session));
        problem = why != nullptr ? why : "certificate rejected";
        sslShutdown_(session);
        sslFree_(session);
        return nullptr;
    }

    return session;
}

int Tls::read(void* session, void* buffer, int length) {
    if (context_ == nullptr || session == nullptr) {
        return -1;
    }
    return sslRead_(session, buffer, length);
}

int Tls::write(void* session, const void* buffer, int length) {
    if (context_ == nullptr || session == nullptr) {
        return -1;
    }
    return sslWrite_(session, buffer, length);
}

void Tls::close(void* session) {
    if (context_ == nullptr || session == nullptr) {
        return;
    }
    // One shutdown, not the two-step the protocol allows. We are closing the
    // socket immediately either way, and waiting for the peer's close_notify
    // would be waiting on a server that has no reason to hurry.
    sslShutdown_(session);
    sslFree_(session);
}

}  // namespace tc002
}  // namespace platform
}  // namespace stipple
