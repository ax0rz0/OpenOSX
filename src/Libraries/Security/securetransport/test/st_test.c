/*
 * st_test.c - run st_core against a real TLS peer, on the build host.
 *
 * This is the reason st_core.c has no CoreFoundation in it. Everything here
 * runs natively on Linux, so the TLS logic gets exercised against a genuine
 * OpenSSL server with genuine certificates before any of it is cross-compiled
 * or booted in a VM.
 *
 * The negative cases carry the weight. A TLS layer that accepts an expired
 * certificate, an unknown issuer, or a mismatched hostname is worse than no
 * TLS, because the caller believes it is protected - so the tests that must
 * FAIL are the ones worth writing first, and a "pass" here means we correctly
 * refused.
 *
 * The peer is a plain-OpenSSL server on the other end of a socketpair rather
 * than a spawned s_server: no ports to collide on, no processes to reap, and
 * the whole test is deterministic. The client fd is O_NONBLOCK precisely so the
 * would-block paths - the ones that turn into hangs when they are wrong - are
 * hit constantly rather than by luck.
 */
#include "../st_core.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

static const char *g_fixture_dir = "fixtures";
static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, fmt, ...)                                                 \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            printf("    FAIL  " fmt "\n", ##__VA_ARGS__);                     \
        }                                                                     \
    } while (0)

static char *slurp(const char *name, size_t *len)
{
    char path[512], *buf;
    long n;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", g_fixture_dir, name);
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { exit(2); }
    buf[n] = '\0';
    fclose(f);
    if (len) *len = (size_t)n;
    return buf;
}

/* ------------------------------------------------------------- the peer -- */

struct server_arg {
    int   fd;
    char  cert[64];
    char  key[64];
    int   accepted;         /* 1 if SSL_accept succeeded */
    int   echoed;
};

static void *server_thread(void *v)
{
    struct server_arg *a = v;
    SSL_CTX *ctx;
    SSL     *ssl;
    char     path[512];
    char     buf[512];
    int      n;

    ctx = SSL_CTX_new(TLS_server_method());
    snprintf(path, sizeof path, "%s/%s", g_fixture_dir, a->cert);
    if (SSL_CTX_use_certificate_file(ctx, path, SSL_FILETYPE_PEM) != 1) goto out;
    snprintf(path, sizeof path, "%s/%s", g_fixture_dir, a->key);
    if (SSL_CTX_use_PrivateKey_file(ctx, path, SSL_FILETYPE_PEM) != 1) goto out;

    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, a->fd);
    if (SSL_accept(ssl) == 1) {
        a->accepted = 1;
        n = SSL_read(ssl, buf, sizeof buf);
        if (n > 0) { SSL_write(ssl, buf, n); a->echoed = 1; }
        SSL_shutdown(ssl);
    }
    SSL_free(ssl);
out:
    SSL_CTX_free(ctx);
    close(a->fd);
    return NULL;
}

/* ------------------------------------------------- the app's I/O callbacks -
 * Deliberately faithful to Apple's contract, including the part that catches
 * people out: dataLength is IN/OUT, and a short transfer is reported as
 * errSSLWouldBlock *together with* the bytes that did move.
 */
static int32_t cb_read(void *conn, void *data, size_t *len)
{
    int     fd = (int)(intptr_t)conn;
    size_t  want = *len, got = 0;
    ssize_t n;

    while (got < want) {
        n = recv(fd, (char *)data + got, want - got, 0);
        if (n > 0) { got += (size_t)n; continue; }
        if (n == 0) { *len = got; return got ? ST_ERR_WOULDBLOCK
                                             : ST_ERR_CLOSED_GRACEFUL; }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            *len = got;
            return ST_ERR_WOULDBLOCK;
        }
        *len = got;
        return ST_ERR_CLOSED_ABORT;
    }
    *len = got;
    return ST_ERR_SUCCESS;
}

static int32_t cb_write(void *conn, const void *data, size_t *len)
{
    int     fd = (int)(intptr_t)conn;
    size_t  want = *len, put = 0;
    ssize_t n;

    while (put < want) {
        /* MSG_NOSIGNAL: writing to a peer that has gone away must surface as
         * EPIPE we can report, not a signal that kills the process. */
        n = send(fd, (const char *)data + put, want - put, MSG_NOSIGNAL);
        if (n > 0) { put += (size_t)n; continue; }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            *len = put;
            return ST_ERR_WOULDBLOCK;
        }
        *len = put;
        return ST_ERR_CLOSED_ABORT;
    }
    *len = put;
    return ST_ERR_SUCCESS;
}

