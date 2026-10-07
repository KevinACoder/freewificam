/*
 * @file
 * @brief Byte order helpers.
 */

#ifndef _SYS_ENDIAN_H_
#define _SYS_ENDIAN_H_

#include <sys/cdefs.h>
#include <sys/types.h>

static inline uint16_t bswap16_impl(uint16_t v) {
	return __builtin_bswap16(v);
}
static inline uint32_t bswap32_impl(uint32_t v) {
	return __builtin_bswap32(v);
}

#define __bswap16(x) bswap16_impl(x)
#define __bswap32(x) bswap32_impl(x)
#define __bswap64(x) __builtin_bswap64(x)

#define __byte_swap_long(x) bswap32_impl(x)
#define __byte_swap_var(x) bswap32_impl(x)

#define __swap32md(x) bswap32_impl(x)

#define htobe16(x) __bswap16(x)
#define htobe32(x) __bswap32(x)
#define htobe64(x) __bswap64(x)
#define htole16(x) ((uint16_t)(x))
#define htole32(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))

#define be16toh(x) __bswap16(x)
#define be32toh(x) __bswap32(x)
#define be64toh(x) __bswap64(x)
#define le16toh(x) ((uint16_t)(x))
#define le32toh(x) ((uint32_t)(x))
#define le64toh(x) ((uint64_t)(x))

#define betoh16(x) be16toh(x)
#define letoh16(x) le16toh(x)
#define betoh32(x) be32toh(x)
#define letoh32(x) le32toh(x)
#define betoh64(x) be64toh(x)
#define letoh64(x) le64toh(x)

#define htons(x) htobe16(x)
#define htonl(x) htobe32(x)
#define ntohs(x) be16toh(x)
#define ntohl(x) be32toh(x)

#ifndef bswap32
static inline uint32_t bswap32(uint32_t v) {
	return bswap32_impl(v);
}
#endif

/* byte load/store helpers used by the imported crypto */
static inline uint16_t le16dec(const void *p) {
	const uint8_t *b = p;
	return (uint16_t) (b[0] | (b[1] << 8));
}
static inline uint32_t le32dec(const void *p) {
	const uint8_t *b = p;
	return (uint32_t) (b[0] | (b[1] << 8) | (b[2] << 16) |
	    ((uint32_t) b[3] << 24));
}
static inline uint64_t le64dec(const void *p) {
	const uint8_t *b = p;
	uint64_t v = 0;
	int i;

	for (i = 7; i >= 0; i--) {
		v = (v << 8) | b[i];
	}
	return v;
}
static inline void le32enc(void *p, uint32_t v) {
	uint8_t *b = p;

	b[0] = (uint8_t) v;
	b[1] = (uint8_t) (v >> 8);
	b[2] = (uint8_t) (v >> 16);
	b[3] = (uint8_t) (v >> 24);
}
static inline uint32_t be32dec(const void *p) {
	const uint8_t *b = p;
	return ((uint32_t) b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}
static inline void be32enc(void *p, uint32_t v) {
	uint8_t *b = p;

	b[0] = (uint8_t) (v >> 24);
	b[1] = (uint8_t) (v >> 16);
	b[2] = (uint8_t) (v >> 8);
	b[3] = (uint8_t) v;
}

#endif /* _SYS_ENDIAN_H_ */
