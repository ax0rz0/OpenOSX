/*
 * st_core.c - Secure Transport's engine, as plain C over OpenSSL.
 *
 * See st_core.h for why this file has no CoreFoundation in it.
 *
 * Two things here are easy to get subtly wrong, and both fail in ways that look
 * like something else:
 *
 *   1. The would-block mapping. Apple's SSLReadFunc/SSLWriteFunc use an IN/OUT
 *      length and return errSSLWouldBlock for a *short* transfer, not only for
 *      a zero-byte one. A BIO that treats "short" as "error" turns a working
 *      non-blocking socket into a hang. So: any non-zero transfer is success as
 *      far as the BIO is concerned, and only a genuine zero-byte would-block
 *      sets the retry flag.
 *
 *   2. Trust. OpenSSL reports one failure at a time through the verify
 *      callback, and by default does not check the hostname at all. Both are
 *      handled explicitly below: the hostname goes through
 *      X509_VERIFY_PARAM_set1_host so it is verified as part of chain building,
 *      and the X509_V_ERR_* code is captured and translated into the specific
 *      errSSL* code the calling app expects.
 */
#include "st_core.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/bio.h>
#include <openssl/pem.h>

#define ST_MAX_CHAIN 16

struct st_ctx {
    SSL_CTX      *ssl_ctx;
    SSL          *ssl;
    BIO          *bio;              /* owned by ssl once set */

    int           side;             /* ST_SIDE_* */
    int           conn_type;        /* ST_TYPE_* */
    int           state;            /* ST_STATE_* */

    st_read_fn    read_fn;
    st_write_fn   write_fn;
    void         *conn;

    char         *peer_name;        /* NUL-terminated copy */
    size_t        peer_name_len;

    int           proto_min;
    int           proto_max;

    int           opt_break_server_auth;
    int           opt_break_cert_requested;
    int           opt_break_client_auth;
    int           opt_false_start;
    int           opt_one_byte_record;
    int           opt_allow_identity_change;
    int           opt_fallback;
    int           opt_break_client_hello;

    int           verify_enabled;
    int           roots_configured;
    char         *default_ca_file;

    /* Set by the verify callback: the first error OpenSSL hit, kept because
     * SSL_get_verify_result() only survives to the end of the handshake and we
     * want the specific code even when we abort mid-handshake. */
    int           x509_err;
    int           reported_auth_completed;
    int32_t       trust_result;
    int32_t       io_err;           /* error the app's callback handed us */
    int           saw_eof;          /* peer closed the transport under us */
    int           auth_break_is_post_handshake;   /* retry_verify unavailable */

    /* Peer chain, DER, leaf first. */
    unsigned char *chain_der[ST_MAX_CHAIN];
    size_t         chain_len[ST_MAX_CHAIN];
    size_t         chain_count;

    char          errbuf[256];
};

/* ------------------------------------------------------------------ BIO -- */

static int st_bio_write_cb(BIO *b, const char *buf, int len)
{
    st_ctx *c = (st_ctx *)BIO_get_data(b);
    size_t n;
    int32_t s;

    BIO_clear_retry_flags(b);
    if (!c || !c->write_fn || len < 0)
        return -1;

    n = (size_t)len;
    s = c->write_fn(c->conn, buf, &n);

    /* Any progress at all is progress. A short write reports would-block with a
     * non-zero length, and telling OpenSSL "error" there would strand the
     * connection. */
    if (n > 0)
        return (int)n;
    if (s == ST_ERR_WOULDBLOCK) {
        BIO_set_retry_write(b);
        return -1;
    }
    c->io_err = s ? s : ST_ERR_IO;
    return -1;
}

static int st_bio_read_cb(BIO *b, char *buf, int len)
{
    st_ctx *c = (st_ctx *)BIO_get_data(b);
    size_t n;
    int32_t s;

    BIO_clear_retry_flags(b);
    if (!c || !c->read_fn || len < 0)
        return -1;

    n = (size_t)len;
    s = c->read_fn(c->conn, buf, &n);

    if (n > 0)
        return (int)n;
    if (s == ST_ERR_WOULDBLOCK) {
        BIO_set_retry_read(b);
        return -1;
    }
    if (s == ST_ERR_CLOSED_GRACEFUL || s == ST_ERR_SUCCESS) {
        /* EOF. Retry flags stay cleared - that is exactly what distinguishes
         * "the peer is gone" from "nothing to read yet" at the SSL_get_error
         * layer, and getting it wrong turns a dropped connection into a spin. */
        c->saw_eof = 1;
        return 0;
    }
    c->io_err = s;
    return -1;
}

static long st_bio_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    st_ctx *c = (st_ctx *)BIO_get_data(b);
    (void)num; (void)ptr;
    switch (cmd) {
    case BIO_CTRL_FLUSH:            return 1;   /* we never buffer */
    case BIO_CTRL_EOF:              return c ? c->saw_eof : 0;
    case BIO_CTRL_PUSH:
    case BIO_CTRL_POP:              return 0;
    default:                        return 0;
    }
}

static int st_bio_create(BIO *b)
{
    BIO_set_init(b, 1);
    BIO_set_data(b, NULL);
    BIO_clear_retry_flags(b);
    return 1;
}

static int st_bio_destroy(BIO *b)
{
    if (!b) return 0;
    BIO_set_data(b, NULL);
    BIO_set_init(b, 0);
    return 1;
}

