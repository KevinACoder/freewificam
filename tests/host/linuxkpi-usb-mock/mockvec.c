/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * mockvec.c - loader for the container's vector data files.
 *
 * @design data-driven test input - ref linux:tools/testing/selftests patterns
 *         that read a fixture file and compare it against a captured buffer;
 *         the file format itself is the mock container's (its checker is the
 *         authority, this loader consumes).
 */

#include <stdio.h>
#include <string.h>

#include "mockvec.h"

static int
hex_val(int c)
{

	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void
stem_of(const char *file, char *out, size_t outlen)
{
	const char *base = strrchr(file, '/');
	size_t n;

	base = base != NULL ? base + 1 : file;
	n = strlen(base);
	if (n > 4 && strcmp(base + n - 4, ".dat") == 0)
		n -= 4;
	if (n >= outlen)
		n = outlen - 1;
	memcpy(out, base, n);
	out[n] = '\0';
}

int
mockvec_load(const char *root, const char *seg, const char *file,
    struct mockvec *out, char *err, size_t errlen)
{
	char path[MOCKVEC_ID_MAX + 256];
	FILE *f;
	char line[512];
	long declared = -1;
	size_t got = 0;
	int in_payload = 0;

	if (root == NULL || seg == NULL || file == NULL || out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));
	stem_of(file, out->id, sizeof(out->id));

	if (snprintf(path, sizeof(path), "%s/%s/%s", root, seg, file) >=
	    (int)sizeof(path)) {
		snprintf(err, errlen, "vector path too long");
		return -1;
	}
	f = fopen(path, "r");
	if (f == NULL) {
		snprintf(err, errlen, "cannot open %s", path);
		return -1;
	}
	if (snprintf(out->seg, sizeof(out->seg), "%s", seg) >=
	    (int)sizeof(out->seg)) {
		fclose(f);
		snprintf(err, errlen, "segment id too long");
		return -1;
	}

	while (fgets(line, sizeof(line), f) != NULL) {
		char *nl = strchr(line, '\n');
		char *eq;

		if (nl != NULL)
			*nl = '\0';
		if (line[0] == '\0' || line[0] == '#')
			continue;
		if (!in_payload) {
			eq = strchr(line, '=');
			if (eq == NULL) {
				snprintf(err, errlen, "%s: malformed header",
				    path);
				fclose(f);
				return -1;
			}
			*eq = '\0';
			if (strcmp(line, "payload_bytes") == 0) {
				declared = strtol(eq + 1, NULL, 10);
				in_payload = 1;
			} else if (strcmp(line, "dir") == 0) {
				snprintf(out->dir, sizeof(out->dir), "%s",
				    eq + 1);
			} else if (strcmp(line, "anchor") == 0) {
				snprintf(out->anchor, sizeof(out->anchor), "%s",
				    eq + 1);
			}
			continue;
		}
		/* payload: lower-case hex, two characters per byte */
		{
			const char *p = line;

			while (p[0] != '\0' && p[1] != '\0') {
				int hi = hex_val(p[0]);
				int lo = hex_val(p[1]);

				if (hi < 0 || lo < 0)
					break;
				if (got >= MOCKVEC_MAX_PAYLOAD) {
					snprintf(err, errlen,
					    "%s: payload exceeds the loader "
					    "buffer", path);
					fclose(f);
					return -1;
				}
				out->payload[got++] = (unsigned char)
				    ((hi << 4) | lo);
				p += 2;
			}
		}
	}
	fclose(f);

	if (declared < 0) {
		snprintf(err, errlen, "%s: no payload_bytes header", path);
		return -1;
	}
	if ((long)got != declared) {
		snprintf(err, errlen,
		    "%s: payload_bytes=%ld but %u bytes decoded", path,
		    declared, (unsigned int)got);
		return -1;
	}
	out->len = got;
	return 0;
}

int
mockvec_diff(const unsigned char *got, size_t got_len, const struct mockvec *v)
{
	size_t n = got_len < v->len ? got_len : v->len;
	size_t i;

	for (i = 0; i < n; i++) {
		if (got[i] != v->payload[i])
			return (int)i;
	}
	if (got_len != v->len)
		return (int)n;
	return -1;
}
