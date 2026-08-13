/*
 * st_core.h - Secure Transport's engine, as plain C over OpenSSL.
 *
 * Deliberately knows nothing about CoreFoundation, Mach-O, or Darwin. That is
 * the whole point: this file and st_core.c compile and run natively on the
 * Linux build host, so the TLS logic can be tested against a real peer with a
 * real certificate long before anything boots in a VM. The CoreFoundation and
 * OSStatus veneer that apps actually call lives in SecureTransport.c.
 *
 * A TLS layer that quietly accepts an expired certificate, an unknown issuer,
 * or a mismatched hostname is worse than no TLS at all, because callers believe
 * they are protected. So the negative paths here are tested first-class - see
 * securetransport/test/.
 *
 * Constants are namespaced ST_* rather than reusing Apple's spellings, so that
 * SecureTransport.c can include both this header and the SDK's
 * <Security/SecureTransport.h> and static-assert that every value agrees. The
 * values below were transcribed from the MacOSX11.3 SDK headers, not recalled:
 *   SecureTransport.h, SecBase.h and SecProtocolTypes.h.
 */
#ifndef OPENOSX_ST_CORE_H
#define OPENOSX_ST_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- errors --
 * From the SDK. An app compiled against macOS has these baked into its own
 * code, so returning a different number is not a cosmetic difference: it is a
 * behavioural bug that shows up as a hang or a bogus error message.
 */
#define ST_ERR_SUCCESS                 0        /* errSecSuccess */
#define ST_ERR_PARAM                  -50       /* errSecParam */
#define ST_ERR_ALLOCATE              -108       /* errSecAllocate */
#define ST_ERR_UNIMPLEMENTED           -4       /* errSecUnimplemented */
#define ST_ERR_IO                     -36       /* errSecIO */
#define ST_ERR_BADREQ                -909       /* errSecBadReq */

#define ST_ERR_PROTOCOL             -9800       /* errSSLProtocol */
#define ST_ERR_NEGOTIATION          -9801       /* errSSLNegotiation */
#define ST_ERR_FATAL_ALERT          -9802       /* errSSLFatalAlert */
#define ST_ERR_WOULDBLOCK           -9803       /* errSSLWouldBlock */
#define ST_ERR_SESSION_NOT_FOUND    -9804       /* errSSLSessionNotFound */
#define ST_ERR_CLOSED_GRACEFUL      -9805       /* errSSLClosedGraceful */
#define ST_ERR_CLOSED_ABORT         -9806       /* errSSLClosedAbort */
#define ST_ERR_XCERT_CHAIN_INVALID  -9807       /* errSSLXCertChainInvalid */
#define ST_ERR_BAD_CERT             -9808       /* errSSLBadCert */
#define ST_ERR_CRYPTO               -9809       /* errSSLCrypto */
#define ST_ERR_INTERNAL             -9810       /* errSSLInternal */
#define ST_ERR_UNKNOWN_ROOT_CERT    -9812       /* errSSLUnknownRootCert */
#define ST_ERR_NO_ROOT_CERT         -9813       /* errSSLNoRootCert */
#define ST_ERR_CERT_EXPIRED         -9814       /* errSSLCertExpired */
#define ST_ERR_CERT_NOT_YET_VALID   -9815       /* errSSLCertNotYetValid */
#define ST_ERR_CLOSED_NO_NOTIFY     -9816       /* errSSLClosedNoNotify */
#define ST_ERR_BUFFER_OVERFLOW      -9817       /* errSSLBufferOverflow */
#define ST_ERR_BAD_CIPHER_SUITE     -9818       /* errSSLBadCipherSuite */
#define ST_ERR_PEER_UNKNOWN_CA      -9831       /* errSSLPeerUnknownCA */
#define ST_ERR_PEER_AUTH_COMPLETED  -9841       /* errSSLPeerAuthCompleted */
#define ST_ERR_CLIENT_CERT_REQUESTED -9842      /* errSSLClientCertRequested */
#define ST_ERR_HOST_NAME_MISMATCH   -9843       /* errSSLHostNameMismatch */
#define ST_ERR_CONNECTION_REFUSED   -9844       /* errSSLConnectionRefused */
#define ST_ERR_BAD_CONFIGURATION    -9848       /* errSSLBadConfiguration */
#define ST_ERR_HANDSHAKE_FAIL       -9858       /* errSSLHandshakeFail */

