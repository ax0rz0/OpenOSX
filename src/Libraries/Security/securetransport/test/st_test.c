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
        n = send(fd, (const char *)data + put, want - put, 0);
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

int main(int argc, char **argv)
{
    struct outcome o;

    if (argc > 1) g_fixture_dir = argv[1];
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

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