/* --------------------------------------------------------------- driver -- */

struct outcome {
    int32_t handshake;      /* final status */
    int32_t auth_break;     /* status seen at the BreakOnServerAuth pause, or 0 */
    int32_t trust;          /* st_peer_trust_result() at the pause */
    size_t  chain;          /* certificates the peer sent */
    int     echoed;         /* application data made it there and back */
    int     post_handshake_break;
};

/*
 * Run one connection. `leaf` names the fixture the server presents; `host` is
 * what the client claims to be talking to; `break_auth` turns on
 * kSSLSessionOptionBreakOnServerAuth, and `approve` says whether the app then
 * chooses to continue.
 */
static struct outcome run(const char *leaf, const char *host, int break_auth,
                          int approve, int verify)
{
    struct outcome     o;
    struct server_arg  sa;
    pthread_t          th;
    int                sv[2];
    st_ctx            *c;
    char              *ca;
    size_t             ca_len;
    int                spins = 0;

    memset(&o, 0, sizeof o);
    memset(&sa, 0, sizeof sa);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { perror("socketpair"); exit(2); }
    snprintf(sa.cert, sizeof sa.cert, "%s.pem", leaf);
    snprintf(sa.key,  sizeof sa.key,  "%s.key", leaf);
    sa.fd = sv[1];
    pthread_create(&th, NULL, server_thread, &sa);

    fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);

    c = st_new(ST_SIDE_CLIENT, ST_TYPE_STREAM);
    st_set_io_funcs(c, cb_read, cb_write);
    st_set_connection(c, (void *)(intptr_t)sv[0]);
    st_set_peer_domain_name(c, host, strlen(host));
    st_set_enable_cert_verify(c, verify);
    if (break_auth) st_set_session_option(c, ST_OPT_BREAK_ON_SERVER_AUTH, 1);

    ca = slurp("ca.pem", &ca_len);
    st_set_trusted_roots_pem(c, ca, ca_len, 1 /* replace system roots */);
    free(ca);

    for (;;) {
        int32_t s = st_handshake(c);
        if (s == ST_ERR_WOULDBLOCK) {
            if (++spins > 200000) { o.handshake = -1; break; }   /* hang guard */
            continue;
        }
        if (s == ST_ERR_PEER_AUTH_COMPLETED) {
            o.auth_break = s;
            o.trust      = st_peer_trust_result(c);
            o.chain      = st_peer_cert_count(c);
            if (!approve) { o.handshake = s; break; }
            continue;                       /* calling again = app approves */
        }
        o.handshake = s;
        break;
    }

    if (o.handshake == ST_ERR_SUCCESS) {
        static const char msg[] = "openosx";
        char  back[64];
        size_t n = 0;
        int32_t s;
        if (!o.chain) o.chain = st_peer_cert_count(c);
        do { s = st_write(c, msg, sizeof msg - 1, &n); } while (s == ST_ERR_WOULDBLOCK && n == 0);
        n = 0;
        do { s = st_read(c, back, sizeof msg - 1, &n); } while (s == ST_ERR_WOULDBLOCK && n == 0);
        if (n == sizeof msg - 1 && memcmp(back, msg, n) == 0) o.echoed = 1;
        st_close(c);
    }

    st_free(c);
    close(sv[0]);
    pthread_join(th, NULL);
    return o;
}

static const char *errname(int32_t e)
{
    switch (e) {
    case ST_ERR_SUCCESS:            return "success";
    case ST_ERR_WOULDBLOCK:         return "errSSLWouldBlock";
    case ST_ERR_CERT_EXPIRED:       return "errSSLCertExpired";
    case ST_ERR_CERT_NOT_YET_VALID: return "errSSLCertNotYetValid";
    case ST_ERR_UNKNOWN_ROOT_CERT:  return "errSSLUnknownRootCert";
    case ST_ERR_NO_ROOT_CERT:       return "errSSLNoRootCert";
    case ST_ERR_HOST_NAME_MISMATCH: return "errSSLHostNameMismatch";
    case ST_ERR_XCERT_CHAIN_INVALID:return "errSSLXCertChainInvalid";
    case ST_ERR_PEER_AUTH_COMPLETED:return "errSSLPeerAuthCompleted";
    case ST_ERR_HANDSHAKE_FAIL:     return "errSSLHandshakeFail";
    case ST_ERR_CLOSED_ABORT:       return "errSSLClosedAbort";
    case ST_ERR_CLOSED_GRACEFUL:    return "errSSLClosedGraceful";
    case ST_ERR_BADREQ:             return "errSecBadReq";
    case ST_ERR_PARAM:              return "errSecParam";
    default:                        return "?";
    }
}

