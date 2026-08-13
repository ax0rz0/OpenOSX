/*
 * SecureTransport.c - the CoreFoundation face of Secure Transport.
 *
 * All of the TLS lives in st_core.c, which knows nothing about CoreFoundation
 * and is tested natively on the build host. This file is the veneer apps
 * actually call: three CFTypes (SSLContext, SecCertificate, SecTrust) and the
 * SSL and SecTrust entry points on top of that core.
 *
 * Because this file is compiled against the real MacOSX SDK headers, it can use
 * Apple's own enum names rather than transcribing their values - and it can
 * check the values st_core.h *did* transcribe against the SDK at compile time.
 * If Apple's errSSLWouldBlock ever disagrees with ST_ERR_WOULDBLOCK, the build
 * stops here instead of an app hanging in the field.
 *
 * SecTrustEvaluate is the security boundary that matters. curl - and so every
 * app in the corpus linking libcurl - runs Secure Transport with
 * kSSLSessionOptionBreakOnServerAuth and then judges the chain itself through
 * SecTrustSetAnchorCertificates + SecTrustEvaluate. SSLHandshake has been told
 * to stand aside by then, so what SecTrustEvaluate answers is the whole of the
 * protection those apps get.
 */
#include <Security/SecureTransport.h>
#include <Security/SecCertificate.h>
#include <Security/SecTrust.h>
#include <Security/SecPolicy.h>
#include <Security/SecBase.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFRuntime.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "st_core.h"

/* --------------------------------------------------------------- asserts --
 * st_core.h transcribed these from the SDK by hand. This is where that gets
 * checked rather than trusted.
 */
#define ST_SAME(ours, theirs) \
    typedef char st_assert_##ours[((int)(ours) == (int)(theirs)) ? 1 : -1]

ST_SAME(ST_ERR_SUCCESS,             errSecSuccess);
ST_SAME(ST_ERR_PARAM,               errSecParam);
ST_SAME(ST_ERR_ALLOCATE,            errSecAllocate);
ST_SAME(ST_ERR_UNIMPLEMENTED,       errSecUnimplemented);
ST_SAME(ST_ERR_IO,                  errSecIO);
ST_SAME(ST_ERR_BADREQ,              errSecBadReq);
ST_SAME(ST_ERR_PROTOCOL,            errSSLProtocol);
ST_SAME(ST_ERR_NEGOTIATION,         errSSLNegotiation);
ST_SAME(ST_ERR_FATAL_ALERT,         errSSLFatalAlert);
ST_SAME(ST_ERR_WOULDBLOCK,          errSSLWouldBlock);
ST_SAME(ST_ERR_CLOSED_GRACEFUL,     errSSLClosedGraceful);
ST_SAME(ST_ERR_CLOSED_ABORT,        errSSLClosedAbort);
ST_SAME(ST_ERR_XCERT_CHAIN_INVALID, errSSLXCertChainInvalid);
ST_SAME(ST_ERR_BAD_CERT,            errSSLBadCert);
ST_SAME(ST_ERR_CRYPTO,              errSSLCrypto);
ST_SAME(ST_ERR_INTERNAL,            errSSLInternal);
ST_SAME(ST_ERR_UNKNOWN_ROOT_CERT,   errSSLUnknownRootCert);
ST_SAME(ST_ERR_NO_ROOT_CERT,        errSSLNoRootCert);
ST_SAME(ST_ERR_CERT_EXPIRED,        errSSLCertExpired);
ST_SAME(ST_ERR_CERT_NOT_YET_VALID,  errSSLCertNotYetValid);
ST_SAME(ST_ERR_CLOSED_NO_NOTIFY,    errSSLClosedNoNotify);
ST_SAME(ST_ERR_BUFFER_OVERFLOW,     errSSLBufferOverflow);
ST_SAME(ST_ERR_BAD_CIPHER_SUITE,    errSSLBadCipherSuite);
ST_SAME(ST_ERR_PEER_AUTH_COMPLETED, errSSLPeerAuthCompleted);
ST_SAME(ST_ERR_HOST_NAME_MISMATCH,  errSSLHostNameMismatch);
ST_SAME(ST_ERR_BAD_CONFIGURATION,   errSSLBadConfiguration);
ST_SAME(ST_ERR_HANDSHAKE_FAIL,      errSSLHandshakeFail);