static BIO_METHOD *st_bio_method(void)
{
    static BIO_METHOD *m;           /* leaked once, deliberately */
    if (!m) {
        m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK,
                         "OpenOSX SecureTransport");
        if (!m) return NULL;
        BIO_meth_set_write(m, st_bio_write_cb);
        BIO_meth_set_read(m, st_bio_read_cb);
        BIO_meth_set_ctrl(m, st_bio_ctrl);
        BIO_meth_set_create(m, st_bio_create);
        BIO_meth_set_destroy(m, st_bio_destroy);
    }
    return m;
}

/* -------------------------------------------------------------- verify -- */

/* Map OpenSSL's chain error onto the specific errSSL* code an app expects.
 * Collapsing everything to one generic code is what makes ported TLS stacks
 * report "handshake failed" for an expired certificate, so each case that has a
 * dedicated Apple code gets one. */
static int32_t st_map_x509_err(int e)
{
    switch (e) {
    case X509_V_OK:
        return ST_ERR_SUCCESS;
    case X509_V_ERR_CERT_HAS_EXPIRED:
    case X509_V_ERR_CRL_HAS_EXPIRED:
        return ST_ERR_CERT_EXPIRED;
    case X509_V_ERR_CERT_NOT_YET_VALID:
    case X509_V_ERR_CRL_NOT_YET_VALID:
        return ST_ERR_CERT_NOT_YET_VALID;
    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
        return ST_ERR_UNKNOWN_ROOT_CERT;
    case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
        return ST_ERR_NO_ROOT_CERT;
    case X509_V_ERR_HOSTNAME_MISMATCH:
        return ST_ERR_HOST_NAME_MISMATCH;
    case X509_V_ERR_INVALID_CA:
    case X509_V_ERR_INVALID_PURPOSE:
    case X509_V_ERR_CERT_UNTRUSTED:
    case X509_V_ERR_CERT_REJECTED:
        return ST_ERR_XCERT_CHAIN_INVALID;
    default:
        return ST_ERR_XCERT_CHAIN_INVALID;
    }
}

static int st_ssl_ctx_index(void)
{
    static int idx = -1;
    if (idx < 0)
        idx = SSL_get_ex_new_index(0, (void *)"st_ctx", NULL, NULL, NULL);
    return idx;
}

static int st_verify_cb(int preverify_ok, X509_STORE_CTX *store)
{
    SSL    *ssl;
    st_ctx *c;
    int     err, depth;

    ssl = X509_STORE_CTX_get_ex_data(
              store, SSL_get_ex_data_X509_STORE_CTX_idx());
    c   = ssl ? (st_ctx *)SSL_get_ex_data(ssl, st_ssl_ctx_index()) : NULL;
    err = X509_STORE_CTX_get_error(store);
    depth = X509_STORE_CTX_get_error_depth(store);

    if (!preverify_ok && c && c->x509_err == X509_V_OK)
        c->x509_err = err;          /* keep the first, most specific failure */

    if (!c)
        return preverify_ok;

    /* Verification switched off by the caller (curl -k and friends). */
    if (!c->verify_enabled)
        return 1;

    /*
     * kSSLSessionOptionBreakOnServerAuth: the app evaluates trust itself. Chain
     * building runs root-first, so depth 0 is the leaf and therefore the last
     * callback - that is the point at which the whole chain has been seen and
     * the handshake can be suspended.
     *
     * SSL_set_retry_verify() suspends it properly, mid-handshake, exactly where
     * Apple breaks. That is worth using rather than finishing the handshake and
     * breaking afterwards, because it means nothing further is sent to a peer
     * the app has not yet approved.
     */
    if (c->opt_break_server_auth) {
        if (depth == 0 && !c->reported_auth_completed) {
            /* Per OpenSSL's SSL_CTX_set_verify(3): call SSL_set_retry_verify()
             * and return *1*. Returning -1 or 0 aborts the handshake instead of
             * suspending it - which is exactly what it did here before this was
             * checked against the documentation rather than assumed. */
            if (SSL_set_retry_verify(ssl))
                return 1;           /* suspended; SSL_ERROR_WANT_RETRY_VERIFY */
            /* Could not suspend. Fall back to accepting here and breaking after
             * the handshake; st_handshake still reports the trust result before
             * any application data moves. */
            c->auth_break_is_post_handshake = 1;
        }
        /* Once we have handed the app its verdict and it has called
         * SSLHandshake again, that call *is* its approval - so continue. The
         * app owning the decision is the entire point of this option. */
        return 1;
    }
    return preverify_ok;
}

/* --------------------------------------------------------------- setup -- */

static void st_seterr(st_ctx *c, const char *what)
{
    unsigned long e = ERR_peek_last_error();
    if (e)
        snprintf(c->errbuf, sizeof c->errbuf, "%s: %s", what,
                 ERR_reason_error_string(e) ? ERR_reason_error_string(e) : "?");
    else
        snprintf(c->errbuf, sizeof c->errbuf, "%s", what);
}

