/*
 * @file
 * @brief Bind the <crypto/aes/aes.h> dispatchers to the default BearSSL
 * implementation.
 *
 * NetBSD picks the implementation in sys/crypto/aes/aes_impl.c, whose
 * sysctl/module scaffolding has no counterpart here.  This keeps the
 * verbatim imports untouched and provides the same entry points with a
 * fixed implementation choice, mirroring aes_select() falling back to
 * aes_bear_impl.
 *
 * @date 08.09.2026
 * @author zhugengyu
 */

#include <crypto/aes/aes.h>
#include <crypto/aes/aes_bear.h>
#include <crypto/aes/aes_impl.h>

static const struct aes_impl *const aes_impl = &aes_bear_impl;

void
aes_setenckey(struct aesenc *enc, const uint8_t key[static 16],
    uint32_t nrounds)
{

	aes_impl->ai_setenckey(enc, key, nrounds);
}

uint32_t
aes_setenckey128(struct aesenc *enc, const uint8_t key[static 16])
{
	uint32_t nrounds = AES_128_NROUNDS;

	aes_setenckey(enc, key, nrounds);
	return nrounds;
}

uint32_t
aes_setenckey192(struct aesenc *enc, const uint8_t key[static 24])
{
	uint32_t nrounds = AES_192_NROUNDS;

	aes_setenckey(enc, key, nrounds);
	return nrounds;
}

uint32_t
aes_setenckey256(struct aesenc *enc, const uint8_t key[static 32])
{
	uint32_t nrounds = AES_256_NROUNDS;

	aes_setenckey(enc, key, nrounds);
	return nrounds;
}

void
aes_enc(const struct aesenc *enc, const uint8_t in[static 16],
    uint8_t out[static 16], uint32_t nrounds)
{

	aes_impl->ai_enc(enc, in, out, nrounds);
}

void
aes_dec(const struct aesdec *dec, const uint8_t in[static 16],
    uint8_t out[static 16], uint32_t nrounds)
{

	aes_impl->ai_dec(dec, in, out, nrounds);
}

void
aes_cbcmac_update1(const struct aesenc *enc, const uint8_t in[static 16],
    size_t nbytes, uint8_t auth[static 16], uint32_t nrounds)
{

	aes_impl->ai_cbcmac_update1(enc, in, nbytes, auth, nrounds);
}

void
aes_ccm_enc1(const struct aesenc *enc, const uint8_t in[static 16],
    uint8_t out[static 16], size_t nbytes, uint8_t authctr[static 32],
    uint32_t nrounds)
{

	aes_impl->ai_ccm_enc1(enc, in, out, nbytes, authctr, nrounds);
}

void
aes_ccm_dec1(const struct aesenc *enc, const uint8_t in[static 16],
    uint8_t out[static 16], size_t nbytes, uint8_t authctr[static 32],
    uint32_t nrounds)
{

	aes_impl->ai_ccm_dec1(enc, in, out, nbytes, authctr, nrounds);
}