ST_SAME(ST_PROTO_UNKNOWN,   kSSLProtocolUnknown);
ST_SAME(ST_PROTO_TLS1,      kTLSProtocol1);
ST_SAME(ST_PROTO_TLS11,     kTLSProtocol11);
ST_SAME(ST_PROTO_TLS12,     kTLSProtocol12);
ST_SAME(ST_PROTO_TLS13,     kTLSProtocol13);
ST_SAME(ST_PROTO_ALL,       kSSLProtocolAll);
ST_SAME(ST_STATE_IDLE,      kSSLIdle);
ST_SAME(ST_STATE_HANDSHAKE, kSSLHandshake);
ST_SAME(ST_STATE_CONNECTED, kSSLConnected);
ST_SAME(ST_STATE_CLOSED,    kSSLClosed);
ST_SAME(ST_STATE_ABORTED,   kSSLAborted);
ST_SAME(ST_SIDE_SERVER,     kSSLServerSide);
ST_SAME(ST_SIDE_CLIENT,     kSSLClientSide);
ST_SAME(ST_TYPE_STREAM,     kSSLStreamType);
ST_SAME(ST_TYPE_DATAGRAM,   kSSLDatagramType);
ST_SAME(ST_OPT_BREAK_ON_SERVER_AUTH, kSSLSessionOptionBreakOnServerAuth);
ST_SAME(ST_OPT_FALSE_START,          kSSLSessionOptionFalseStart);
ST_SAME(ST_OPT_FALLBACK,             kSSLSessionOptionFallback);

/* ================================================== SecCertificate CFType == */

struct __SecCertificate {
    CFRuntimeBase base;
    CFDataRef     der;
};

static CFTypeID g_cert_type = _kCFRuntimeNotATypeID;

static void cert_dealloc(CFTypeRef o)
{
    struct __SecCertificate *c = (struct __SecCertificate *)o;
    if (c->der) CFRelease(c->der);
}

static Boolean cert_equal(CFTypeRef a, CFTypeRef b)
{
    struct __SecCertificate *x = (struct __SecCertificate *)a;
    struct __SecCertificate *y = (struct __SecCertificate *)b;
    if (!x->der || !y->der) return x->der == y->der;
    return CFEqual(x->der, y->der);
}

static CFStringRef cert_desc(CFTypeRef o)
{
    return CFStringCreateWithFormat(CFGetAllocator(o), NULL,
                                    CFSTR("<SecCertificate %p>"), o);
}

static const CFRuntimeClass cert_class = {
    0, "SecCertificate", NULL, NULL, cert_dealloc, cert_equal, NULL, NULL,
    cert_desc
};

static void cert_register(void) { g_cert_type = _CFRuntimeRegisterClass(&cert_class); }

CFTypeID SecCertificateGetTypeID(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, cert_register);
    return g_cert_type;
}

static SecCertificateRef cert_make(CFDataRef der)
{
    struct __SecCertificate *c;
    SecCertificateGetTypeID();
    c = (struct __SecCertificate *)_CFRuntimeCreateInstance(
            kCFAllocatorDefault, g_cert_type,
            sizeof(struct __SecCertificate) - sizeof(CFRuntimeBase), NULL);
    if (!c) return NULL;
    c->der = der ? (CFDataRef)CFRetain(der) : NULL;
    return (SecCertificateRef)c;
}

SecCertificateRef SecCertificateCreateWithData(CFAllocatorRef allocator,
                                               CFDataRef data)
{
    (void)allocator;
    if (!data || CFDataGetLength(data) == 0) return NULL;
    return cert_make(data);
}

CFDataRef SecCertificateCopyData(SecCertificateRef cert)
{
    struct __SecCertificate *c = (struct __SecCertificate *)cert;
    if (!c || !c->der) return NULL;
    return (CFDataRef)CFRetain(c->der);
}

/* Display only. Returning the DER length as a name would be worse than
 * returning nothing, so when there is nothing meaningful to say, say nothing. */
CFStringRef SecCertificateCopySubjectSummary(SecCertificateRef cert)
{
    if (!cert) return NULL;
    return CFStringCreateWithCString(kCFAllocatorDefault, "certificate",
                                     kCFStringEncodingUTF8);
}