st_ctx *st_new(int side, int conn_type)
{
    st_ctx *c;
    const SSL_METHOD *m;

    if (side != ST_SIDE_CLIENT && side != ST_SIDE_SERVER)
        return NULL;
    /* DTLS is accepted into the struct but not wired up; st_handshake refuses
     * it rather than silently doing TLS over a datagram socket. */
    if (conn_type != ST_TYPE_STREAM && conn_type != ST_TYPE_DATAGRAM)
        return NULL;

    c = calloc(1, sizeof *c);
    if (!c) return NULL;

    c->side       = side;
    c->conn_type  = conn_type;
    c->state      = ST_STATE_IDLE;
    c->proto_min  = ST_PROTO_TLS1;
    c->proto_max  = ST_PROTO_TLS13;
    c->verify_enabled = 1;
    c->x509_err   = X509_V_OK;
    c->trust_result = ST_ERR_SUCCESS;
    c->opt_allow_identity_change = 0;

    m = (side == ST_SIDE_CLIENT) ? TLS_client_method() : TLS_server_method();
    c->ssl_ctx = SSL_CTX_new(m);
    if (!c->ssl_ctx) { free(c); return NULL; }

    SSL_CTX_set_verify(c->ssl_ctx, SSL_VERIFY_PEER, st_verify_cb);
    SSL_CTX_set_mode(c->ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE |
                                 SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    return c;
}

void st_free(st_ctx *c)
{
    size_t i;
    if (!c) return;
    for (i = 0; i < c->chain_count; i++)
        OPENSSL_free(c->chain_der[i]);
    if (c->ssl)     SSL_free(c->ssl);           /* frees the BIO too */
    else if (c->bio) BIO_free(c->bio);
    if (c->ssl_ctx) SSL_CTX_free(c->ssl_ctx);
    free(c->peer_name);
    free(c->default_ca_file);
    free(c);
}

int32_t st_set_io_funcs(st_ctx *c, st_read_fn rd, st_write_fn wr)
{
    if (!c || !rd || !wr)       return ST_ERR_PARAM;
    if (c->state != ST_STATE_IDLE) return ST_ERR_BADREQ;
    c->read_fn = rd; c->write_fn = wr;
    return ST_ERR_SUCCESS;
}

int32_t st_set_connection(st_ctx *c, void *conn)
{
    if (!c)                     return ST_ERR_PARAM;
    if (c->state != ST_STATE_IDLE) return ST_ERR_BADREQ;
    c->conn = conn;
    return ST_ERR_SUCCESS;
}

int32_t st_get_connection(st_ctx *c, void **conn)
{
    if (!c || !conn) return ST_ERR_PARAM;
    *conn = c->conn;
    return ST_ERR_SUCCESS;
}

int32_t st_set_peer_domain_name(st_ctx *c, const char *name, size_t len)
{
    char *copy;
    if (!c)                     return ST_ERR_PARAM;
    if (c->state != ST_STATE_IDLE) return ST_ERR_BADREQ;
    if (!name || !len) {
        free(c->peer_name);
        c->peer_name = NULL; c->peer_name_len = 0;
        return ST_ERR_SUCCESS;
    }
    /* Apple's SSLSetPeerDomainName takes a possibly non-NUL-terminated buffer
     * with an explicit length. Embedded NULs are a classic way to smuggle a
     * name past a check, so reject them rather than truncating. */
    if (memchr(name, '\0', len))
        return ST_ERR_PARAM;
    copy = malloc(len + 1);
    if (!copy) return ST_ERR_ALLOCATE;
    memcpy(copy, name, len);
    copy[len] = '\0';
    free(c->peer_name);
    c->peer_name = copy;
    c->peer_name_len = len;
    return ST_ERR_SUCCESS;
}

int32_t st_get_peer_domain_name_length(st_ctx *c, size_t *len)
{
    if (!c || !len) return ST_ERR_PARAM;
    /* Apple includes the NUL in the reported length. */
    *len = c->peer_name ? c->peer_name_len + 1 : 0;
    return ST_ERR_SUCCESS;
}

int32_t st_get_peer_domain_name(st_ctx *c, char *buf, size_t *len)
{
    size_t need;
    if (!c || !len) return ST_ERR_PARAM;
    need = c->peer_name ? c->peer_name_len + 1 : 0;
    if (!buf) { *len = need; return ST_ERR_SUCCESS; }
    if (*len < need) { *len = need; return ST_ERR_PARAM; }
    if (need) memcpy(buf, c->peer_name, need);
    *len = need;
    return ST_ERR_SUCCESS;
}

/* Apple's SSLProtocol values are not ordered, so this cannot be a comparison. */
static int st_proto_to_openssl(int p, int *out)
{
    switch (p) {
    case ST_PROTO_SSL3:       *out = SSL3_VERSION;   return 1;
    case ST_PROTO_TLS1:       *out = TLS1_VERSION;   return 1;
    case ST_PROTO_TLS11:      *out = TLS1_1_VERSION; return 1;
    case ST_PROTO_TLS12:      *out = TLS1_2_VERSION; return 1;
    case ST_PROTO_TLS13:      *out = TLS1_3_VERSION; return 1;
    case ST_PROTO_MAX_SUPPORTED: *out = TLS1_3_VERSION; return 1;
    case ST_PROTO_UNKNOWN:    *out = 0;              return 1;  /* "any" */
    default:                  return 0;
    }
}

static int st_openssl_to_proto(int v)
{
    switch (v) {
    case SSL3_VERSION:   return ST_PROTO_SSL3;
    case TLS1_VERSION:   return ST_PROTO_TLS1;
    case TLS1_1_VERSION: return ST_PROTO_TLS11;
    case TLS1_2_VERSION: return ST_PROTO_TLS12;
    case TLS1_3_VERSION: return ST_PROTO_TLS13;
    default:             return ST_PROTO_UNKNOWN;
    }
}

int32_t st_set_protocol_version_min(st_ctx *c, int proto)
{
    int v;
    if (!c) return ST_ERR_PARAM;
    if (c->state != ST_STATE_IDLE) return ST_ERR_BADREQ;
    if (!st_proto_to_openssl(proto, &v)) return ST_ERR_PARAM;
    c->proto_min = proto;
    return ST_ERR_SUCCESS;
}

int32_t st_set_protocol_version_max(st_ctx *c, int proto)
{
    int v;
    if (!c) return ST_ERR_PARAM;
    if (c->state != ST_STATE_IDLE) return ST_ERR_BADREQ;
    if (!st_proto_to_openssl(proto, &v)) return ST_ERR_PARAM;
    c->proto_max = proto;
    return ST_ERR_SUCCESS;
}

int32_t st_get_protocol_version_min(st_ctx *c, int *proto)
{
    if (!c || !proto) return ST_ERR_PARAM;
    *proto = c->proto_min; return ST_ERR_SUCCESS;
}

int32_t st_get_protocol_version_max(st_ctx *c, int *proto)
{
    if (!c || !proto) return ST_ERR_PARAM;
    *proto = c->proto_max; return ST_ERR_SUCCESS;
}

int32_t st_set_session_option(st_ctx *c, int opt, int on)
{
    if (!c) return ST_ERR_PARAM;
    switch (opt) {
    case ST_OPT_BREAK_ON_SERVER_AUTH:         c->opt_break_server_auth = !!on; break;
    case ST_OPT_BREAK_ON_CERT_REQUESTED:      c->opt_break_cert_requested = !!on; break;
    case ST_OPT_BREAK_ON_CLIENT_AUTH:         c->opt_break_client_auth = !!on; break;
    case ST_OPT_FALSE_START:                  c->opt_false_start = !!on; break;
    case ST_OPT_SEND_ONE_BYTE_RECORD:         c->opt_one_byte_record = !!on; break;
    case ST_OPT_ALLOW_SERVER_IDENTITY_CHANGE: c->opt_allow_identity_change = !!on; break;
    case ST_OPT_FALLBACK:                     c->opt_fallback = !!on; break;
    case ST_OPT_BREAK_ON_CLIENT_HELLO:        c->opt_break_client_hello = !!on; break;
    default: return ST_ERR_PARAM;
    }
    return ST_ERR_SUCCESS;
}

int32_t st_get_session_option(st_ctx *c, int opt, int *on)
{
    if (!c || !on) return ST_ERR_PARAM;
    switch (opt) {
    case ST_OPT_BREAK_ON_SERVER_AUTH:         *on = c->opt_break_server_auth; break;
    case ST_OPT_BREAK_ON_CERT_REQUESTED:      *on = c->opt_break_cert_requested; break;
    case ST_OPT_BREAK_ON_CLIENT_AUTH:         *on = c->opt_break_client_auth; break;
    case ST_OPT_FALSE_START:                  *on = c->opt_false_start; break;
    case ST_OPT_SEND_ONE_BYTE_RECORD:         *on = c->opt_one_byte_record; break;
    case ST_OPT_ALLOW_SERVER_IDENTITY_CHANGE: *on = c->opt_allow_identity_change; break;
    case ST_OPT_FALLBACK:                     *on = c->opt_fallback; break;
    case ST_OPT_BREAK_ON_CLIENT_HELLO:        *on = c->opt_break_client_hello; break;
    default: return ST_ERR_PARAM;
    }
    return ST_ERR_SUCCESS;
}

int32_t st_set_enable_cert_verify(st_ctx *c, int on)
{
    if (!c) return ST_ERR_PARAM;
    if (c->state != ST_STATE_IDLE) return ST_ERR_BADREQ;
    c->verify_enabled = !!on;
    return ST_ERR_SUCCESS;
}

int32_t st_get_enable_cert_verify(st_ctx *c, int *on)
{
    if (!c || !on) return ST_ERR_PARAM;
    *on = c->verify_enabled; return ST_ERR_SUCCESS;
}

int32_t st_set_trusted_roots_pem(st_ctx *c, const char *pem, size_t len,
                                 int replace)
{
    BIO       *b;
    X509      *x;
    X509_STORE *store;
    int        added = 0;

    if (!c || !pem || !len) return ST_ERR_PARAM;
    if (c->state != ST_STATE_IDLE) return ST_ERR_BADREQ;
    if (len > (size_t)INT_MAX)  return ST_ERR_PARAM;

    if (replace) {
        /* A fresh store, so the system roots really are gone rather than
         * merely shadowed. */
        store = X509_STORE_new();
        if (!store) return ST_ERR_ALLOCATE;
        SSL_CTX_set_cert_store(c->ssl_ctx, store);   /* takes ownership */
    } else {
        store = SSL_CTX_get_cert_store(c->ssl_ctx);
        if (!store) return ST_ERR_INTERNAL;
    }

    b = BIO_new_mem_buf(pem, (int)len);
    if (!b) return ST_ERR_ALLOCATE;
    while ((x = PEM_read_bio_X509(b, NULL, NULL, NULL)) != NULL) {
        if (X509_STORE_add_cert(store, x)) added++;
        X509_free(x);
    }
    ERR_clear_error();              /* the loop always ends on a parse error */
    BIO_free(b);

    if (!added) { st_seterr(c, "no certificates in PEM"); return ST_ERR_PARAM; }
    c->roots_configured = 1;
    return ST_ERR_SUCCESS;
}

int32_t st_set_default_ca_file(st_ctx *c, const char *path)
{
    char *copy;
    if (!c || !path) return ST_ERR_PARAM;
    copy = strdup(path);
    if (!copy) return ST_ERR_ALLOCATE;
    free(c->default_ca_file);
    c->default_ca_file = copy;
    return ST_ERR_SUCCESS;
}

int32_t st_set_client_certificate_pem(st_ctx *c, const char *cert_pem,
                                      size_t cert_len, const char *key_pem,
                                      size_t key_len)
{
    BIO      *b;
    X509     *x;
    EVP_PKEY *k;
    int       ok;

    if (!c || !cert_pem || !cert_len || !key_pem || !key_len)
        return ST_ERR_PARAM;
    if (c->state != ST_STATE_IDLE) return ST_ERR_BADREQ;

    b = BIO_new_mem_buf(cert_pem, (int)cert_len);
    if (!b) return ST_ERR_ALLOCATE;
    x = PEM_read_bio_X509(b, NULL, NULL, NULL);
    BIO_free(b);
    if (!x) { st_seterr(c, "client certificate"); return ST_ERR_PARAM; }

    b = BIO_new_mem_buf(key_pem, (int)key_len);
    if (!b) { X509_free(x); return ST_ERR_ALLOCATE; }
    k = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);
    BIO_free(b);
    if (!k) { X509_free(x); st_seterr(c, "client key"); return ST_ERR_PARAM; }

    ok = SSL_CTX_use_certificate(c->ssl_ctx, x) == 1 &&
         SSL_CTX_use_PrivateKey(c->ssl_ctx, k) == 1 &&
         SSL_CTX_check_private_key(c->ssl_ctx) == 1;
    X509_free(x);
    EVP_PKEY_free(k);
    if (!ok) { st_seterr(c, "client identity"); return ST_ERR_PARAM; }
    return ST_ERR_SUCCESS;
}

