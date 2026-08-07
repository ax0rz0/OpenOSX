/*
 * Real CommonCrypto digest API, bridged onto corecrypto (which OpenOSX
 * already has real and cross-built - see src/Libraries/libSystem/corecrypto).
 * Apple never open-sourced CommonCrypto's own digest-bridging code (only
 * its checksum/CRC helpers live in libcn/), so this fills that gap for the
 * subset callers actually need (currently: ld64's OutputFile.cpp UUID
 * computation via CC_MD5).
 *
 * CC_MD5_CTX's public struct layout (CommonDigest.h: A,B,C,D,Nl,Nh,
 * data[16] - 88 bytes) is a legacy, fixed-size ABI from the original
 * (non-corecrypto) MD5 implementation. corecrypto's real ccdigest context
 * for MD5 needs ccdigest_di_size(state=16,block=64) = 92 bytes, 4 more than
 * CC_MD5_CTX provides - too tight to embed in place. Instead we heap-
 * allocate the real context and stash the pointer in the first 8 bytes of
 * CC_MD5_CTX; nothing else inspects CC_MD5_CTX's individual fields, only
 * passes the struct by pointer between Init/Update/Final.
 */
#include <corecrypto/ccmd5.h>
#include <corecrypto/ccdigest.h>
#include <CommonCrypto/CommonDigest.h>
#include <stdlib.h>

struct pd_cc_md5_shim {
	void *real_ctx;
};

int
CC_MD5_Init(CC_MD5_CTX *c)
{
	struct pd_cc_md5_shim *shim = (struct pd_cc_md5_shim *)(void *)c;
	const struct ccdigest_info *di = ccmd5_di();

	shim->real_ctx = malloc(ccdigest_di_size(di));
	if (shim->real_ctx == NULL)
		return 0;
	ccdigest_init(di, (struct ccdigest_ctx *)shim->real_ctx);
	return 1;
}

int
CC_MD5_Update(CC_MD5_CTX *c, const void *data, CC_LONG len)
{
	struct pd_cc_md5_shim *shim = (struct pd_cc_md5_shim *)(void *)c;

	ccdigest_update(ccmd5_di(), (struct ccdigest_ctx *)shim->real_ctx, len, data);
	return 1;
}

int
CC_MD5_Final(unsigned char *md, CC_MD5_CTX *c)
{
	struct pd_cc_md5_shim *shim = (struct pd_cc_md5_shim *)(void *)c;

	ccdigest_final(ccmd5_di(), (struct ccdigest_ctx *)shim->real_ctx, md);
	free(shim->real_ctx);
	shim->real_ctx = NULL;
	return 1;
}

unsigned char *
CC_MD5(const void *data, CC_LONG len, unsigned char *md)
{
	CC_MD5_CTX c;

	CC_MD5_Init(&c);
	CC_MD5_Update(&c, data, len);
	CC_MD5_Final(md, &c);
	return md;
}