/* ======================================================== SecTrust CFType == */

struct __SecTrust {
    CFRuntimeBase     base;
    CFMutableArrayRef certs;        /* SecCertificateRef, leaf first */
    CFMutableArrayRef anchors;      /* SecCertificateRef, or NULL */
    int               anchors_only;
    CFStringRef       hostname;     /* from the SSL policy, may be NULL */
    SecTrustResultType result;
    OSStatus          detail;       /* the specific errSSL* we refused with */
    int               evaluated;
};

static CFTypeID g_trust_type = _kCFRuntimeNotATypeID;

static void trust_dealloc(CFTypeRef o)
{
    struct __SecTrust *t = (struct __SecTrust *)o;
    if (t->certs)    CFRelease(t->certs);
    if (t->anchors)  CFRelease(t->anchors);
    if (t->hostname) CFRelease(t->hostname);
}

static CFStringRef trust_desc(CFTypeRef o)
{
    return CFStringCreateWithFormat(CFGetAllocator(o), NULL,
                                    CFSTR("<SecTrust %p>"), o);
}

static const CFRuntimeClass trust_class = {
    0, "SecTrust", NULL, NULL, trust_dealloc, NULL, NULL, NULL, trust_desc
};

static void trust_register(void) { g_trust_type = _CFRuntimeRegisterClass(&trust_class); }

CFTypeID SecTrustGetTypeID(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, trust_register);
    return g_trust_type;
}

/* SecPolicyCreateSSL carries the hostname the caller wants checked. We model a
 * policy as just that string, since the name check is the part that matters. */
struct __SecPolicy {
    CFRuntimeBase base;
    CFStringRef   hostname;
};

static CFTypeID g_policy_type = _kCFRuntimeNotATypeID;

static void policy_dealloc(CFTypeRef o)
{
    struct __SecPolicy *p = (struct __SecPolicy *)o;
    if (p->hostname) CFRelease(p->hostname);
}

static CFStringRef policy_desc(CFTypeRef o)
{
    return CFStringCreateWithFormat(CFGetAllocator(o), NULL,
                                    CFSTR("<SecPolicy %p>"), o);
}

static const CFRuntimeClass policy_class = {
    0, "SecPolicy", NULL, NULL, policy_dealloc, NULL, NULL, NULL, policy_desc
};

static void policy_register(void) { g_policy_type = _CFRuntimeRegisterClass(&policy_class); }

CFTypeID SecPolicyGetTypeID(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, policy_register);
    return g_policy_type;
}

SecPolicyRef SecPolicyCreateSSL(Boolean server, CFStringRef hostname)
{
    struct __SecPolicy *p;
    (void)server;
    SecPolicyGetTypeID();
    p = (struct __SecPolicy *)_CFRuntimeCreateInstance(
            kCFAllocatorDefault, g_policy_type,
            sizeof(struct __SecPolicy) - sizeof(CFRuntimeBase), NULL);
    if (!p) return NULL;
    p->hostname = hostname ? (CFStringRef)CFRetain(hostname) : NULL;
    return (SecPolicyRef)p;
}

SecPolicyRef SecPolicyCreateBasicX509(void)
{
    return SecPolicyCreateSSL(false, NULL);
}

static CFStringRef policy_hostname(CFTypeRef policies)
{
    if (!policies) return NULL;
    if (CFGetTypeID(policies) == SecPolicyGetTypeID())
        return ((struct __SecPolicy *)policies)->hostname;
    if (CFGetTypeID(policies) == CFArrayGetTypeID()) {
        CFIndex i, n = CFArrayGetCount((CFArrayRef)policies);
        for (i = 0; i < n; i++) {
            CFTypeRef p = CFArrayGetValueAtIndex((CFArrayRef)policies, i);
            if (p && CFGetTypeID(p) == SecPolicyGetTypeID() &&
                ((struct __SecPolicy *)p)->hostname)
                return ((struct __SecPolicy *)p)->hostname;
        }
    }
    return NULL;
}