/* ----------------------------------------------------------- handshake -- */

static void st_cache_peer_chain(st_ctx *c)
{
    STACK_OF(X509) *chain;
    X509 *leaf;
    int i, n;

    if (c->chain_count) return;

    leaf = SSL_get1_peer_certificate(c->ssl);
    if (leaf) {
        unsigned char *der = NULL;
        int len = i2d_X509(leaf, &der);
        if (len > 0 && c->chain_count < ST_MAX_CHAIN) {
            c->chain_der[c->chain_count] = der;
            c->chain_len[c->chain_count] = (size_t)len;
            c->chain_count++;
        } else if (der) {
            OPENSSL_free(der);
        }
    }

    chain = SSL_get_peer_cert_chain(c->ssl);
    n = chain ? sk_X509_num(chain) : 0;
    for (i = 0; i < n && c->chain_count < ST_MAX_CHAIN; i++) {
        X509 *x = sk_X509_value(chain, i);
        unsigned char *der = NULL;
        int len;
        /* As a client, OpenSSL puts the leaf at index 0 of the chain too. */
        if (leaf && X509_cmp(x, leaf) == 0) continue;
        len = i2d_X509(x, &der);
        if (len > 0) {
            c->chain_der[c->chain_count] = der;
            c->chain_len[c->chain_count] = (size_t)len;
            c->chain_count++;
        } else if (der) {
            OPENSSL_free(der);
        }
    }
    if (leaf) X509_free(leaf);
}

