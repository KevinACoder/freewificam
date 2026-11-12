/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file    linuxkpi_ccm.c
 * @brief   The one software AEAD the WPA2 round needs: ccm(aes).
 *
 * mac80211 allocates a ccm(aes) transformation for *every* CCMP key, even a
 * key it is about to hand to the device (net/mac80211/key.c:
 * ieee80211_aes_key_setup_encrypt).  A driver that offloads the PTK still
 * needs this object to exist, or NL80211_CMD_NEW_KEY never reaches the
 * driver.  And the AC 7260 (gen1, no iwl_mvm_has_new_tx_api) deliberately
 * does not upload an AP's group key to the firmware
 * (mvm/mac80211.c, "GTK on AP interface is a TX-only key"): group-addressed
 * frames (ARP) are encrypted and decrypted by mac80211 itself.
 *
 * The implementation is therefore real, not a stub: AES-128 from FIPS-197
 * (encrypt-only, which is all CCM ever needs) and CCM as in RFC 3610 with
 * L=2, M=authsize.  It follows mac80211's b_0 convention exactly: the IV the
 * stack passes is b_0, whose first byte is L' and whose bytes 1..13 are the
 * nonce.
 *
 * The code is written here; the algorithms are the published ones.  No
 * mbedtls and no hostap crypto object is pulled in, so the provider also
 * builds in images that do not carry hostapd (and in the host bench).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "linuxkpi_core.h"

#include <linux/err.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/string.h>

#include <crypto/aead.h>

#ifndef EBADMSG
#define EBADMSG	74		/* asm-generic/errno.h: authentication failure */
#endif

#define LKPI_CCM_L		2	/* length field: 2 bytes */
#define LKPI_CCM_NONCE_LEN	(15 - LKPI_CCM_L)

struct lkpi_ccm_tfm {
	struct crypto_aead	base;
	u8			key[16];
	u8			rk[176];	/* AES-128 round keys */
};

/* ------------------------------------------------------------------ */
/* AES-128 (FIPS-197), encryption direction only                       */
/* ------------------------------------------------------------------ */

static const u8 lkpi_aes_sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const u8 lkpi_aes_rcon[10] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
};

static u8
lkpi_xtime(u8 x)
{
	return (u8)((x << 1) ^ ((x >> 7) * 0x1b));
}

static void
lkpi_aes_add_round_key(u8 st[16], const u8 *rk)
{
	unsigned int i;

	for (i = 0; i < 16; i++)
		st[i] ^= rk[i];
}

static void
lkpi_aes_sub_bytes(u8 st[16])
{
	unsigned int i;

	for (i = 0; i < 16; i++)
		st[i] = lkpi_aes_sbox[st[i]];
}

static void
lkpi_aes_shift_rows(u8 st[16])
{
	u8 t;

	t = st[1]; st[1] = st[5]; st[5] = st[9]; st[9] = st[13]; st[13] = t;
	t = st[2]; st[2] = st[10]; st[10] = t;
	t = st[6]; st[6] = st[14]; st[14] = t;
	t = st[15]; st[15] = st[11]; st[11] = st[7]; st[7] = st[3]; st[3] = t;
}

static void
lkpi_aes_mix_columns(u8 st[16])
{
	unsigned int i;

	for (i = 0; i < 4; i++) {
		u8 a0 = st[4 * i + 0];
		u8 a1 = st[4 * i + 1];
		u8 a2 = st[4 * i + 2];
		u8 a3 = st[4 * i + 3];
		u8 t = a0 ^ a1 ^ a2 ^ a3;

		st[4 * i + 0] = a0 ^ t ^ lkpi_xtime(a0 ^ a1);
		st[4 * i + 1] = a1 ^ t ^ lkpi_xtime(a1 ^ a2);
		st[4 * i + 2] = a2 ^ t ^ lkpi_xtime(a2 ^ a3);
		st[4 * i + 3] = a3 ^ t ^ lkpi_xtime(a3 ^ a0);
	}
}

static void
lkpi_aes_setkey(u8 rk[176], const u8 key[16])
{
	u8 w[176];
	unsigned int i;

	memcpy(w, key, 16);
	for (i = 4; i < 44; i++) {
		u8 t[4];

		memcpy(t, &w[(i - 1) * 4], 4);
		if ((i % 4) == 0) {
			u8 tmp = t[0];

			t[0] = lkpi_aes_sbox[t[1]] ^ lkpi_aes_rcon[i / 4 - 1];
			t[1] = lkpi_aes_sbox[t[2]];
			t[2] = lkpi_aes_sbox[t[3]];
			t[3] = lkpi_aes_sbox[tmp];
		}
		w[i * 4 + 0] = w[(i - 4) * 4 + 0] ^ t[0];
		w[i * 4 + 1] = w[(i - 4) * 4 + 1] ^ t[1];
		w[i * 4 + 2] = w[(i - 4) * 4 + 2] ^ t[2];
		w[i * 4 + 3] = w[(i - 4) * 4 + 3] ^ t[3];
	}
	memcpy(rk, w, 176);
}