static CFMutableArrayRef array_of(CFTypeRef one_or_many)
{
    CFMutableArrayRef a = CFArrayCreateMutable(kCFAllocatorDefault, 0,
                                               &kCFTypeArrayCallBacks);
    if (!a || !one_or_many) return a;
    if (CFGetTypeID(one_or_many) == CFArrayGetTypeID()) {
        CFIndex i, n = CFArrayGetCount((CFArrayRef)one_or_many);
        for (i = 0; i < n; i++)
            CFArrayAppendValue(a, CFArrayGetValueAtIndex((CFArrayRef)one_or_many, i));
    } else {
        CFArrayAppendValue(a, one_or_many);
    }
    return a;
}

OSStatus SecTrustCreateWithCertificates(CFTypeRef certificates,
                                        CFTypeRef policies,
                                        SecTrustRef *trust)
{
    struct __SecTrust *t;
    CFStringRef host;

    if (!certificates || !trust) return errSecParam;
    *trust = NULL;

    SecTrustGetTypeID();
    t = (struct __SecTrust *)_CFRuntimeCreateInstance(
            kCFAllocatorDefault, g_trust_type,
            sizeof(struct __SecTrust) - sizeof(CFRuntimeBase), NULL);
    if (!t) return errSecAllocate;

    t->certs   = array_of(certificates);
    t->anchors = NULL;
    t->anchors_only = 0;
    t->result  = kSecTrustResultInvalid;
    t->detail  = errSecSuccess;
    t->evaluated = 0;

    host = policy_hostname(policies);
    t->hostname = host ? (CFStringRef)CFRetain(host) : NULL;

    if (!t->certs || CFArrayGetCount(t->certs) == 0) {
        CFRelease((CFTypeRef)t);
        return errSecParam;
    }
    *trust = (SecTrustRef)t;
    return errSecSuccess;
}

OSStatus SecTrustSetAnchorCertificates(SecTrustRef trust, CFArrayRef anchors)
{
    struct __SecTrust *t = (struct __SecTrust *)trust;
    if (!t) return errSecParam;
    if (t->anchors) { CFRelease(t->anchors); t->anchors = NULL; }
    if (anchors) t->anchors = array_of(anchors);
    t->evaluated = 0;
    return errSecSuccess;
}

OSStatus SecTrustSetAnchorCertificatesOnly(SecTrustRef trust,
                                           Boolean anchorCertificatesOnly)
{
    struct __SecTrust *t = (struct __SecTrust *)trust;
    if (!t) return errSecParam;
    t->anchors_only = anchorCertificatesOnly ? 1 : 0;
    t->evaluated = 0;
    return errSecSuccess;
}

CFIndex SecTrustGetCertificateCount(SecTrustRef trust)
{
    struct __SecTrust *t = (struct __SecTrust *)trust;
    return (t && t->certs) ? CFArrayGetCount(t->certs) : 0;
}

SecCertificateRef SecTrustGetCertificateAtIndex(SecTrustRef trust, CFIndex ix)
{
    struct __SecTrust *t = (struct __SecTrust *)trust;
    if (!t || !t->certs) return NULL;
    if (ix < 0 || ix >= CFArrayGetCount(t->certs)) return NULL;
    return (SecCertificateRef)CFArrayGetValueAtIndex(t->certs, ix);
}

/* Pull DER pointers out of an array of SecCertificateRef. The CFDataRefs stay
 * owned by the certificates, which outlive the call. */
static size_t collect_der(CFArrayRef a, const uint8_t **out, size_t *lens,
                          size_t max)
{
    CFIndex i, n;
    size_t k = 0;
    if (!a) return 0;
    n = CFArrayGetCount(a);
    for (i = 0; i < n && k < max; i++) {
        struct __SecCertificate *c =
            (struct __SecCertificate *)CFArrayGetValueAtIndex(a, i);
        if (!c || !c->der) continue;
        out[k]  = (const uint8_t *)CFDataGetBytePtr(c->der);
        lens[k] = (size_t)CFDataGetLength(c->der);
        k++;
    }
    return k;
}

#define ST_TRUST_MAX 24

