#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_build=$(mktemp -d)
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
${CC:-cc} -std=gnu11 -Wall -Wextra -g -fsanitize=address,undefined \
    -include stdint.h -include stdbool.h \
    '-D__printflike(a,b)=__attribute__((format(printf,a,b)))' \
    -Itests/include -idirafter compat/netbsd \
    tests/mbuf_trim.c port/net/embox/bsd_mbuf.c -o "$test_build/mbuf_trim"
"$test_build/mbuf_trim"
${CC:-cc} -std=gnu11 -D_KERNEL -g -fsanitize=address,undefined \
    -include tests/include/crypto_host.h \
    -Itests/include -I. -idirafter compat/netbsd \
    tests/ccm.c crypto/aes/aes_bear.c crypto/aes/aes_ct.c \
    crypto/aes/aes_ct_enc.c crypto/aes/aes_ct_dec.c crypto/aes/aes_ccm.c \
    crypto/aes/aes_ccm_mbuf.c port/aes_impl_compat.c port/net/embox/bsd_mbuf.c \
    -o "$test_build/ccm" 2>"$test_build/ccm-build.log" || {
        cat "$test_build/ccm-build.log" >&2
        exit 1
    }
"$test_build/ccm"