static int32_t st_start(st_ctx *c)
{
    int vmin = 0, vmax = 0;

    if (!c->read_fn || !c->write_fn)  return ST_ERR_BADREQ;
    if (c->conn_type == ST_TYPE_DATAGRAM) {
        /* Better an honest refusal than DTLS silently handled as TLS. */
        st_seterr(c, "DTLS is not implemented");
        return ST_ERR_UNIMPLEMENTED;
    }

    c->ssl = SSL_new(c->ssl_ctx);
    if (!c->ssl) { st_seterr(c, "SSL_new"); return ST_ERR_ALLOCATE; }
    SSL_set_ex_data(c->ssl, st_ssl_ctx_index(), c);

    st_proto_to_openssl(c->proto_min, &vmin);
    st_proto_to_openssl(c->proto_max, &vmax);
    if (vmin) SSL_set_min_proto_version(c->ssl, vmin);
    if (vmax) SSL_set_max_proto_version(c->ssl, vmax);

    if (c->side == ST_SIDE_CLIENT) {
        if (c->verify_enabled && !c->roots_configured) {
            const char *ca = c->default_ca_file ? c->default_ca_file
                                                : "/etc/ssl/cert.pem";
            if (!SSL_CTX_load_verify_locations(c->ssl_ctx, ca, NULL)) {
                /* No trust anchors and verification on: refuse rather than
                 * connect to something we cannot check. */
                ERR_clear_error();
                st_seterr(c, "no trust anchors available");
                c->state = ST_STATE_ABORTED;
                return ST_ERR_NO_ROOT_CERT;
            }
        }
        if (c->peer_name) {
            X509_VERIFY_PARAM *p = SSL_get0_param(c->ssl);
            /* Without this OpenSSL checks the chain but not the name, which is
             * exactly the "valid certificate, wrong site" hole. */
            X509_VERIFY_PARAM_set_hostflags(p,
                X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            if (!X509_VERIFY_PARAM_set1_host(p, c->peer_name,
                                             c->peer_name_len)) {
                st_seterr(c, "set1_host");
                return ST_ERR_INTERNAL;
            }
            SSL_set_tlsext_host_name(c->ssl, c->peer_name);   /* SNI */
        }
        SSL_set_connect_state(c->ssl);
    } else {
        SSL_set_accept_state(c->ssl);
    }

    c->bio = BIO_new(st_bio_method());
    if (!c->bio) { st_seterr(c, "BIO_new"); return ST_ERR_ALLOCATE; }
    BIO_set_data(c->bio, c);
    BIO_set_init(c->bio, 1);
    SSL_set_bio(c->ssl, c->bio, c->bio);        /* SSL owns it now */

    c->state = ST_STATE_HANDSHAKE;
    return ST_ERR_SUCCESS;
}

/* Translate an SSL_get_error() result into an errSSL* code. */
static int32_t st_map_ssl_err(st_ctx *c, int ret)
{
    int e = SSL_get_error(c->ssl, ret);

    switch (e) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
    case SSL_ERROR_WANT_CONNECT:
    case SSL_ERROR_WANT_ACCEPT:
        return ST_ERR_WOULDBLOCK;

    case SSL_ERROR_WANT_RETRY_VERIFY:
        /* The verify callback suspended us so the app can judge the chain. */
        return ST_ERR_PEER_AUTH_COMPLETED;

    case SSL_ERROR_ZERO_RETURN:
        c->state = ST_STATE_CLOSED;
        return ST_ERR_CLOSED_GRACEFUL;

    case SSL_ERROR_SYSCALL:
        c->state = ST_STATE_ABORTED;
        /* Our BIO stashes the app callback's own status here; prefer it, since
         * it is the real cause and the app knows what it means. */
        if (c->io_err) return c->io_err;
        st_seterr(c, "transport");
        return ST_ERR_CLOSED_ABORT;

    case SSL_ERROR_SSL:
    default:
        c->state = ST_STATE_ABORTED;
        /*
         * A truncated connection - the peer vanished without close_notify -
         * arrives here in OpenSSL 3.x as SSL_R_UNEXPECTED_EOF_WHILE_READING.
         * It must be errSSLClosedAbort and never a clean close: reporting it as
         * graceful is precisely the truncation attack, where an attacker cuts
         * the connection early and the app treats a partial response as whole.
         */
        if (ERR_GET_REASON(ERR_peek_last_error()) ==
            SSL_R_UNEXPECTED_EOF_WHILE_READING) {
            st_seterr(c, "connection truncated without close_notify");
            return ST_ERR_CLOSED_ABORT;
        }
        /* A verification failure surfaces as a generic SSL error, so check for
         * a recorded X509 problem first: that is what carries the useful code. */
        if (c->x509_err != X509_V_OK) {
            c->trust_result = st_map_x509_err(c->x509_err);
            snprintf(c->errbuf, sizeof c->errbuf, "certificate: %s",
                     X509_verify_cert_error_string(c->x509_err));
            return c->trust_result;
        }
        st_seterr(c, "handshake");
        return ST_ERR_HANDSHAKE_FAIL;
    }
}