OSStatus SecTrustEvaluate(SecTrustRef trust, SecTrustResultType *result)
{
    struct __SecTrust *t = (struct __SecTrust *)trust;
    const uint8_t *certs[ST_TRUST_MAX], *anchors[ST_TRUST_MAX];
    size_t clens[ST_TRUST_MAX], alens[ST_TRUST_MAX];
    size_t nc, na;
    char hostbuf[256];
    const char *host = NULL;
    int32_t r;

    if (!t) return errSecParam;
    if (result) *result = kSecTrustResultInvalid;

    nc = collect_der(t->certs, certs, clens, ST_TRUST_MAX);
    if (!nc) return errSecParam;
    na = collect_der(t->anchors, anchors, alens, ST_TRUST_MAX);

    if (t->hostname &&
        CFStringGetCString(t->hostname, hostbuf, sizeof hostbuf,
                           kCFStringEncodingUTF8))
        host = hostbuf;

    r = st_verify_chain(certs, clens, nc,
                        na ? anchors : NULL, na ? alens : NULL, na,
                        t->anchors_only, host, NULL);

    t->detail    = (OSStatus)r;
    t->evaluated = 1;
    /*
     * kSecTrustResultUnspecified is the *success* value - it means "trusted,
     * and no user decision was involved". Callers test for Proceed or
     * Unspecified; anything else is a refusal. Returning Proceed here would
     * claim a user explicitly approved this chain, which never happened.
     */
    t->result = (r == ST_ERR_SUCCESS) ? kSecTrustResultUnspecified
                                      : kSecTrustResultRecoverableTrustFailure;
    if (result) *result = t->result;
    return errSecSuccess;          /* evaluation ran; the verdict is in *result */
}

OSStatus SecTrustGetTrustResult(SecTrustRef trust, SecTrustResultType *result)
{
    struct __SecTrust *t = (struct __SecTrust *)trust;
    if (!t || !result) return errSecParam;
    *result = t->evaluated ? t->result : kSecTrustResultInvalid;
    return errSecSuccess;
}

/* Returns C99 `bool`, not `Boolean` - the SDK differs from the rest of the
 * framework here, and Boolean is unsigned char, so the two do not merge. */
bool SecTrustEvaluateWithError(SecTrustRef trust, CFErrorRef *error)
{
    SecTrustResultType res = kSecTrustResultInvalid;
    struct __SecTrust *t = (struct __SecTrust *)trust;

    if (error) *error = NULL;
    if (SecTrustEvaluate(trust, &res) != errSecSuccess) {
        if (error)
            *error = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainOSStatus,
                                   errSecParam, NULL);
        return false;
    }
    if (res == kSecTrustResultProceed || res == kSecTrustResultUnspecified)
        return true;
    if (error)
        *error = CFErrorCreate(kCFAllocatorDefault, kCFErrorDomainOSStatus,
                               t ? t->detail : errSSLXCertChainInvalid, NULL);
    return false;
}

OSStatus SecTrustSetPolicies(SecTrustRef trust, CFTypeRef policies)
{
    struct __SecTrust *t = (struct __SecTrust *)trust;
    CFStringRef host;
    if (!t) return errSecParam;
    host = policy_hostname(policies);
    if (t->hostname) { CFRelease(t->hostname); t->hostname = NULL; }
    if (host) t->hostname = (CFStringRef)CFRetain(host);
    t->evaluated = 0;
    return errSecSuccess;
}

/* ====================================================== SSLContext CFType == */

struct SSLContext {
    CFRuntimeBase base;
    st_ctx       *core;
};

static CFTypeID g_ssl_type = _kCFRuntimeNotATypeID;

static void ssl_dealloc(CFTypeRef o)
{
    struct SSLContext *s = (struct SSLContext *)o;
    if (s->core) st_free(s->core);
    s->core = NULL;
}

static CFStringRef ssl_desc(CFTypeRef o)
{
    return CFStringCreateWithFormat(CFGetAllocator(o), NULL,
                                    CFSTR("<SSLContext %p>"), o);
}

static const CFRuntimeClass ssl_class = {
    0, "SSLContext", NULL, NULL, ssl_dealloc, NULL, NULL, NULL, ssl_desc
};

static void ssl_register(void) { g_ssl_type = _CFRuntimeRegisterClass(&ssl_class); }

CFTypeID SSLContextGetTypeID(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, ssl_register);
    return g_ssl_type;
}

#define CORE(ctx) (((struct SSLContext *)(ctx)) ? \
                   ((struct SSLContext *)(ctx))->core : NULL)