/* ---------------------------------------------- standalone trust evaluation -
 * This is what curl actually relies on: it puts Secure Transport in
 * BreakOnServerAuth mode and then judges the chain itself through
 * SecTrustEvaluate. So these cases decide whether a curl-linked app is
 * protected, regardless of how well SSLHandshake behaves.
 */
static unsigned char *pem_to_der(const char *name, size_t *len)
{
    char  path[512];
    BIO  *b;
    X509 *x;
    unsigned char *der = NULL;
    int   n;

    snprintf(path, sizeof path, "%s/%s", g_fixture_dir, name);
    b = BIO_new_file(path, "r");
    if (!b) { fprintf(stderr, "cannot open %s\n", path); exit(2); }
    x = PEM_read_bio_X509(b, NULL, NULL, NULL);
    BIO_free(b);
    if (!x) { fprintf(stderr, "cannot parse %s\n", path); exit(2); }
    n = i2d_X509(x, &der);
    X509_free(x);
    if (n <= 0) exit(2);
    *len = (size_t)n;
    return der;
}

static void test_trust(void)
{
    unsigned char *ca, *good, *expired, *self, *wrong;
    size_t ca_n, good_n, exp_n, self_n, wrong_n;
    const uint8_t *chain[1], *anchors[1];
    size_t chain_n[1], anchor_n[1];
    int32_t r;

    ca      = pem_to_der("ca.pem",         &ca_n);
    good    = pem_to_der("good.pem",       &good_n);
    expired = pem_to_der("expired.pem",    &exp_n);
    self    = pem_to_der("selfsigned.pem", &self_n);
    wrong   = pem_to_der("wronghost.pem",  &wrong_n);

    anchors[0] = ca;  anchor_n[0] = ca_n;

#define TRUST(leaf, leaf_n, host, only)                                        \
    (chain[0] = (leaf), chain_n[0] = (leaf_n),                                 \
     st_verify_chain(chain, chain_n, 1, anchors, anchor_n, 1, (only), (host),  \
                     NULL))

    r = TRUST(good, good_n, "localhost", 1);
    CHECK(r == ST_ERR_SUCCESS, "good chain rejected: %s", errname(r));
    printf("    good vs our CA                -> %s\n", errname(r));

    r = TRUST(expired, exp_n, "localhost", 1);
    CHECK(r == ST_ERR_CERT_EXPIRED, "expected errSSLCertExpired, got %s",
          errname(r));
    printf("    expired vs our CA             -> %s\n", errname(r));

    r = TRUST(self, self_n, "localhost", 1);
    CHECK(r != ST_ERR_SUCCESS, "self-signed accepted against our CA");
    printf("    self-signed vs our CA         -> %s\n", errname(r));

    r = TRUST(wrong, wrong_n, "localhost", 1);
    CHECK(r == ST_ERR_HOST_NAME_MISMATCH,
          "expected errSSLHostNameMismatch, got %s", errname(r));
    printf("    wrong hostname vs our CA      -> %s\n", errname(r));

    /* Pinning: anchors_only must mean *only*. If a real chain that the system
     * store would happily accept still passes here when we pinned to an
     * unrelated anchor, pinning is decorative. */
    {
        const uint8_t *only_self[1]; size_t only_self_n[1];
        only_self[0] = self; only_self_n[0] = self_n;
        chain[0] = good; chain_n[0] = good_n;
        r = st_verify_chain(chain, chain_n, 1, only_self, only_self_n, 1,
                            1 /* anchors_only */, "localhost", NULL);
        CHECK(r != ST_ERR_SUCCESS,
              "pinned to an unrelated anchor but the chain still verified");
        printf("    good pinned to WRONG anchor   -> %s\n", errname(r));
    }

    /* No hostname means no name check - correct, but only because the caller
     * explicitly asked for that. Recorded so the behaviour is deliberate. */
    r = TRUST(wrong, wrong_n, NULL, 1);
    CHECK(r == ST_ERR_SUCCESS,
          "chain valid but rejected when no hostname was requested: %s",
          errname(r));
    printf("    wrong hostname, no name check -> %s\n", errname(r));

#undef TRUST
    OPENSSL_free(ca); OPENSSL_free(good); OPENSSL_free(expired);
    OPENSSL_free(self); OPENSSL_free(wrong);
}