int32_t st_handshake(st_ctx *c)
{
    int ret;

    if (!c) return ST_ERR_PARAM;
    if (c->state == ST_STATE_CONNECTED) {
        /* Second call after we reported errSSLPeerAuthCompleted: the app has
         * had its look at the certificate and wants to continue. */
        return ST_ERR_SUCCESS;
    }
    if (c->state == ST_STATE_CLOSED || c->state == ST_STATE_ABORTED)
        return ST_ERR_BADREQ;

    if (c->state == ST_STATE_IDLE) {
        int32_t s = st_start(c);
        if (s != ST_ERR_SUCCESS) return s;
    }

    c->io_err = 0;
    ERR_clear_error();
    ret = SSL_do_handshake(c->ssl);
    if (ret != 1) {
        int32_t s = st_map_ssl_err(c, ret);
        if (s == ST_ERR_PEER_AUTH_COMPLETED) {
            /* Suspended at the leaf. Everything the app needs to make its own
             * decision - the chain and our verdict on it - has to be ready
             * before we hand control back. */
            st_cache_peer_chain(c);
            c->trust_result = st_map_x509_err(c->x509_err);
            c->reported_auth_completed = 1;
            c->state = ST_STATE_HANDSHAKE;   /* genuinely not finished yet */
        }
        return s;
    }

    st_cache_peer_chain(c);

    /*
     * The handshake is done; decide what the certificate was worth.
     *
     * In BreakOnServerAuth mode our verify callback returned 1 to let the
     * handshake proceed, so SSL_get_verify_result() now says X509_V_OK even
     * when the chain was bad. Trusting it here would erase the real verdict and
     * report a self-signed certificate as clean - so in that mode the answer
     * recorded at the break is the one that stands.
     */
    if (!c->opt_break_server_auth) {
        long v = SSL_get_verify_result(c->ssl);
        if (v != X509_V_OK) {
            if (c->x509_err == X509_V_OK) c->x509_err = (int)v;
            c->trust_result = st_map_x509_err((int)v);
        } else {
            c->trust_result = ST_ERR_SUCCESS;
        }
    } else {
        c->trust_result = st_map_x509_err(c->x509_err);
    }

    if (c->verify_enabled && c->opt_break_server_auth &&
        !c->reported_auth_completed) {
        /*
         * Fallback path only: SSL_set_retry_verify() could not suspend the
         * handshake, so we break here instead, after it completed but before
         * reporting kSSLConnected and before any application data can move.
         *
         * The difference from Apple, and from the suspend path above, is that
         * our client Finished - and a client certificate, had one been
         * configured - has already reached a peer the app has not yet approved.
         * No plaintext does. auth_break_is_post_handshake records that this is
         * what happened, so a test can tell the two paths apart.
         */
        c->reported_auth_completed = 1;
        c->state = ST_STATE_CONNECTED;
        return ST_ERR_PEER_AUTH_COMPLETED;
    }

    /* In BreakOnServerAuth mode the app has already been shown the verdict and
     * called back to continue; that call is its approval, so a bad chain is no
     * longer ours to veto. In every other mode it is. */
    if (c->verify_enabled && !c->opt_break_server_auth &&
        c->trust_result != ST_ERR_SUCCESS) {
        c->state = ST_STATE_ABORTED;
        return c->trust_result;
    }

    c->state = ST_STATE_CONNECTED;
    return ST_ERR_SUCCESS;
}

