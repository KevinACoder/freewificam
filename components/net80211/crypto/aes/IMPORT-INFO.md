Verbatim import of NetBSD sys/crypto/aes (netbsd-11), the dependency
closure of net80211/ieee80211_crypto_ccmp.c:

  aes.h aes_impl.h aes_ccm.h aes_ccm_mbuf.h aes_bear.h
  aes_bear.c aes_ccm.c aes_ccm_mbuf.c

Not imported on purpose:

  aes_impl.c   its sysctl/module scaffolding does not fit the port; the
               <crypto/aes/aes.h> dispatchers it would provide are bound
               to the default BearSSL implementation (aes_bear_impl) by
               port/aes_impl_compat.c instead.

  files.aes arch/ selftests left to the NetBSD build.

Import rules follow ../net80211/IMPORT-INFO.md: keep the files byte
identical to the NetBSD tree and express port adaptations outside them.

Local corrections (2026-09-09):

- aes_ccm_mbuf.c: subtract the length of the segment being skipped before
  advancing to the next mbuf. Unequal segment lengths otherwise underflow
  the offset. tests/ccm.c covers every split of RFC 3610 vector 1 and rejects
  a forged MIC without retaining unauthenticated plaintext.
- aes_ct.c/aes_ct_enc.c/aes_ct_dec.c are included as dependencies of the
  BearSSL implementation.
