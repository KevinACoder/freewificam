/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_crypto.c
 * @brief   The crypto library entries the imported set names.
 *
 * @design This port carries no crypto provider.  The API records exist (the
 *         faces declare them and the stack's BIP/TKIP/CCMP code compiles), and
 *         every allocation returns -ENOSYS: a caller that reaches an
 *         encryption path gets a loud, correct failure instead of a fake
 *         success.  The two pure byte functions (xor/memneq) and the ARC4
 *         stream cipher are real because they are small, self-contained and
 *         the soft-MAC's WEP/TKIP paths need them on C5; AES-CMAC is not
 *         carried and says so.
 *
 *         The consequence for the first board gates: C2/C3 exercise an open
 *         network (no key install), so the encryption gap does not touch them.
 *         C5 (association) on an encrypted AP will, and that is where a real
 *         AES/CMAC provider lands.
 * @date    10.11.2026
 */

#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <crypto/aes.h>
#include <crypto/aead.h>
#include <crypto/arc4.h>
#include <crypto/skcipher.h>
#include <crypto/utils.h>
#include <linux/crypto.h>

/* ------------------------------------------------------------------ */
/* pure byte helpers                                                   */
/* ------------------------------------------------------------------ */

void
__crypto_xor(u8 *dst, const u8 *src1, const u8 *src2, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		dst[i] = src1[i] ^ src2[i];
}

unsigned long
__crypto_memneq(const void *a, const void *b, size_t size)
{
	const unsigned char *pa = a;
	const unsigned char *pb = b;
	unsigned long diff = 0;
	size_t i;

	for (i = 0; i < size; i++)
		diff |= (unsigned long)(pa[i] ^ pb[i]);
	return diff != 0 ? 1UL : 0UL;
}

/* ------------------------------------------------------------------ */
/* ARC4 (real: WEP/TKIP's stream cipher)                               */
/* ------------------------------------------------------------------ */

int
arc4_setkey(struct arc4_ctx *ctx, const u8 *in_key, unsigned int key_len)
{
	unsigned int i;
	unsigned int j = 0;
	u8 k[256];

	if (ctx == NULL || in_key == NULL || key_len == 0 || key_len > 256)
		return -EINVAL;
	for (i = 0; i < 256; i++) {
		ctx->S[i] = i;
		k[i] = in_key[i % key_len];
	}
	for (i = 0; i < 256; i++) {
		u32 t;

		j = (j + ctx->S[i] + k[i]) & 0xff;
		t = ctx->S[i];
		ctx->S[i] = ctx->S[j];
		ctx->S[j] = t;
	}
	ctx->x = 0;
	ctx->y = 0;
	return 0;
}

void
arc4_crypt(struct arc4_ctx *ctx, u8 *out, const u8 *in, unsigned int len)
{
	unsigned int i;

	if (ctx == NULL || out == NULL || in == NULL)
		return;
	for (i = 0; i < len; i++) {
		u32 t;

		ctx->x = (ctx->x + 1) & 0xff;
		ctx->y = (ctx->y + ctx->S[ctx->x]) & 0xff;
		t = ctx->S[ctx->x];
		ctx->S[ctx->x] = ctx->S[ctx->y];
		ctx->S[ctx->y] = t;
		out[i] = in[i] ^ (u8)ctx->S[(ctx->S[ctx->x] + ctx->S[ctx->y]) & 0xff];
	}
}

/* ------------------------------------------------------------------ */
/* AES-CMAC (not carried; the caller gets -ENOSYS)                     */
/* ------------------------------------------------------------------ */

int
aes_cmac_preparekey(struct aes_cmac_key *key, const u8 *in_key, size_t key_len)
{
	(void)key;
	(void)in_key;
	(void)key_len;
	return -ENOSYS;
}

void
aes_cmac_update(struct aes_cmac_ctx *ctx, const u8 *data, size_t data_len)
{
	(void)ctx;
	(void)data;
	(void)data_len;
}

void
aes_cmac_final(struct aes_cmac_ctx *ctx, u8 out[AES_BLOCK_SIZE])
{
	(void)ctx;
	memset(out, 0, AES_BLOCK_SIZE);
}

/* ------------------------------------------------------------------ */
/* the crypto API allocations (loudly unavailable)                     */
/* ------------------------------------------------------------------ */

struct crypto_aead *
crypto_alloc_aead(const char *alg_name, u32 type, u32 mask)
{
	(void)alg_name;
	(void)type;
	(void)mask;
	return ERR_PTR(-ENOSYS);
}

int
crypto_aead_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen)
{
	(void)tfm;
	(void)key;
	(void)keylen;
	return -ENOSYS;
}

int
crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	(void)tfm;
	(void)authsize;
	return -ENOSYS;
}

int
crypto_aead_encrypt(struct aead_request *req)
{
	(void)req;
	return -ENOSYS;
}

int
crypto_aead_decrypt(struct aead_request *req)
{
	(void)req;
	return -ENOSYS;
}

struct crypto_skcipher *
crypto_alloc_skcipher(const char *alg_name, u32 type, u32 mask)
{
	(void)alg_name;
	(void)type;
	(void)mask;
	return ERR_PTR(-ENOSYS);
}

int
crypto_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
		       unsigned int keylen)
{
	(void)tfm;
	(void)key;
	(void)keylen;
	return -ENOSYS;
}

int
crypto_skcipher_encrypt(struct skcipher_request *req)
{
	(void)req;
	return -ENOSYS;
}

int
crypto_skcipher_decrypt(struct skcipher_request *req)
{
	(void)req;
	return -ENOSYS;
}

void
crypto_destroy_tfm(void *mem, struct crypto_tfm *tfm)
{
	(void)mem;
	(void)tfm;
}
