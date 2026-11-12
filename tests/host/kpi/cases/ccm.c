/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * @file   ccm.c
 * @brief  The ccm(aes) provider, against RFC 3610 packet vector #1 and a
 *         mac80211-shaped round trip.
 *
 * mac80211 allocates this transformation for every CCMP key, so a broken
 * provider breaks the handshake before the driver sees a key - not just the
 * group-addressed data path.  The vector pins AES-128, the B_0 flags, the
 * short-form AAD encoding and the tag; the round trip pins the direction the
 * stack actually uses (b_0 in, 8-byte tag, in-place data).
 */
#include <string.h>

#include "linuxkpi_core.h"

#include <crypto/aead.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "kpi_test.h"

static int
ccm_vector(void)
{
	/* RFC 3610, packet vector #1 (AES-128, L=2, M=8, 8-byte AAD) */
	static const u8 key[16] = {
		0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
		0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	};
	static const u8 nonce[13] = {
		0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00,
		0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5,
	};
	static const u8 aad[8] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	};
	static const u8 plain[23] = {
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e,
	};
	static const u8 want_ct[23] = {
		0x58, 0x8c, 0x97, 0x9a, 0x61, 0xc6, 0x63, 0xd2,
		0xf0, 0x66, 0xd0, 0xc2, 0xc0, 0xf9, 0x89, 0x80,
		0x6d, 0x5f, 0x6b, 0x61, 0xda, 0xc3, 0x84,
	};
	static const u8 want_mic[8] = {
		0x17, 0xe8, 0xd1, 0x2c, 0xfd, 0xf9, 0x26, 0xe0,
	};
	u8 iv[16];
	u8 data[23];
	u8 mic[8];
	struct crypto_aead *tfm;
	struct aead_request *req;
	struct scatterlist sg[3];
	int rc;

	tfm = crypto_alloc_aead("ccm(aes)", 0, 0);
	KPI_ASSERT(!IS_ERR(tfm), "ccm(aes) allocation failed");
	KPI_ASSERT_EQ(crypto_aead_setkey(tfm, key, sizeof(key)), 0,
		      "setkey");
	KPI_ASSERT_EQ(crypto_aead_setauthsize(tfm, sizeof(mic)), 0,
		      "setauthsize");

	memset(iv, 0, sizeof(iv));
	iv[0] = 2 - 1;			/* L' = L - 1 */
	memcpy(&iv[1], nonce, sizeof(nonce));

	memcpy(data, plain, sizeof(data));
	memset(mic, 0, sizeof(mic));
	sg_init_table(sg, 3);
	sg_set_buf(&sg[0], aad, sizeof(aad));
	sg_set_buf(&sg[1], data, sizeof(data));
	sg_set_buf(&sg[2], mic, sizeof(mic));

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	KPI_ASSERT(req != NULL, "request allocation failed");
	aead_request_set_tfm(req, tfm);
	aead_request_set_crypt(req, sg, sg, sizeof(data), iv);
	aead_request_set_ad(req, sizeof(aad));

	rc = crypto_aead_encrypt(req);
	KPI_ASSERT_EQ(rc, 0, "encrypt");
	KPI_ASSERT(memcmp(data, want_ct, sizeof(data)) == 0,
		   "ciphertext does not match RFC 3610");
	KPI_ASSERT(memcmp(mic, want_mic, sizeof(mic)) == 0,
		   "tag does not match RFC 3610");

	memcpy(data, want_ct, sizeof(data));
	memcpy(mic, want_mic, sizeof(mic));
	aead_request_set_crypt(req, sg, sg, sizeof(data) + sizeof(mic), iv);
	rc = crypto_aead_decrypt(req);
	KPI_ASSERT_EQ(rc, 0, "decrypt");
	KPI_ASSERT(memcmp(data, plain, sizeof(data)) == 0,
		   "plaintext does not round-trip");

	data[0] ^= 0x01;
	memcpy(mic, want_mic, sizeof(mic));
	aead_request_set_crypt(req, sg, sg, sizeof(data) + sizeof(mic), iv);
	rc = crypto_aead_decrypt(req);
	KPI_ASSERT_EQ(rc, -EBADMSG, "tampered ciphertext must fail");

	kfree(req);
	crypto_free_aead(tfm);

	tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	KPI_ASSERT(IS_ERR(tfm), "an unimplemented alg must not be allocated");
	return KPI_PASS;
}

int
kpi_case_ccm(void)
{
	return ccm_vector();
}