SSLContextRef SSLCreateContext(CFAllocatorRef alloc, SSLProtocolSide side,
                               SSLConnectionType type)
{
    struct SSLContext *s;
    (void)alloc;
    SSLContextGetTypeID();
    s = (struct SSLContext *)_CFRuntimeCreateInstance(
            kCFAllocatorDefault, g_ssl_type,
            sizeof(struct SSLContext) - sizeof(CFRuntimeBase), NULL);
    if (!s) return NULL;
    s->core = st_new((int)side, (int)type);
    if (!s->core) { CFRelease(s); return NULL; }
    return (SSLContextRef)s;
}

/* Deprecated pre-10.8 spellings. SSLNewContext hands back a +1 reference the
 * caller releases with SSLDisposeContext rather than CFRelease. */
OSStatus SSLNewContext(Boolean isServer, SSLContextRef *contextPtr)
{
    SSLContextRef c;
    if (!contextPtr) return errSecParam;
    c = SSLCreateContext(NULL, isServer ? kSSLServerSide : kSSLClientSide,
                         kSSLStreamType);
    if (!c) return errSecAllocate;
    *contextPtr = c;
    return errSecSuccess;
}

OSStatus SSLDisposeContext(SSLContextRef context)
{
    if (!context) return errSecParam;
    CFRelease(context);
    return errSecSuccess;
}

/* The app's callbacks have Apple's signature; st_core's differ only in spelling
 * of the opaque cookie, so they can be handed straight through. */
OSStatus SSLSetIOFuncs(SSLContextRef ctx, SSLReadFunc readFunc,
                       SSLWriteFunc writeFunc)
{
    return st_set_io_funcs(CORE(ctx), (st_read_fn)readFunc,
                           (st_write_fn)writeFunc);
}

OSStatus SSLSetConnection(SSLContextRef ctx, SSLConnectionRef connection)
{
    return st_set_connection(CORE(ctx), (void *)connection);
}

OSStatus SSLGetConnection(SSLContextRef ctx, SSLConnectionRef *connection)
{
    void *c = NULL;
    OSStatus s;
    if (!connection) return errSecParam;
    s = st_get_connection(CORE(ctx), &c);
    *connection = (SSLConnectionRef)c;
    return s;
}

OSStatus SSLSetPeerDomainName(SSLContextRef ctx, const char *peerName,
                              size_t peerNameLen)
{
    return st_set_peer_domain_name(CORE(ctx), peerName, peerNameLen);
}

OSStatus SSLGetPeerDomainNameLength(SSLContextRef ctx, size_t *peerNameLen)
{
    return st_get_peer_domain_name_length(CORE(ctx), peerNameLen);
}

OSStatus SSLGetPeerDomainName(SSLContextRef ctx, char *peerName,
                              size_t *peerNameLen)
{
    return st_get_peer_domain_name(CORE(ctx), peerName, peerNameLen);
}

OSStatus SSLSetProtocolVersionMin(SSLContextRef ctx, SSLProtocol minVersion)
{
    return st_set_protocol_version_min(CORE(ctx), (int)minVersion);
}

OSStatus SSLSetProtocolVersionMax(SSLContextRef ctx, SSLProtocol maxVersion)
{
    return st_set_protocol_version_max(CORE(ctx), (int)maxVersion);
}

OSStatus SSLGetProtocolVersionMin(SSLContextRef ctx, SSLProtocol *minVersion)
{
    int v = 0;
    OSStatus s;
    if (!minVersion) return errSecParam;
    s = st_get_protocol_version_min(CORE(ctx), &v);
    *minVersion = (SSLProtocol)v;
    return s;
}

OSStatus SSLGetProtocolVersionMax(SSLContextRef ctx, SSLProtocol *maxVersion)
{
    int v = 0;
    OSStatus s;
    if (!maxVersion) return errSecParam;
    s = st_get_protocol_version_max(CORE(ctx), &v);
    *maxVersion = (SSLProtocol)v;
    return s;
}

OSStatus SSLSetSessionOption(SSLContextRef ctx, SSLSessionOption option,
                             Boolean value)
{
    return st_set_session_option(CORE(ctx), (int)option, value ? 1 : 0);
}