/* ------------------------------------------------------------------ io -- */

int32_t st_read(st_ctx *c, void *data, size_t want, size_t *got)
{
    size_t total = 0;

    if (!c || (!data && want) || !got) return ST_ERR_PARAM;
    *got = 0;
    if (c->state != ST_STATE_CONNECTED) return ST_ERR_BADREQ;
    if (!want) return ST_ERR_SUCCESS;

    while (total < want) {
        int n, chunk = (int)((want - total) > INT_MAX ? INT_MAX : want - total);
        c->io_err = 0;
        ERR_clear_error();
        n = SSL_read(c->ssl, (unsigned char *)data + total, chunk);
        if (n > 0) { total += (size_t)n; continue; }

        *got = total;
        {
            int32_t s = st_map_ssl_err(c, n);
            /* Apple reports a short read as would-block with the bytes it did
             * get, and a clean close only once the buffer has been drained. */
            if (s == ST_ERR_CLOSED_GRACEFUL && total) return ST_ERR_SUCCESS;
            return s;
        }
    }
    *got = total;
    return ST_ERR_SUCCESS;
}

int32_t st_write(st_ctx *c, const void *data, size_t want, size_t *put)
{
    size_t total = 0;

    if (!c || (!data && want) || !put) return ST_ERR_PARAM;
    *put = 0;
    if (c->state != ST_STATE_CONNECTED) return ST_ERR_BADREQ;
    if (!want) return ST_ERR_SUCCESS;

    while (total < want) {
        int n, chunk = (int)((want - total) > INT_MAX ? INT_MAX : want - total);
        c->io_err = 0;
        ERR_clear_error();
        n = SSL_write(c->ssl, (const unsigned char *)data + total, chunk);
        if (n > 0) { total += (size_t)n; continue; }
        *put = total;
        return st_map_ssl_err(c, n);
    }
    *put = total;
    return ST_ERR_SUCCESS;
}

int32_t st_close(st_ctx *c)
{
    int ret;

    if (!c) return ST_ERR_PARAM;
    if (!c->ssl || c->state != ST_STATE_CONNECTED) {
        c->state = ST_STATE_CLOSED;
        return ST_ERR_SUCCESS;
    }

    c->io_err = 0;
    ERR_clear_error();
    /* One shutdown: send close_notify, do not block waiting for the peer's.
     * Waiting is what hangs a client whose server never replies. */
    ret = SSL_shutdown(c->ssl);
    if (ret < 0) {
        int e = SSL_get_error(c->ssl, ret);
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            /* close_notify has not gone out yet. Say so, so a non-blocking
             * caller retries rather than dropping the connection unclosed. */
            ERR_clear_error();
            return ST_ERR_WOULDBLOCK;
        }
    }
    ERR_clear_error();
    c->state = ST_STATE_CLOSED;
    return ST_ERR_SUCCESS;
}

/* --------------------------------------------------------------- query -- */

int32_t st_get_session_state(st_ctx *c, int *state)
{
    if (!c || !state) return ST_ERR_PARAM;
    *state = c->state; return ST_ERR_SUCCESS;
}

int32_t st_get_negotiated_protocol_version(st_ctx *c, int *proto)
{
    if (!c || !proto) return ST_ERR_PARAM;
    *proto = c->ssl ? st_openssl_to_proto(SSL_version(c->ssl))
                    : ST_PROTO_UNKNOWN;
    return ST_ERR_SUCCESS;
}

int32_t st_get_negotiated_cipher(st_ctx *c, uint32_t *cipher)
{
    const SSL_CIPHER *cs;
    if (!c || !cipher) return ST_ERR_PARAM;
    *cipher = 0;
    if (!c->ssl) return ST_ERR_BADREQ;
    cs = SSL_get_current_cipher(c->ssl);
    if (!cs) return ST_ERR_BADREQ;
    /* SSL_CIPHER_get_id() carries OpenSSL's 0x03000000 prefix; the wire value
     * is the low 16 bits, and that is what SSLCipherSuite holds. */
    *cipher = (uint32_t)(SSL_CIPHER_get_id(cs) & 0xFFFF);
    return ST_ERR_SUCCESS;
}