static void
lkpi_aes_encrypt_block(const u8 rk[176], const u8 in[16], u8 out[16])
{
	u8 st[16];
	unsigned int round;

	memcpy(st, in, 16);
	lkpi_aes_add_round_key(st, rk);
	for (round = 1; round < 10; round++) {
		lkpi_aes_sub_bytes(st);
		lkpi_aes_shift_rows(st);
		lkpi_aes_mix_columns(st);
		lkpi_aes_add_round_key(st, &rk[round * 16]);
	}
	lkpi_aes_sub_bytes(st);
	lkpi_aes_shift_rows(st);
	lkpi_aes_add_round_key(st, &rk[160]);
	memcpy(out, st, 16);
}

/* ------------------------------------------------------------------ */
/* CCM (RFC 3610), L = 2                                               */
/* ------------------------------------------------------------------ */

static void
lkpi_ccm_mac(const u8 rk[176], const u8 nonce[LKPI_CCM_NONCE_LEN],
	     const u8 *aad, size_t aad_len, const u8 *msg, size_t msg_len,
	     unsigned int mic_len, u8 x[16])
{
	u8 b0[16];
	u8 block[16];
	unsigned int j;
	size_t i;

	memset(b0, 0, sizeof(b0));
	b0[0] = (u8)((aad_len ? 0x40u : 0u) |
		     (((mic_len - 2u) / 2u) << 3) |
		     (LKPI_CCM_L - 1u));
	memcpy(&b0[1], nonce, LKPI_CCM_NONCE_LEN);
	b0[14] = (u8)(msg_len >> 8);
	b0[15] = (u8)msg_len;

	memcpy(x, b0, 16);
	lkpi_aes_encrypt_block(rk, x, x);

	if (aad_len != 0) {
		u8 aad_buf[32];
		size_t n = 0;

		/* short form only: the stack's AAD is <= 30 bytes */
		aad_buf[n++] = (u8)(aad_len >> 8);
		aad_buf[n++] = (u8)aad_len;
		memcpy(&aad_buf[n], aad, aad_len);
		n += aad_len;
		while ((n % 16) != 0)
			aad_buf[n++] = 0;
		for (i = 0; i < n; i += 16) {
			for (j = 0; j < 16; j++)
				x[j] ^= aad_buf[i + j];
			lkpi_aes_encrypt_block(rk, x, x);
		}
	}

	for (i = 0; i < msg_len; i += 16) {
		size_t left = msg_len - i;
		size_t take = left < 16 ? left : 16;

		memset(block, 0, sizeof(block));
		memcpy(block, &msg[i], take);
		for (j = 0; j < 16; j++)
			x[j] ^= block[j];
		lkpi_aes_encrypt_block(rk, x, x);
	}
}

static void
lkpi_ccm_tag(const u8 rk[176], const u8 nonce[LKPI_CCM_NONCE_LEN],
	     const u8 x[16], u8 *mic, unsigned int mic_len)
{
	u8 a0[16];
	u8 s0[16];
	unsigned int i;

	memset(a0, 0, sizeof(a0));
	a0[0] = LKPI_CCM_L - 1;
	memcpy(&a0[1], nonce, LKPI_CCM_NONCE_LEN);
	lkpi_aes_encrypt_block(rk, a0, s0);
	for (i = 0; i < mic_len; i++)
		mic[i] = (u8)(x[i] ^ s0[i]);
}

static void
lkpi_ccm_ctr(const u8 rk[176], const u8 nonce[LKPI_CCM_NONCE_LEN],
	     u8 *data, size_t len)
{
	u8 a[16];
	u8 s[16];
	size_t i;
	unsigned int counter = 0;

	memset(a, 0, sizeof(a));
	a[0] = LKPI_CCM_L - 1;
	memcpy(&a[1], nonce, LKPI_CCM_NONCE_LEN);

	for (i = 0; i < len; i += 16) {
		size_t take = len - i < 16 ? len - i : 16;
		size_t j;

		counter++;
		a[14] = (u8)(counter >> 8);
		a[15] = (u8)counter;
		lkpi_aes_encrypt_block(rk, a, s);
		for (j = 0; j < take; j++)
			data[i + j] ^= s[j];
	}
}

static int
lkpi_ccm_crypt(const u8 rk[176], const u8 nonce[LKPI_CCM_NONCE_LEN],
	       const u8 *aad, size_t aad_len, u8 *data, size_t len,
	       u8 *mic, unsigned int mic_len, int encrypt)
{
	u8 x[16];
	u8 tag[16];
	unsigned int i;
	unsigned int diff = 0;

	if (mic_len < 4 || mic_len > 16 || (mic_len & 1) != 0)
		return -EINVAL;
	if (aad_len > 30)
		return -EINVAL;
	if (len > 0xffff)
		return -EINVAL;

	if (!encrypt) {
		lkpi_ccm_ctr(rk, nonce, data, len);
		lkpi_ccm_mac(rk, nonce, aad, aad_len, data, len, mic_len, x);
		lkpi_ccm_tag(rk, nonce, x, tag, mic_len);
		for (i = 0; i < mic_len; i++)
			diff |= (unsigned int)(tag[i] ^ mic[i]);
		if (diff != 0)
			return -EBADMSG;
		return 0;
	}

	lkpi_ccm_mac(rk, nonce, aad, aad_len, data, len, mic_len, x);
	lkpi_ccm_tag(rk, nonce, x, mic, mic_len);
	lkpi_ccm_ctr(rk, nonce, data, len);
	return 0;
}