/* ------------------------------------------------------------- protocols --
 * SecProtocolTypes.h. Note the ordering is historical, not numeric: TLS 1.0 is
 * 4 and SSL3 is 2, with the "Only"/"All" pseudo-values wedged between them.
 */
#define ST_PROTO_UNKNOWN       0                /* kSSLProtocolUnknown */
#define ST_PROTO_SSL2          1                /* kSSLProtocol2 */
#define ST_PROTO_SSL3          2                /* kSSLProtocol3 */
#define ST_PROTO_SSL3_ONLY     3                /* kSSLProtocol3Only */
#define ST_PROTO_TLS1          4                /* kTLSProtocol1 */
#define ST_PROTO_TLS1_ONLY     5                /* kTLSProtocol1Only */
#define ST_PROTO_ALL           6                /* kSSLProtocolAll */
#define ST_PROTO_TLS11         7                /* kTLSProtocol11 */
#define ST_PROTO_TLS12         8                /* kTLSProtocol12 */
#define ST_PROTO_DTLS1         9                /* kDTLSProtocol1 */
#define ST_PROTO_TLS13        10                /* kTLSProtocol13 */
#define ST_PROTO_DTLS12       11                /* kDTLSProtocol12 */
#define ST_PROTO_MAX_SUPPORTED 999              /* kTLSProtocolMaxSupported */

/* ----------------------------------------------------------------- state --
 * SSLSessionState. Plain 0..4, no explicit initialisers in the SDK.
 */
#define ST_STATE_IDLE          0                /* kSSLIdle */
#define ST_STATE_HANDSHAKE     1                /* kSSLHandshake */
#define ST_STATE_CONNECTED     2                /* kSSLConnected */
#define ST_STATE_CLOSED        3                /* kSSLClosed */
#define ST_STATE_ABORTED       4                /* kSSLAborted */

/* --------------------------------------------------------------- options --
 * SSLSessionOption, explicitly numbered 0..7 in the SDK.
 */
#define ST_OPT_BREAK_ON_SERVER_AUTH           0
#define ST_OPT_BREAK_ON_CERT_REQUESTED        1
#define ST_OPT_BREAK_ON_CLIENT_AUTH           2
#define ST_OPT_FALSE_START                    3
#define ST_OPT_SEND_ONE_BYTE_RECORD           4
#define ST_OPT_ALLOW_SERVER_IDENTITY_CHANGE   5
#define ST_OPT_FALLBACK                       6
#define ST_OPT_BREAK_ON_CLIENT_HELLO          7

/* SSLProtocolSide / SSLConnectionType, both plain 0/1. */
#define ST_SIDE_SERVER         0                /* kSSLServerSide */
#define ST_SIDE_CLIENT         1                /* kSSLClientSide */
#define ST_TYPE_STREAM         0                /* kSSLStreamType */
#define ST_TYPE_DATAGRAM       1                /* kSSLDatagramType */

/* SSLClientCertificateState. */
#define ST_CLIENTCERT_NONE      0
#define ST_CLIENTCERT_REQUESTED 1
#define ST_CLIENTCERT_SENT      2
#define ST_CLIENTCERT_REJECTED  3