OSStatus SSLGetSessionOption(SSLContextRef ctx, SSLSessionOption option,
                             Boolean *value)
{
    int on = 0;
    OSStatus s;
    if (!value) return errSecParam;
    s = st_get_session_option(CORE(ctx), (int)option, &on);
    *value = on ? true : false;
    return s;
}

OSStatus SSLSetEnableCertVerify(SSLContextRef ctx, Boolean enableVerify)
{
    return st_set_enable_cert_verify(CORE(ctx), enableVerify ? 1 : 0);
}

OSStatus SSLGetEnableCertVerify(SSLContextRef ctx, Boolean *enableVerify)
{
    int on = 0;
    OSStatus s;
    if (!enableVerify) return errSecParam;
    s = st_get_enable_cert_verify(CORE(ctx), &on);
    *enableVerify = on ? true : false;
    return s;
}

/*
 * SSLSetAllowsAnyRoot / SSLSetAllowsExpiredCerts are deprecated APIs that exist
 * to weaken verification. They are honoured faithfully rather than ignored: an
 * app that asks for them and is silently given strict checking fails in a way
 * its author cannot debug. Turning verification off entirely is the closest
 * honest mapping we have, and it is the caller's explicit request.
 */
OSStatus SSLSetAllowsAnyRoot(SSLContextRef ctx, Boolean anyRoot)
{
    if (!anyRoot) return errSecSuccess;
    return st_set_enable_cert_verify(CORE(ctx), 0);
}

OSStatus SSLSetAllowsExpiredCerts(SSLContextRef ctx, Boolean allowExpired)
{
    if (!allowExpired) return errSecSuccess;
    return st_set_enable_cert_verify(CORE(ctx), 0);
}

OSStatus SSLSetAllowsExpiredRoots(SSLContextRef ctx, Boolean allowExpired)
{
    if (!allowExpired) return errSecSuccess;
    return st_set_enable_cert_verify(CORE(ctx), 0);
}

OSStatus SSLGetAllowsAnyRoot(SSLContextRef ctx, Boolean *anyRoot)
{
    int on = 1;
    if (!anyRoot) return errSecParam;
    st_get_enable_cert_verify(CORE(ctx), &on);
    *anyRoot = on ? false : true;
    return errSecSuccess;
}

OSStatus SSLHandshake(SSLContextRef ctx)
{
    return st_handshake(CORE(ctx));
}

OSStatus SSLRead(SSLContextRef ctx, void *data, size_t dataLength,
                 size_t *processed)
{
    size_t got = 0;
    OSStatus s;
    if (!processed) return errSecParam;
    s = st_read(CORE(ctx), data, dataLength, &got);
    *processed = got;
    return s;
}

OSStatus SSLWrite(SSLContextRef ctx, const void *data, size_t dataLength,
                  size_t *processed)
{
    size_t put = 0;
    OSStatus s;
    if (!processed) return errSecParam;
    s = st_write(CORE(ctx), data, dataLength, &put);
    *processed = put;
    return s;
}

OSStatus SSLClose(SSLContextRef ctx)
{
    return st_close(CORE(ctx));
}

OSStatus SSLGetSessionState(SSLContextRef ctx, SSLSessionState *state)
{
    int st = 0;
    OSStatus s;
    if (!state) return errSecParam;
    s = st_get_session_state(CORE(ctx), &st);
    *state = (SSLSessionState)st;
    return s;
}

OSStatus SSLGetNegotiatedProtocolVersion(SSLContextRef ctx, SSLProtocol *protocol)
{
    int v = 0;
    OSStatus s;
    if (!protocol) return errSecParam;
    s = st_get_negotiated_protocol_version(CORE(ctx), &v);
    *protocol = (SSLProtocol)v;
    return s;
}

OSStatus SSLGetNegotiatedCipher(SSLContextRef ctx, SSLCipherSuite *cipherSuite)
{
    uint32_t c = 0;
    OSStatus s;
    if (!cipherSuite) return errSecParam;
    s = st_get_negotiated_cipher(CORE(ctx), &c);
    *cipherSuite = (SSLCipherSuite)c;
    return s;
}

/* Not cosmetic: a select()-driven app stalls forever if plaintext is sitting
 * buffered inside the TLS layer while the socket itself reads not-readable. */