int main(int argc, char **argv)
{
    struct outcome o;

    if (argc > 1) g_fixture_dir = argv[1];
    /* Unbuffered: if a case does crash, the output up to that point is what
     * says which one, and block buffering throws exactly that away. */
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGPIPE, SIG_IGN);
    SSL_library_init();
    SSL_load_error_strings();

    printf("st_core against a real TLS peer (fixtures: %s)\n\n", g_fixture_dir);

    /* --- the positive case, so we know the harness itself works ---------- */
    printf("  valid certificate, correct hostname\n");
    o = run("good", "localhost", 0, 0, 1);
    CHECK(o.handshake == ST_ERR_SUCCESS,
          "expected success, got %s (%d)", errname(o.handshake), o.handshake);
    CHECK(o.chain >= 1, "expected a peer chain, got %zu certificates", o.chain);
    CHECK(o.echoed, "application data did not survive the round trip");
    printf("    -> %s, chain=%zu, echo=%s\n",
           errname(o.handshake), o.chain, o.echoed ? "ok" : "no");

    /* --- the cases that must be refused ---------------------------------- */
    printf("  expired certificate  (must be refused)\n");
    o = run("expired", "localhost", 0, 0, 1);
    CHECK(o.handshake == ST_ERR_CERT_EXPIRED,
          "expected errSSLCertExpired, got %s (%d)",
          errname(o.handshake), o.handshake);
    CHECK(!o.echoed, "data flowed over a connection that should have failed");
    printf("    -> %s\n", errname(o.handshake));

    printf("  untrusted self-signed certificate  (must be refused)\n");
    o = run("selfsigned", "localhost", 0, 0, 1);
    CHECK(o.handshake == ST_ERR_UNKNOWN_ROOT_CERT ||
          o.handshake == ST_ERR_NO_ROOT_CERT ||
          o.handshake == ST_ERR_XCERT_CHAIN_INVALID,
          "expected an untrusted-root error, got %s (%d)",
          errname(o.handshake), o.handshake);
    CHECK(!o.echoed, "data flowed over a connection that should have failed");
    printf("    -> %s\n", errname(o.handshake));

    printf("  valid certificate, WRONG hostname  (must be refused)\n");
    o = run("wronghost", "localhost", 0, 0, 1);
    CHECK(o.handshake == ST_ERR_HOST_NAME_MISMATCH,
          "expected errSSLHostNameMismatch, got %s (%d)",
          errname(o.handshake), o.handshake);
    CHECK(!o.echoed, "data flowed to a server presenting the wrong name");
    printf("    -> %s\n", errname(o.handshake));

    /* --- BreakOnServerAuth: the app decides ------------------------------ */
    printf("  BreakOnServerAuth, app rejects an untrusted certificate\n");
    o = run("selfsigned", "localhost", 1, 0, 1);
    CHECK(o.auth_break == ST_ERR_PEER_AUTH_COMPLETED,
          "expected a break, got %s", errname(o.auth_break));
    CHECK(o.trust != ST_ERR_SUCCESS,
          "the app was told an untrusted chain was fine (trust=%s)",
          errname(o.trust));
    CHECK(o.chain >= 1, "no chain was available at the break");
    CHECK(!o.echoed, "data flowed after the app rejected the certificate");
    printf("    -> break=%s, verdict handed to app=%s, chain=%zu\n",
           errname(o.auth_break), errname(o.trust), o.chain);

    printf("  BreakOnServerAuth, app accepts the same certificate\n");
    o = run("selfsigned", "localhost", 1, 1, 1);
    CHECK(o.handshake == ST_ERR_SUCCESS,
          "app approved but the handshake still failed: %s",
          errname(o.handshake));
    CHECK(o.echoed, "app approved but no data flowed");
    printf("    -> %s, echo=%s\n", errname(o.handshake),
           o.echoed ? "ok" : "no");

    /* --- verification off is the caller's explicit choice ---------------- */
    printf("  verification disabled, untrusted certificate accepted\n");
    o = run("selfsigned", "localhost", 0, 0, 0);
    CHECK(o.handshake == ST_ERR_SUCCESS,
          "expected success with verification off, got %s",
          errname(o.handshake));
    printf("    -> %s\n", errname(o.handshake));

    /* --- SecTrustEvaluate's engine, standalone --------------------------- */
    printf("\n  standalone chain verification (SecTrustEvaluate's engine)\n");
    test_trust();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