/* ---------------------------------------------------------------- the API --
 *
 * The I/O callbacks match SSLReadFunc/SSLWriteFunc byte for byte, including the
 * detail that trips up naive ports: dataLength is IN/OUT. On entry it is how
 * much the caller wants; on return it is how much actually moved. A short
 * transfer returns ST_ERR_WOULDBLOCK *with a non-zero length*, and that is not
 * an error - it is the normal non-blocking case.
 */
typedef int32_t (*st_read_fn)(void *conn, void *data, size_t *data_len);
typedef int32_t (*st_write_fn)(void *conn, const void *data, size_t *data_len);

typedef struct st_ctx st_ctx;

st_ctx *st_new(int side, int conn_type);
void    st_free(st_ctx *c);

int32_t st_set_io_funcs(st_ctx *c, st_read_fn rd, st_write_fn wr);
int32_t st_set_connection(st_ctx *c, void *conn);
int32_t st_get_connection(st_ctx *c, void **conn);

/* Hostname to check the peer certificate against. Without this there is no
 * hostname check at all, which is how "valid certificate for the wrong site"
 * slips through. */
int32_t st_set_peer_domain_name(st_ctx *c, const char *name, size_t len);
int32_t st_get_peer_domain_name_length(st_ctx *c, size_t *len);
int32_t st_get_peer_domain_name(st_ctx *c, char *buf, size_t *len);

int32_t st_set_protocol_version_min(st_ctx *c, int proto);
int32_t st_set_protocol_version_max(st_ctx *c, int proto);
int32_t st_get_protocol_version_min(st_ctx *c, int *proto);
int32_t st_get_protocol_version_max(st_ctx *c, int *proto);

int32_t st_set_session_option(st_ctx *c, int opt, int on);
int32_t st_get_session_option(st_ctx *c, int opt, int *on);

/* Turning verification off is a caller's explicit choice (curl -k). It is kept
 * separate from the trust-root plumbing so that "no roots configured" can stay
 * a hard failure rather than silently degrading into "trust everything". */
int32_t st_set_enable_cert_verify(st_ctx *c, int on);
int32_t st_get_enable_cert_verify(st_ctx *c, int *on);

/* PEM, one or more certificates. replace=1 discards the default store. */
int32_t st_set_trusted_roots_pem(st_ctx *c, const char *pem, size_t len, int replace);
/* Fall back to this file when no roots are set explicitly. */
int32_t st_set_default_ca_file(st_ctx *c, const char *path);

int32_t st_set_client_certificate_pem(st_ctx *c, const char *cert_pem,
                                      size_t cert_len, const char *key_pem,
                                      size_t key_len);

int32_t st_handshake(st_ctx *c);
int32_t st_read(st_ctx *c, void *data, size_t want, size_t *got);
int32_t st_write(st_ctx *c, const void *data, size_t want, size_t *put);
int32_t st_close(st_ctx *c);

int32_t st_get_session_state(st_ctx *c, int *state);
int32_t st_get_negotiated_protocol_version(st_ctx *c, int *proto);
int32_t st_get_negotiated_cipher(st_ctx *c, uint32_t *cipher);
int32_t st_get_buffered_read_size(st_ctx *c, size_t *n);
int32_t st_get_client_certificate_state(st_ctx *c, int *state);

/* Peer chain as DER, leaf first, valid until the context is freed or
 * re-handshaked. The CF veneer wraps these as SecCertificateRefs. */
size_t  st_peer_cert_count(st_ctx *c);
int32_t st_peer_cert_der(st_ctx *c, size_t idx, const uint8_t **der, size_t *len);

/* Result of our own chain verification, as an ST_ERR_* code. Meaningful once
 * the handshake has got far enough to have seen the peer's certificate. */
int32_t st_peer_trust_result(st_ctx *c);

/* Human-readable detail for the last failure. Diagnostics only - never parse
 * it, and never surface it as the reason a connection was allowed. */
const char *st_last_error_string(st_ctx *c);

#ifdef __cplusplus
}
#endif
#endif /* OPENOSX_ST_CORE_H */