OSStatus SSLGetBufferedReadSize(SSLContextRef ctx, size_t *bufSize)
{
    return st_get_buffered_read_size(CORE(ctx), bufSize);
}

OSStatus SSLGetClientCertificateState(SSLContextRef ctx,
                                      SSLClientCertificateState *clientState)
{
    int st = 0;
    OSStatus s;
    if (!clientState) return errSecParam;
    s = st_get_client_certificate_state(CORE(ctx), &st);
    *clientState = (SSLClientCertificateState)st;
    return s;
}

/* Session resumption is not implemented. Accepting the id and doing nothing is
 * correct behaviour - it costs a full handshake, nothing more. */
OSStatus SSLSetPeerID(SSLContextRef ctx, const void *peerID, size_t peerIDLen)
{
    (void)peerID; (void)peerIDLen;
    return CORE(ctx) ? errSecSuccess : errSecParam;
}

OSStatus SSLGetPeerID(SSLContextRef ctx, const void **peerID, size_t *peerIDLen)
{
    if (!peerID || !peerIDLen) return errSecParam;
    *peerID = NULL; *peerIDLen = 0;
    return CORE(ctx) ? errSecSuccess : errSecParam;
}

static CFArrayRef copy_peer_certs(st_ctx *core)
{
    CFMutableArrayRef a;
    size_t i, n;

    if (!core) return NULL;
    n = st_peer_cert_count(core);
    if (!n) return NULL;

    a = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (!a) return NULL;
    for (i = 0; i < n; i++) {
        const uint8_t *der = NULL;
        size_t len = 0;
        CFDataRef d;
        SecCertificateRef c;
        if (st_peer_cert_der(core, i, &der, &len) != ST_ERR_SUCCESS) continue;
        d = CFDataCreate(kCFAllocatorDefault, der, (CFIndex)len);
        if (!d) continue;
        c = SecCertificateCreateWithData(NULL, d);
        CFRelease(d);
        if (!c) continue;
        CFArrayAppendValue(a, c);
        CFRelease(c);
    }
    return a;
}

OSStatus SSLCopyPeerTrust(SSLContextRef ctx, SecTrustRef *trust)
{
    st_ctx *core = CORE(ctx);
    CFArrayRef certs;
    SecPolicyRef policy;
    CFStringRef host = NULL;
    OSStatus s;
    size_t hlen = 0;

    if (!trust) return errSecParam;
    *trust = NULL;
    if (!core) return errSecParam;

    certs = copy_peer_certs(core);
    if (!certs) return errSSLBadCert;

    /* Carry the peer name we were told to expect into the trust object, so an
     * app that evaluates the chain itself still gets the hostname checked. */
    if (st_get_peer_domain_name_length(core, &hlen) == ST_ERR_SUCCESS && hlen > 1) {
        char *buf = malloc(hlen);
        size_t got = hlen;
        if (buf && st_get_peer_domain_name(core, buf, &got) == ST_ERR_SUCCESS)
            host = CFStringCreateWithCString(kCFAllocatorDefault, buf,
                                             kCFStringEncodingUTF8);
        free(buf);
    }

    policy = SecPolicyCreateSSL(false, host);
    if (host) CFRelease(host);

    s = SecTrustCreateWithCertificates(certs, policy, trust);
    CFRelease(certs);
    if (policy) CFRelease(policy);
    return s;
}

OSStatus SSLCopyPeerCertificates(SSLContextRef ctx, CFArrayRef *certs)
{
    CFArrayRef a;
    if (!certs) return errSecParam;
    a = copy_peer_certs(CORE(ctx));
    *certs = a;
    return a ? errSecSuccess : errSSLBadCert;
}

OSStatus SSLSetTrustedRoots(SSLContextRef ctx, CFArrayRef trustedRoots,
                            Boolean replaceExisting)
{
    /* Anchors supplied as SecCertificateRefs need a PEM/DER bridge into the
     * core's store. Until that is wired, refuse rather than accept the call and
     * quietly verify against the system store instead - which would look like
     * pinning while pinning nothing. */
    (void)ctx; (void)trustedRoots; (void)replaceExisting;
    return errSecUnimplemented;
}