/* ------------------------------------------------------------------ */
/* the crypto API face                                                 */
/* ------------------------------------------------------------------ */

static inline struct lkpi_ccm_tfm *
lkpi_ccm_from_tfm(struct crypto_aead *tfm)
{
	return container_of(tfm, struct lkpi_ccm_tfm, base);
}

struct crypto_aead *
crypto_alloc_aead(const char *alg_name, u32 type, u32 mask)
{
	struct lkpi_ccm_tfm *tfm;

	(void)type;
	(void)mask;
	if (alg_name == NULL || strcmp(alg_name, "ccm(aes)") != 0)
		return ERR_PTR(-ENOSYS);
	tfm = kzalloc(sizeof(*tfm), GFP_KERNEL);
	if (tfm == NULL)
		return ERR_PTR(-ENOMEM);
	return &tfm->base;
}

int
crypto_aead_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen)
{
	struct lkpi_ccm_tfm *t;

	if (tfm == NULL || key == NULL || keylen != 16)
		return -EINVAL;
	t = lkpi_ccm_from_tfm(tfm);
	memcpy(t->key, key, 16);
	lkpi_aes_setkey(t->rk, t->key);
	return 0;
}

int
crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	if (tfm == NULL || authsize < 4 || authsize > 16 || (authsize & 1) != 0)
		return -EINVAL;
	tfm->authsize = authsize;
	return 0;
}

static u8 *
lkpi_ccm_sg_data(struct scatterlist *sg)
{
	if (sg == NULL)
		return NULL;
	return (u8 *)page_address(sg_page(sg)) + sg->offset;
}

int
crypto_aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *tfm;
	struct lkpi_ccm_tfm *t;
	struct scatterlist *sg0;
	struct scatterlist *sg1;
	struct scatterlist *sg2;
	unsigned int mic_len;
	size_t data_len;

	if (req == NULL || req->src == NULL || req->iv == NULL)
		return -EINVAL;
	tfm = crypto_aead_reqtfm(req);
	t = lkpi_ccm_from_tfm(tfm);
	mic_len = crypto_aead_authsize(tfm);
	data_len = req->cryptlen;
	if (mic_len < 4 || mic_len > 16 || (mic_len & 1) != 0)
		return -EINVAL;

	sg0 = req->src;
	sg1 = sg_next(sg0);
	sg2 = sg1 != NULL ? sg_next(sg1) : NULL;
	if (sg0 == NULL || sg1 == NULL || sg2 == NULL)
		return -EINVAL;
	if (sg0->length < req->assoclen || sg1->length < data_len ||
	    sg2->length < mic_len)
		return -EINVAL;

	return lkpi_ccm_crypt(t->rk, req->iv + 1,
			      lkpi_ccm_sg_data(sg0), req->assoclen,
			      lkpi_ccm_sg_data(sg1), data_len,
			      lkpi_ccm_sg_data(sg2), mic_len, 1);
}

int
crypto_aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *tfm;
	struct lkpi_ccm_tfm *t;
	struct scatterlist *sg0;
	struct scatterlist *sg1;
	struct scatterlist *sg2;
	unsigned int mic_len;
	size_t data_len;

	if (req == NULL || req->src == NULL || req->iv == NULL)
		return -EINVAL;
	tfm = crypto_aead_reqtfm(req);
	t = lkpi_ccm_from_tfm(tfm);
	mic_len = crypto_aead_authsize(tfm);
	if (mic_len < 4 || mic_len > 16 || (mic_len & 1) != 0)
		return -EINVAL;
	if (req->cryptlen < mic_len)
		return -EINVAL;
	data_len = req->cryptlen - mic_len;

	sg0 = req->src;
	sg1 = sg_next(sg0);
	sg2 = sg1 != NULL ? sg_next(sg1) : NULL;
	if (sg0 == NULL || sg1 == NULL || sg2 == NULL)
		return -EINVAL;
	if (sg0->length < req->assoclen || sg1->length < data_len ||
	    sg2->length < mic_len)
		return -EINVAL;

	return lkpi_ccm_crypt(t->rk, req->iv + 1,
			      lkpi_ccm_sg_data(sg0), req->assoclen,
			      lkpi_ccm_sg_data(sg1), data_len,
			      lkpi_ccm_sg_data(sg2), mic_len, 0);
}

void
crypto_destroy_tfm(void *mem, struct crypto_tfm *tfm)
{
	struct lkpi_ccm_tfm *t;

	(void)tfm;
	if (mem == NULL)
		return;
	t = container_of((struct crypto_aead *)mem, struct lkpi_ccm_tfm, base);
	memset(t->key, 0, sizeof(t->key));
	memset(t->rk, 0, sizeof(t->rk));
	kfree_sensitive(t);
}
