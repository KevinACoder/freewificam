/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * fwc_proto.h - the wire format between the F429 and the Pi 4B.
 *
 * The F429 is only a capture and send node: one TCP connection carries video
 * (port 5000) and one carries the camera microphone's PCM (port 5001).  Every
 * message is a fixed 24-byte little-endian header followed by its payload.
 * TCP already protects the bytes on the wire; the CRC is there so the Pi can
 * prove that the payload it assembled is the payload the camera produced, and
 * so a single frame's integrity can be compared across the two logs without
 * shipping a hash implementation on the MCU.
 *
 * The CRC is the reflected CRC-32 used by zlib (poly 0xEDB88320, init/final
 * 0xFFFFFFFF), computed with a 16-entry nibble table: one table lookup per
 * half byte is cheap enough for a 168 MHz M4 at 14 frames per second.
 */

#ifndef FWC_PROTO_H
#define FWC_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define FWC_MAGIC 0x46574331U	/* "FWC1" as the u32 constant, LE on wire */

#define FWC_VERSION 1U
#define FWC_TYPE_VIDEO 1U
#define FWC_TYPE_AUDIO 2U

#define FWC_HEADER_BYTES 24U
#define FWC_MAX_PAYLOAD (128U * 1024U)

/* Defaults; the profiles carry the same numbers as Kconfig values. */
#define FWC_VIDEO_PORT 5000U
#define FWC_AUDIO_PORT 5001U

struct fwc_msg_hdr {
	uint32_t magic;
	uint8_t	 version;
	uint8_t	 type;
	uint16_t flags;
	uint32_t length;
	uint32_t timestamp_us;
	uint32_t seq;
	uint32_t crc32;
} __attribute__((packed));

_Static_assert(sizeof(struct fwc_msg_hdr) == FWC_HEADER_BYTES,
	       "FWC header must stay 24 bytes");

static inline uint32_t
fwc_crc32(const void *data, size_t len)
{
	static const uint32_t nibble[16] = {
		0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
		0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
		0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
		0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU,
	};
	const uint8_t *p = (const uint8_t *)data;
	uint32_t crc = 0xFFFFFFFFU;
	size_t i;

	for (i = 0U; i < len; i++) {
		crc ^= p[i];
		crc = (crc >> 4) ^ nibble[crc & 0x0FU];
		crc = (crc >> 4) ^ nibble[crc & 0x0FU];
	}
	return crc ^ 0xFFFFFFFFU;
}

#endif /* FWC_PROTO_H */