int32_t st_get_buffered_read_size(st_ctx *c, size_t *n)
{
    int p;
    if (!c || !n) return ST_ERR_PARAM;
    *n = 0;
    if (!c->ssl) return ST_ERR_BADREQ;
    p = SSL_pending(c->ssl);
    *n = p > 0 ? (size_t)p : 0;
    return ST_ERR_SUCCESS;
}

int32_t st_get_client_certificate_state(st_ctx *c, int *state)
{
    if (!c || !state) return ST_ERR_PARAM;
    /* Client-certificate exchange is not wired up yet; report "not asked for"
     * rather than inventing a state. */
    *state = ST_CLIENTCERT_NONE;
    return ST_ERR_SUCCESS;
}

size_t st_peer_cert_count(st_ctx *c)
{
    return c ? c->chain_count : 0;
}

int32_t st_peer_cert_der(st_ctx *c, size_t idx, const uint8_t **der, size_t *len)
{
    if (!c || !der || !len) return ST_ERR_PARAM;
    if (idx >= c->chain_count) return ST_ERR_PARAM;
    *der = c->chain_der[idx];
    *len = c->chain_len[idx];
    return ST_ERR_SUCCESS;
}

int32_t st_peer_trust_result(st_ctx *c)
{
    return c ? c->trust_result : ST_ERR_PARAM;
}

const char *st_last_error_string(st_ctx *c)
{
    return (c && c->errbuf[0]) ? c->errbuf : "";
}

/* ------------------------------------------------------ trust, standalone -- */

static X509 *st_d2i(const uint8_t *der, size_t len)
{
    const unsigned char *p = der;
    if (!der || !len || len > (size_t)LONG_MAX) return NULL;
    return d2i_X509(NULL, &p, (long)len);
}

int32_t st_verify_chain(const uint8_t *const *certs,   const size_t *cert_lens,
                        size_t ncerts,
                        const uint8_t *const *anchors, const size_t *anchor_lens,
                        size_t nanchors, int anchors_only,
                        const char *hostname, const char *default_ca_file)
{
    X509_STORE     *store = NULL;
    X509_STORE_CTX *ctx   = NULL;
    STACK_OF(X509) *untrusted = NULL;
    X509           *leaf  = NULL;
    int32_t         out   = ST_ERR_XCERT_CHAIN_INVALID;
    size_t          i;
    int             rc;

    if (!certs || !cert_lens || !ncerts) return ST_ERR_PARAM;

    leaf = st_d2i(certs[0], cert_lens[0]);
    if (!leaf) { ERR_clear_error(); return ST_ERR_BAD_CERT; }

    store = X509_STORE_new();
    if (!store) { X509_free(leaf); return ST_ERR_ALLOCATE; }

    /* anchors_only means exactly the caller's anchors. Quietly falling back to
     * the system store when the caller asked for pinning would defeat the
     * entire point of pinning. */
    if (!anchors_only) {
        const char *ca = default_ca_file ? default_ca_file : "/etc/ssl/cert.pem";
        if (!X509_STORE_load_locations(store, ca, NULL))
            ERR_clear_error();      /* anchors alone may still be enough */
    }
    for (i = 0; i < nanchors; i++) {
        X509 *a = st_d2i(anchors[i], anchor_lens[i]);
        if (!a) { ERR_clear_error(); continue; }
        X509_STORE_add_cert(store, a);
        X509_free(a);
    }

    untrusted = sk_X509_new_null();
    if (!untrusted) { out = ST_ERR_ALLOCATE; goto done; }
    for (i = 1; i < ncerts; i++) {
        X509 *x = st_d2i(certs[i], cert_lens[i]);
        if (!x) { ERR_clear_error(); continue; }
        if (!sk_X509_push(untrusted, x)) X509_free(x);
    }

    ctx = X509_STORE_CTX_new();
    if (!ctx) { out = ST_ERR_ALLOCATE; goto done; }
    if (!X509_STORE_CTX_init(ctx, store, leaf, untrusted)) {
        out = ST_ERR_INTERNAL; goto done;
    }

    if (hostname && *hostname) {
        X509_VERIFY_PARAM *p = X509_STORE_CTX_get0_param(ctx);
        X509_VERIFY_PARAM_set_hostflags(p, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (!X509_VERIFY_PARAM_set1_host(p, hostname, strlen(hostname))) {
            out = ST_ERR_INTERNAL; goto done;
        }
    }

    rc = X509_verify_cert(ctx);
    if (rc == 1) {
        out = ST_ERR_SUCCESS;
    } else {
        out = st_map_x509_err(X509_STORE_CTX_get_error(ctx));
        /* X509_verify_cert can fail without setting a specific error. Never let
         * that collapse into success. */
        if (out == ST_ERR_SUCCESS) out = ST_ERR_XCERT_CHAIN_INVALID;
    }

done:
    if (ctx)   X509_STORE_CTX_free(ctx);
    if (untrusted) sk_X509_pop_free(untrusted, X509_free);
    if (store) X509_STORE_free(store);
    if (leaf)  X509_free(leaf);
    ERR_clear_error();
    return out;
}
