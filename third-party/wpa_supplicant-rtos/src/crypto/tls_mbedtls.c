/*
 * SPDX-FileCopyrightText: 2020-2021 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "utils/includes.h"
#include "utils/common.h"

#include "tls.h"
#include "crypto/sha1.h"
#include "crypto/md5.h"
#include "crypto/sha256.h"
#include "crypto/sha384.h"
#include "random.h"
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/dhm.h>
#include "mbedtls/ssl_ticket.h"

#include <mbedtls/debug.h>
//#include <mbedtls/mbedtls_config.h>
#include <assert.h>

#define TLS_RANDOM_LEN        32
#define TLS_MASTER_SECRET_LEN 48
#define MAX_CIPHERSUITE       32

#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST) || defined(EAP_TEAP) || \
    defined(EAP_SERVER_TEAP)
#ifdef MBEDTLS_SSL_SESSION_TICKETS
#ifdef MBEDTLS_SSL_TICKET_C
#define TLS_MBEDTLS_SESSION_TICKETS
#if defined(EAP_TEAP) || defined(EAP_SERVER_TEAP)
#define TLS_MBEDTLS_EAP_TEAP
#endif
#if !defined(CONFIG_FIPS) /* EAP-FAST keys cannot be exported in FIPS mode */
#if defined(EAP_FAST) || defined(EAP_FAST_DYNAMIC) || defined(EAP_SERVER_FAST)
#define TLS_MBEDTLS_EAP_FAST
#endif
#endif
#endif
#endif
#endif

/* Throw a compilation error if basic requirements in mbedtls are not enabled */
#if !defined(MBEDTLS_SSL_TLS_C)
#error "TLS not enabled in mbedtls config"
#endif

#if !defined(MBEDTLS_SHA256_C)
#error "SHA256 is disabled in mbedtls config"
#endif

#if !defined(MBEDTLS_AES_C)
#error "AES support is disabled in mbedtls config"
#endif

#ifndef MBEDTLS_PRIVATE
#define MBEDTLS_PRIVATE(member) member
#endif

uint32_t tls_instance_count;
struct tls_data
{
    /* Data for mbedlts */
    struct wpabuf *in_data;
    /* Data from mbedtls */
    struct wpabuf *out_data;
};

mbedtls_ssl_export_keys_ext_t tls_connection_export_keys_cb;

typedef struct tls_context
{
    mbedtls_ssl_context ssl;           /*!< TLS/SSL context */
    mbedtls_entropy_context entropy;   /*!< mbedTLS entropy context structure */
    mbedtls_ctr_drbg_context ctr_drbg; /*!< mbedTLS ctr drbg context structure */
    mbedtls_ssl_config conf;           /*!< TLS/SSL config to be shared structures */
    mbedtls_x509_crt cacert;           /*!< Container for X.509 CA certificate */
    mbedtls_x509_crt *cacert_ptr;      /*!< Pointer to the cacert being used. */
    mbedtls_x509_crt clientcert;       /*!< Container for X.509 client certificate */
    mbedtls_pk_context clientkey;      /*!< Private key of client certificate */
    mbedtls_dhm_context dhm;           /*!< DH parameters */
#ifdef MBEDTLS_SSL_SESSION_TICKETS
    mbedtls_ssl_ticket_context ticket_ctx;
#endif
    int ciphersuite[MAX_CIPHERSUITE];
    unsigned int has_client_cert : 1;
    unsigned int has_private_key : 1;
} tls_context_t;

struct tls_global
{
    int server;
    tls_context_t *tls;
    struct tls_connection *conn;
    int check_crl;

    void (*event_cb)(void *ctx, enum tls_event ev, union tls_event_data *data);
    void *cb_ctx;
    int cert_in_cb;
    mbedtls_ctr_drbg_context ctr_drbg;
};

struct tls_connection
{
    struct tls_global *global;
    tls_context_t *tls;
    int verify;
    unsigned int resumed : 1;
    unsigned int is_server : 1;
    size_t expkey_keyblock_size;
    struct tls_data tls_io_data;
    unsigned char master_secret[TLS_MASTER_SECRET_LEN];
    unsigned char randbytes[2 * TLS_RANDOM_LEN];
    mbedtls_tls_prf_types tls_prf_type;
#ifdef TLS_MBEDTLS_SESSION_TICKETS
    tls_session_ticket_cb session_ticket_cb;
    void *session_ticket_cb_ctx;
    unsigned char *clienthello_session_ticket;
    size_t clienthello_session_ticket_len;
#endif
    char *peer_subject; /* peer subject info for authenticated peer */
};

static int f_rng(void *p_rng, unsigned char *buf, size_t len)
{
    return random_get_bytes(buf, len);
}

static void tls_mbedtls_cleanup(tls_context_t *tls)
{
    if (!tls)
    {
        return;
    }
    tls->cacert_ptr = NULL;
    mbedtls_x509_crt_free(&tls->cacert);
    mbedtls_x509_crt_free(&tls->clientcert);
    mbedtls_pk_free(&tls->clientkey);
    mbedtls_entropy_free(&tls->entropy);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_ctr_drbg_free(&tls->ctr_drbg);
    mbedtls_dhm_free(&tls->dhm);
#ifdef MBEDTLS_SSL_SESSION_TICKETS
    mbedtls_ssl_ticket_free(&tls->ticket_ctx);
#endif
    mbedtls_ssl_free(&tls->ssl);
}

#ifdef TLS_MBEDTLS_SESSION_TICKETS
void tls_connection_deinit_clienthello_session_ticket(struct tls_connection *conn)
{
    if (conn->clienthello_session_ticket)
    {
        mbedtls_platform_zeroize(conn->clienthello_session_ticket, conn->clienthello_session_ticket_len);
        mbedtls_free(conn->clienthello_session_ticket);
        conn->clienthello_session_ticket     = NULL;
        conn->clienthello_session_ticket_len = 0;
    }
}
#endif

static void tls_mbedtls_conn_delete(tls_context_t *tls)
{
    if (tls != NULL)
    {
        tls_mbedtls_cleanup(tls);
    }
}

static int tls_mbedtls_write(void *ctx, const unsigned char *buf, size_t len)
{
    struct tls_connection *conn = (struct tls_connection *)ctx;
    struct tls_data *data       = &conn->tls_io_data;

    if (wpabuf_resize(&data->out_data, len) < 0)
        return 0;

    wpabuf_put_data(data->out_data, buf, len);

    return len;
}

static int tls_mbedtls_read(void *ctx, unsigned char *buf, size_t len)
{
    struct tls_connection *conn = (struct tls_connection *)ctx;
    struct tls_data *data       = &conn->tls_io_data;
    struct wpabuf *local_buf;
    size_t data_len = len;

    if (data->in_data == NULL)
    {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }

    if (len > wpabuf_len(data->in_data))
    {
        wpa_printf(MSG_ERROR, "don't have suffient data\n");
        data_len = wpabuf_len(data->in_data);
    }

    os_memcpy(buf, wpabuf_head(data->in_data), data_len);
    /* adjust buffer */
    if (len < wpabuf_len(data->in_data))
    {
        local_buf =
            wpabuf_alloc_copy((char *)wpabuf_head(data->in_data) + data_len, wpabuf_len(data->in_data) - data_len);
        wpabuf_free(data->in_data);
        data->in_data = local_buf;
    }
    else
    {
        wpabuf_free(data->in_data);
        data->in_data = NULL;
    }

    return data_len;
}

static int tls_mbedtls_server_write(void *ctx, const unsigned char *buf, size_t len)
{
    struct tls_global *global   = (struct tls_global *)ctx;
    struct tls_connection *conn = (struct tls_connection *)global->conn;
    struct tls_data *data       = &conn->tls_io_data;

    if (wpabuf_resize(&data->out_data, len) < 0)
        return 0;

    wpabuf_put_data(data->out_data, buf, len);

    return len;
}

static int tls_mbedtls_server_read(void *ctx, unsigned char *buf, size_t len)
{
    struct tls_global *global   = (struct tls_global *)ctx;
    struct tls_connection *conn = (struct tls_connection *)global->conn;
    struct tls_data *data       = &conn->tls_io_data;
    struct wpabuf *local_buf;
    size_t data_len = len;

    if (data->in_data == NULL)
    {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }

    if (len > wpabuf_len(data->in_data))
    {
        wpa_printf(MSG_ERROR, "don't have suffient data\n");
        data_len = wpabuf_len(data->in_data);
    }

    os_memcpy(buf, wpabuf_head(data->in_data), data_len);
    /* adjust buffer */
    if (len < wpabuf_len(data->in_data))
    {
        local_buf =
            wpabuf_alloc_copy((char *)wpabuf_head(data->in_data) + data_len, wpabuf_len(data->in_data) - data_len);
        wpabuf_free(data->in_data);
        data->in_data = local_buf;
    }
    else
    {
        wpabuf_free(data->in_data);
        data->in_data = NULL;
    }

    return data_len;
}

static int set_pki_context(tls_context_t *tls, const struct tls_connection_params *cfg)
{
    int ret = 0;

    if (cfg->client_cert_blob == NULL || cfg->private_key_blob == NULL)
    {
        wpa_printf(MSG_ERROR, "%s: config not correct", __func__);
        return -1;
    }

    mbedtls_x509_crt_init(&tls->clientcert);
    mbedtls_pk_init(&tls->clientkey);

    ret = mbedtls_x509_crt_parse(&tls->clientcert, cfg->client_cert_blob, cfg->client_cert_blob_len);
    if (ret < 0)
    {
        wpa_printf(MSG_ERROR, "mbedtls_x509_crt_parse returned -0x%x", -ret);
        return ret;
    }

    ret = mbedtls_pk_parse_key(&tls->clientkey, cfg->private_key_blob, cfg->private_key_blob_len,
                               (const unsigned char *)cfg->private_key_passwd,
                               cfg->private_key_passwd ? os_strlen(cfg->private_key_passwd) : 0); //, f_rng, NULL);
    if (ret < 0)
    {
        wpa_printf(MSG_ERROR, "mbedtls_pk_parse_keyfile returned -0x%x", -ret);
        return ret;
    }

    ret = mbedtls_ssl_conf_own_cert(&tls->conf, &tls->clientcert, &tls->clientkey);
    if (ret < 0)
    {
        wpa_printf(MSG_ERROR, "mbedtls_ssl_conf_own_cert returned -0x%x", -ret);
        return ret;
    }

    return 0;
}

static int set_ca_cert(tls_context_t *tls, const unsigned char *cacert, size_t cacert_len)
{
    tls->cacert_ptr = &tls->cacert;
    mbedtls_x509_crt_init(tls->cacert_ptr);
    int ret = mbedtls_x509_crt_parse(tls->cacert_ptr, cacert, cacert_len);
    if (ret < 0)
    {
        wpa_printf(MSG_ERROR, "mbedtls_x509_crt_parse returned -0x%x", -ret);
        return ret;
    }
    mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls->conf, tls->cacert_ptr, NULL);

    return 0;
}

#ifdef CONFIG_SUITEB192
static int tls_sig_hashes_for_suiteb[] = {
#if defined(MBEDTLS_SHA512_C)
    MBEDTLS_MD_SHA512, MBEDTLS_MD_SHA384,
#endif
    MBEDTLS_MD_NONE};

const mbedtls_x509_crt_profile suiteb_mbedtls_x509_crt_profile = {
#if defined(MBEDTLS_SHA512_C)
    MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA384) | MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA512) |
#endif
        0,
    0xFFFFFFF, /* Any PK alg    */
    MBEDTLS_X509_ID_FLAG(MBEDTLS_ECP_DP_SECP384R1),
    1024,
};

static void tls_set_suiteb_config(tls_context_t *tls)
{
    const mbedtls_x509_crt_profile *crt_profile = &suiteb_mbedtls_x509_crt_profile;
    mbedtls_ssl_conf_cert_profile(&tls->conf, crt_profile);
    mbedtls_ssl_conf_sig_hashes(&tls->conf, tls_sig_hashes_for_suiteb);
}
#endif

static int tls_sig_hashes_for_eap[] = {
#if defined(MBEDTLS_SHA512_C)
    MBEDTLS_MD_SHA512, MBEDTLS_MD_SHA384,
#endif
#if defined(MBEDTLS_SHA256_C)
    MBEDTLS_MD_SHA256, MBEDTLS_MD_SHA224,
#endif
#if defined(MBEDTLS_SHA1_C)
    MBEDTLS_MD_SHA1,
#endif
    MBEDTLS_MD_NONE};

const mbedtls_x509_crt_profile eap_mbedtls_x509_crt_profile = {
#if defined(MBEDTLS_SHA1_C)
    MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA1) |
#endif
#if defined(MBEDTLS_SHA256_C)
        MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA224) | MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA256) |
#endif
#if defined(MBEDTLS_SHA512_C)
        MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA384) | MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA512) |
#endif
        0,
    0xFFFFFFF, /* Any PK alg    */
    0xFFFFFFF, /* Any curve     */
    1024,
};

static void tls_enable_sha1_config(tls_context_t *tls)
{
    const mbedtls_x509_crt_profile *crt_profile = &eap_mbedtls_x509_crt_profile;
    mbedtls_ssl_conf_cert_profile(&tls->conf, crt_profile);
    mbedtls_ssl_conf_sig_hashes(&tls->conf, tls_sig_hashes_for_eap);
}

static const int eap_ciphersuite_preference[] = {
#if defined(MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED)
#if defined(MBEDTLS_SHA512_C) && defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_DHE_RSA_WITH_AES_256_GCM_SHA384,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CCM,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CBC_SHA256, MBEDTLS_TLS_DHE_RSA_WITH_AES_256_CBC_SHA,
#endif
#if defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_DHE_RSA_WITH_CAMELLIA_256_GCM_SHA384,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA256, MBEDTLS_TLS_DHE_RSA_WITH_CAMELLIA_256_CBC_SHA,
#endif

#if defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_DHE_RSA_WITH_AES_128_GCM_SHA256,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CCM,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CBC_SHA256, MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CBC_SHA,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_DHE_RSA_WITH_AES_128_CCM_8,
#endif
#endif
#if defined(MBEDTLS_KEY_EXCHANGE_DHE_PSK_ENABLED)
    MBEDTLS_TLS_DHE_PSK_WITH_AES_256_GCM_SHA384, MBEDTLS_TLS_DHE_PSK_WITH_AES_256_CCM,
    MBEDTLS_TLS_DHE_PSK_WITH_AES_256_CBC_SHA384, MBEDTLS_TLS_DHE_PSK_WITH_AES_256_CBC_SHA,
    MBEDTLS_TLS_DHE_PSK_WITH_AES_256_CCM_8,

    MBEDTLS_TLS_DHE_PSK_WITH_AES_128_GCM_SHA256, MBEDTLS_TLS_DHE_PSK_WITH_AES_128_CCM,
    MBEDTLS_TLS_DHE_PSK_WITH_AES_128_CBC_SHA256, MBEDTLS_TLS_DHE_PSK_WITH_AES_128_CCM_8,
#endif
#if defined(MBEDTLS_SHA512_C) && defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_RSA_WITH_AES_256_GCM_SHA384,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_RSA_WITH_AES_256_CCM,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA256, MBEDTLS_TLS_RSA_WITH_AES_256_CBC_SHA,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_RSA_WITH_AES_256_CCM_8,
#endif

#if defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_RSA_WITH_AES_128_CCM,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA256, MBEDTLS_TLS_RSA_WITH_AES_128_CBC_SHA,
#endif
#if defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256, MBEDTLS_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA,
#endif
#if defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256, MBEDTLS_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_RSA_WITH_AES_128_CCM_8,
#endif

#if defined(MBEDTLS_KEY_EXCHANGE_PSK_ENABLED)
#if defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_RSA_PSK_WITH_AES_128_GCM_SHA256,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_RSA_PSK_WITH_AES_128_CBC_SHA256, MBEDTLS_TLS_RSA_PSK_WITH_AES_128_CBC_SHA,
#endif
/* The PSK suites */
#if defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_PSK_WITH_AES_256_GCM_SHA384,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_PSK_WITH_AES_256_CCM,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_PSK_WITH_AES_256_CBC_SHA384, MBEDTLS_TLS_PSK_WITH_AES_256_CBC_SHA,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_PSK_WITH_AES_256_CCM_8,
#endif

#if defined(MBEDTLS_GCM_C)
    MBEDTLS_TLS_PSK_WITH_AES_128_GCM_SHA256,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_PSK_WITH_AES_128_CCM,
#endif
#if defined(MBEDTLS_CIPHER_MODE_CBC)
    MBEDTLS_TLS_PSK_WITH_AES_128_CBC_SHA256, MBEDTLS_TLS_PSK_WITH_AES_128_CBC_SHA,
#endif
#if defined(MBEDTLS_CCM_C)
    MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8,
#endif
#endif

#if 0
	/* 3DES suites */
	MBEDTLS_TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA,
	MBEDTLS_TLS_DHE_PSK_WITH_3DES_EDE_CBC_SHA,
	MBEDTLS_TLS_RSA_WITH_3DES_EDE_CBC_SHA,
	MBEDTLS_TLS_RSA_PSK_WITH_3DES_EDE_CBC_SHA,
	MBEDTLS_TLS_PSK_WITH_3DES_EDE_CBC_SHA,
#endif
#if defined(MBEDTLS_ARC4_C)
    /* RC4 suites */
    MBEDTLS_TLS_DHE_PSK_WITH_RC4_128_SHA, MBEDTLS_TLS_RSA_WITH_RC4_128_SHA, MBEDTLS_TLS_RSA_WITH_RC4_128_MD5,
    MBEDTLS_TLS_RSA_PSK_WITH_RC4_128_SHA, MBEDTLS_TLS_PSK_WITH_RC4_128_SHA,
#endif
    0};

#ifdef CONFIG_SUITEB192
static const int suiteb_rsa_ciphersuite_preference[] = {
#if defined(MBEDTLS_GCM_C)
#if defined(MBEDTLS_SHA512_C)
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384, MBEDTLS_TLS_DHE_RSA_WITH_AES_256_GCM_SHA384,
#endif
#endif
    0};

static const int suiteb_ecc_ciphersuite_preference[] = {
#if defined(MBEDTLS_GCM_C)
#if defined(MBEDTLS_SHA512_C)
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
#endif
#endif
    0};
static const int suiteb_ciphersuite_preference[] = {
#if defined(MBEDTLS_GCM_C)
#if defined(MBEDTLS_SHA512_C)
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_DHE_RSA_WITH_AES_256_GCM_SHA384,
#endif
#endif
    0};
#endif

static void tls_set_ciphersuite(const struct tls_connection_params *cfg, tls_context_t *tls)
{
    /* Only set ciphersuite if cert's key length is high or ciphersuites are
     * set by user */
#ifdef CONFIG_SUITEB192
    if (cfg->flags & TLS_CONN_SUITEB)
    {
        /* cipher suites will be set based on certificate */
        mbedtls_pk_type_t pk_alg = mbedtls_pk_get_type(&tls->clientkey);
        if (pk_alg == MBEDTLS_PK_RSA || pk_alg == MBEDTLS_PK_RSASSA_PSS)
        {
            mbedtls_ssl_conf_ciphersuites(&tls->conf, suiteb_rsa_ciphersuite_preference);
        }
        else if (pk_alg == MBEDTLS_PK_ECDSA || pk_alg == MBEDTLS_PK_ECKEY || pk_alg == MBEDTLS_PK_ECKEY_DH)
        {
            mbedtls_ssl_conf_ciphersuites(&tls->conf, suiteb_ecc_ciphersuite_preference);
        }
        else
        {
            mbedtls_ssl_conf_ciphersuites(&tls->conf, suiteb_ciphersuite_preference);
        }
    }
    else
#endif
        if (tls->ciphersuite[0])
    {
        mbedtls_ssl_conf_ciphersuites(&tls->conf, tls->ciphersuite);
    }
    else if (mbedtls_pk_get_bitlen(&tls->clientkey) > 2048 ||
             (tls->cacert_ptr && mbedtls_pk_get_bitlen(&tls->cacert_ptr->pk) > 2048))
    {
        mbedtls_ssl_conf_ciphersuites(&tls->conf, eap_ciphersuite_preference);
    }
}

static int parse_certs(const struct tls_connection_params *cfg, tls_context_t *tls)
{
    int ret = 0;

#ifdef CONFIG_MBEDTLS_FS_IO
    if (cfg->ca_cert)
    {
        tls->cacert_ptr = &tls->cacert;
        mbedtls_x509_crt_init(tls->cacert_ptr);

        ret = mbedtls_x509_crt_parse_file(&tls->cacert, cfg->ca_cert);
        if (ret < 0)
        {
            wpa_printf(MSG_ERROR, "mbedtls_x509_crt_parse_der failed -0x%x", -ret);
            return -1;
        }

        mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&tls->conf, tls->cacert_ptr, NULL);
        wpa_printf(MSG_INFO, "Loaded CA cert: %s\n", cfg->ca_cert);
    }
    else
#endif
        if (cfg->ca_cert_blob != NULL)
    {
        ret = set_ca_cert(tls, cfg->ca_cert_blob, cfg->ca_cert_blob_len);
        if (ret != 0)
        {
            return ret;
        }
        mbedtls_ssl_conf_ca_chain(&tls->conf, tls->cacert_ptr, NULL);
    }
    else
    {
        mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_NONE);
    }

#ifdef CONFIG_MBEDTLS_FS_IO
    if (cfg->client_cert && cfg->private_key)
    {
        mbedtls_x509_crt_init(&tls->clientcert);
        ret = mbedtls_x509_crt_parse_file(&tls->clientcert, cfg->client_cert);
        if (ret < 0)
        {
            wpa_printf(MSG_ERROR, "mbedtls_x509_crt_parse_der failed -0x%x", -ret);
            return -1;
        }
        wpa_printf(MSG_INFO, "Loaded Client cert: %s\n", cfg->client_cert);

        mbedtls_pk_init(&tls->clientkey);
        ret = mbedtls_pk_parse_keyfile(&tls->clientkey, cfg->private_key, cfg->private_key_passwd, f_rng, NULL);
        if (ret < 0)
        {
            wpa_printf(MSG_ERROR, "mbedtls_pk_parse_key failed -0x%x", -ret);
            return -1;
        }
        wpa_printf(MSG_INFO, "Loaded private key: %s\n", cfg->private_key);

        ret = mbedtls_ssl_conf_own_cert(&tls->conf, &tls->clientcert, &tls->clientkey);
        if (ret < 0)
        {
            wpa_printf(MSG_ERROR, "mbedtls_ssl_conf_own_cert returned -0x%x", -ret);
            return ret;
        }

        wpa_printf(MSG_INFO, "Loaded client and key\n");
    }
    else
#endif
        if (cfg->client_cert_blob != NULL && cfg->private_key_blob != NULL)
    {
        ret = set_pki_context(tls, cfg);
        if (ret != 0)
        {
            wpa_printf(MSG_ERROR, "Failed to set client pki context");
            return ret;
        }
        tls->has_client_cert = 1;
        tls->has_private_key = 1;
    }

    if (cfg->dh_blob != NULL)
    {
        ret = mbedtls_dhm_parse_dhm(&tls->dhm, cfg->dh_blob, cfg->dh_blob_len);
        if (ret != 0)
        {
            wpa_printf(MSG_ERROR, "Failed to set DH params");
            return ret;
        }
    }

    return 0;
}

static int set_config(const struct tls_connection_params *cfg, tls_context_t *tls, int server)
{
    int ret    = 0;
    int preset = MBEDTLS_SSL_PRESET_DEFAULT;
    assert(cfg != NULL);
    assert(tls != NULL);

#ifdef CONFIG_SUITEB192
    if (cfg->flags & TLS_CONN_SUITEB)
        preset = MBEDTLS_SSL_PRESET_SUITEB;
#endif

    if (server)
    {
        ret = mbedtls_ssl_config_defaults(&tls->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, preset);
    }
    else
    {
        ret = mbedtls_ssl_config_defaults(&tls->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, preset);
    }
    if (ret != 0)
    {
        wpa_printf(MSG_ERROR, "mbedtls_ssl_config_defaults returned -0x%x", -ret);
        return ret;
    }

    if (preset != MBEDTLS_SSL_PRESET_SUITEB)
    {
        /* Enable SHA1 support since it's not enabled by default in
         * mbedtls */
        tls_enable_sha1_config(tls);
#ifdef CONFIG_SUITEB192
    }
    else
    {
        tls_set_suiteb_config(tls);
#endif
    }
    wpa_printf(MSG_INFO, ": mbedtls_ssl_config_defaults: ciphersuite: %s\n", mbedtls_ssl_get_ciphersuite(&tls->ssl));

    wpa_printf(MSG_INFO, ": CA cert: %s\n", cfg->ca_cert);

    if (server)
    {
        wpa_printf(MSG_INFO, ": Server cert: %s\n", cfg->client_cert);
        wpa_printf(MSG_INFO, ": Server key: %s\n", cfg->private_key);
    }
    else
    {
        wpa_printf(MSG_INFO, ": Client cert: %s\n", cfg->client_cert);
        wpa_printf(MSG_INFO, ": Client key: %s\n", cfg->private_key);
    }

    if ((ret = parse_certs(cfg, tls)))
    {
        wpa_printf(MSG_ERROR, "Failed to load certs: %d\n", ret);
        return ret;
    }
    wpa_printf(MSG_INFO, "Loaded certs\n");

    /* Usages of default ciphersuites can take a lot of time on low end
     * device and can cause watchdog. Enabling the ciphers which are secured
     * enough but doesn't take that much processing power */
    tls_set_ciphersuite(cfg, tls);

    return 0;
}

static int tls_create_mbedtls_handle(const struct tls_connection_params *params, tls_context_t *tls, int server)
{
    int ret                     = 0;
    unsigned int ticket_timeout = 86400;

    assert(params != NULL);
    assert(tls != NULL);

    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ctr_drbg_init(&tls->ctr_drbg);
    mbedtls_ssl_config_init(&tls->conf);
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_dhm_init(&tls->dhm);

    ret = set_config(params, tls, server);
    if (ret != 0)
    {
        wpa_printf(MSG_ERROR, "Failed to set client configurations");
        goto exit;
    }

    ret = mbedtls_ctr_drbg_seed(&tls->ctr_drbg, mbedtls_entropy_func, &tls->entropy, NULL, 0);
    if (ret != 0)
    {
        wpa_printf(MSG_ERROR, "mbedtls_ctr_drbg_seed returned -0x%x", -ret);
        goto exit;
    }

    mbedtls_ssl_conf_rng(&tls->conf, mbedtls_ctr_drbg_random, &tls->ctr_drbg);

    ret = mbedtls_ssl_setup(&tls->ssl, &tls->conf);
    if (ret != 0)
    {
        wpa_printf(MSG_ERROR, "mbedtls_ssl_setup returned -0x%x", -ret);
        goto exit;
    }
#if defined(MBEDTLS_SSL_CBC_RECORD_SPLITTING)
    /* Disable BEAST attack countermeasures for Windows 2008
     * interoperability */
    mbedtls_ssl_conf_cbc_record_splitting(&tls->conf, MBEDTLS_SSL_CBC_RECORD_SPLITTING_DISABLED);
#endif

#ifdef MBEDTLS_SSL_SESSION_TICKETS
    if (server)
    {
        mbedtls_ssl_ticket_init(&tls->ticket_ctx);
        mbedtls_ssl_ticket_setup(&tls->ticket_ctx, mbedtls_ctr_drbg_random, &tls->ctr_drbg, MBEDTLS_CIPHER_AES_256_GCM,
                                 86400);
    }
#endif

    return 0;

exit:
    tls_mbedtls_cleanup(tls);
    return ret;
}

void *tls_init(const struct tls_config *conf)
{
    struct tls_global *global;

    tls_instance_count++;

    global = os_zalloc(sizeof(*global));
    if (global == NULL)
        return NULL;

    if (conf)
    {
        global->event_cb   = conf->event_cb;
        global->cb_ctx     = conf->cb_ctx;
        global->cert_in_cb = conf->cert_in_cb;
    }

    return global;
}

void tls_deinit(void *tls_ctx)
{
    struct tls_global *global = tls_ctx;

    tls_instance_count--;

    if (global->server)
    {
        /* Free ssl ctx and data */
        tls_mbedtls_conn_delete((tls_context_t *)global->tls);
        os_free(global->tls);
    }

    global->tls = NULL;
    os_free(global);
}

struct tls_connection *tls_connection_init(void *tls_ctx)
{
    struct tls_global *global   = tls_ctx;
    struct tls_connection *conn = os_zalloc(sizeof(*conn));
    if (!conn)
    {
        wpa_printf(MSG_ERROR, "TLS: Failed to allocate connection memory");
        return NULL;
    }

    if (global->server)
    {
        conn->global = global;
        conn->tls    = global->tls;
        global->conn = conn;
        mbedtls_ssl_conf_export_keys_ext_cb(&conn->tls->conf, tls_connection_export_keys_cb, conn);
    }

    return conn;
}

void tls_connection_deinit(void *tls_ctx, struct tls_connection *conn)
{
    struct tls_global *global = tls_ctx;

    /* case: tls init failed */
    if (!conn)
    {
        return;
    }

    if (!global->server)
    {
        /* Free ssl ctx and data */
        tls_mbedtls_conn_delete((tls_context_t *)conn->tls);
        os_free(conn->tls);
    }
    conn->tls = NULL;

#ifdef TLS_MBEDTLS_SESSION_TICKETS
    if (conn->clienthello_session_ticket)
        tls_connection_deinit_clienthello_session_ticket(conn);
#endif

    /* Data in in ssl ctx, free connection */
    os_free(conn);
}

int tls_get_errors(void *tls_ctx)
{
    return 0;
}

int tls_connection_established(void *tls_ctx, struct tls_connection *conn)
{
    mbedtls_ssl_context *ssl = &conn->tls->ssl;

    if (ssl->MBEDTLS_PRIVATE(state) == MBEDTLS_SSL_HANDSHAKE_OVER)
    {
        return 1;
    }

    return 0;
}

int tls_global_set_verify(void *tls_ctx, int check_crl, int strict)
{
    struct tls_global *global = tls_ctx;
    global->check_crl         = check_crl;
    return 0;
}

int tls_connection_set_verify(void *tls_ctx,
                              struct tls_connection *conn,
                              int verify_peer,
                              unsigned int flags,
                              const u8 *session_ctx,
                              size_t session_ctx_len)
{
    conn->verify = verify_peer;
    return 0;
}

#ifdef TLS_MBEDTLS_SESSION_TICKETS
static int tls_mbedtls_clienthello_session_ticket_prep(struct tls_connection *conn, const u8 *data, size_t len)
{
    if (conn->tls_conf->flags & TLS_CONN_DISABLE_SESSION_TICKET)
        return -1;

    if (conn->clienthello_session_ticket)
        tls_connection_deinit_clienthello_session_ticket(conn);

    if (len)
    {
        conn->clienthello_session_ticket = mbedtls_calloc(1, len);
        if (conn->clienthello_session_ticket == NULL)
            return -1;

        conn->clienthello_session_ticket_len = len;
        os_memcpy(conn->clienthello_session_ticket, data, len);
    }

    return 0;
}

static void tls_mbedtls_clienthello_session_ticket_set(struct tls_connection *conn)
{
    mbedtls_ssl_session *sess = conn->ssl.MBEDTLS_PRIVATE(session_negotiate);
    if (sess->MBEDTLS_PRIVATE(ticket))
    {
        mbedtls_platform_zeroize(sess->MBEDTLS_PRIVATE(ticket), sess->MBEDTLS_PRIVATE(ticket_len));
        mbedtls_free(sess->MBEDTLS_PRIVATE(ticket));
    }
    sess->MBEDTLS_PRIVATE(ticket)          = conn->clienthello_session_ticket;
    sess->MBEDTLS_PRIVATE(ticket_len)      = conn->clienthello_session_ticket_len;
    sess->MBEDTLS_PRIVATE(ticket_lifetime) = 86400; /* XXX: can hint be 0? */

    conn->clienthello_session_ticket     = NULL;
    conn->clienthello_session_ticket_len = 0;
}

static int tls_mbedtls_ssl_ticket_write(void *p_ticket,
                                        const mbedtls_ssl_session *session,
                                        unsigned char *start,
                                        const unsigned char *end,
                                        size_t *tlen,
                                        uint32_t *lifetime)
{
    struct tls_connection *conn = (struct tls_connection *)p_ticket;
    if (conn && conn->session_ticket_cb)
    {
        /* see tls_mbedtls_clienthello_session_ticket_prep() */
        /* see tls_mbedtls_clienthello_session_ticket_set() */
        return 0;
    }

    return mbedtls_ssl_ticket_write(&conn->tls->ticket_ctx, session, start, end, tlen, lifetime);
}

static int tls_mbedtls_ssl_ticket_parse(void *p_ticket, mbedtls_ssl_session *session, unsigned char *buf, size_t len)
{
    /* XXX: TODO: not implemented in client;
     * mbedtls_ssl_conf_session_tickets_cb() callbacks only for TLS server*/

    if (len == 0)
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;

    struct tls_connection *conn = (struct tls_connection *)p_ticket;
    if (conn && conn->session_ticket_cb)
    {
        /* XXX: have random and secret been initialized yet?
         *      or must keys first be exported?
         *      EAP-FAST uses all args, EAP-TEAP only uses secret */
        struct tls_random data;
        if (tls_connection_get_random(NULL, conn, &data) != 0)
            return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        int ret = conn->session_ticket_cb(conn->session_ticket_cb_ctx, buf, len, data.client_random, data.server_random,
                                          conn->master_secret);
        if (ret == 1)
        {
            conn->resumed = 1;
            return 0;
        }
        wpa_printf(MSG_ERROR, "EAP session ticket ext not implemented");
        return MBEDTLS_ERR_SSL_INVALID_MAC;
        /*(non-zero return used for mbedtls debug logging)*/
    }

    /* XXX: TODO always use tls_mbedtls_ssl_ticket_parse() for callback? */
    int rc = mbedtls_ssl_ticket_parse(&conn->tls->ticket_ctx, session, buf, len);
    if (conn)
        conn->resumed = (rc == 0);
    return rc;
}

#endif /* TLS_MBEDTLS_SESSION_TICKETS */

#ifdef TLS_MBEDTLS_EAP_FAST
int tls_connection_get_eap_fast_key(void *tls_ctx, struct tls_connection *conn, u8 *out, size_t out_len)
{
    /* XXX: has export keys callback been run? */
    if (!conn || !conn->tls_prf_type)
        return -1;

#if MBEDTLS_VERSION_NUMBER >= 0x03000000 /* mbedtls 3.0.0 */
    conn->expkey_keyblock_size = tls_mbedtls_ssl_keyblock_size(&conn->ssl);
    if (conn->expkey_keyblock_size == 0)
        return -1;
#endif
    size_t skip            = conn->expkey_keyblock_size * 2;
    unsigned char *tmp_out = os_malloc(skip + out_len);
    if (!tmp_out)
        return -1;

    /* server_random and then client_random */
    unsigned char seed[TLS_RANDOM_LEN * 2];

    os_memcpy(seed, conn->randbytes + TLS_RANDOM_LEN, TLS_RANDOM_LEN);
    os_memcpy(seed + TLS_RANDOM_LEN, conn->randbytes, TLS_RANDOM_LEN);

#if MBEDTLS_VERSION_NUMBER >= 0x02120000 /* mbedtls 2.18.0 */
    int ret = mbedtls_ssl_tls_prf(conn->tls_prf_type, conn->master_secret, TLS_MASTER_SECRET_LEN, "key expansion", seed,
                                  sizeof(seed), tmp_out, skip + out_len);
    if (ret == 0)
        os_memcpy(out, tmp_out + skip, out_len);
#else
    int ret = -1; /*(not reached if not impl; return -1 at top of func)*/
#endif

    bin_clear_free(tmp_out, skip + out_len);
    forced_memzero(seed, sizeof(seed));
    return ret;
}

#endif /* TLS_MBEDTLS_EAP_FAST */

struct wpabuf *tls_connection_handshake(void *tls_ctx,
                                        struct tls_connection *conn,
                                        const struct wpabuf *in_data,
                                        struct wpabuf **appl_data)
{
    tls_context_t *tls = conn->tls;
    int ret            = 0;

    /* data freed by sender */
    conn->tls_io_data.out_data = NULL;
    if (wpabuf_len(in_data))
    {
        conn->tls_io_data.in_data = wpabuf_dup(in_data);
    }

#ifdef TLS_MBEDTLS_SESSION_TICKETS
    if (conn->clienthello_session_ticket)
        /*(starting handshake for EAP-FAST and EAP-TEAP)*/
        tls_mbedtls_clienthello_session_ticket_set(conn);

    /* (not thread-safe due to need to set userdata 'conn' for callback) */
    /* (unable to use mbedtls_ssl_set_user_data_p() with mbedtls 3.2.0+
     *  since ticket write and parse callbacks take (mbedtls_ssl_session *)
     *  param instead of (mbedtls_ssl_context *) param) */
    if (conn->tls_conf->flags & TLS_CONN_DISABLE_SESSION_TICKET)
        mbedtls_ssl_conf_session_tickets_cb(&conn->tls_conf->conf, NULL, NULL, NULL);
    else
        mbedtls_ssl_conf_session_tickets_cb(&conn->tls_conf->conf, tls_mbedtls_ssl_ticket_write,
                                            tls_mbedtls_ssl_ticket_parse, conn);
#endif

    /* Multiple reads */
    while (tls->ssl.MBEDTLS_PRIVATE(state) != MBEDTLS_SSL_HANDSHAKE_OVER)
    {
        ret = mbedtls_ssl_handshake_step(&tls->ssl);

        if (ret < 0)
            break;
    }

#ifdef TLS_MBEDTLS_SESSION_TICKETS
    mbedtls_ssl_conf_session_tickets_cb(&conn->tls_conf->conf, tls_mbedtls_ssl_ticket_write,
                                        tls_mbedtls_ssl_ticket_parse, NULL);
#endif

    if (ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_READ)
    {
        wpa_printf(MSG_INFO, "%s: ret is %d", __func__, ret);
        goto end;
    }

    if (!conn->tls_io_data.out_data)
    {
        wpa_printf(MSG_INFO, "application data is null, adding one byte for ack");
        u8 *dummy                  = os_zalloc(1);
        conn->tls_io_data.out_data = wpabuf_alloc_ext_data(dummy, 0);
    }

end:
    return conn->tls_io_data.out_data;
}

struct wpabuf *tls_connection_server_handshake(void *tls_ctx,
                                               struct tls_connection *conn,
                                               const struct wpabuf *in_data,
                                               struct wpabuf **appl_data)
{
    struct tls_global *global = (struct tls_global *)tls_ctx;
    tls_context_t *tls        = global->tls;
    int ret                   = 0;

    global->conn->is_server = 1;

    /* data freed by sender */
    global->conn->tls_io_data.out_data = NULL;
    if (wpabuf_len(in_data))
    {
        global->conn->tls_io_data.in_data = wpabuf_dup(in_data);
    }

#ifdef TLS_MBEDTLS_SESSION_TICKETS
    if (conn->clienthello_session_ticket)
        /*(starting handshake for EAP-FAST and EAP-TEAP)*/
        tls_mbedtls_clienthello_session_ticket_set(conn);

    /* (not thread-safe due to need to set userdata 'conn' for callback) */
    /* (unable to use mbedtls_ssl_set_user_data_p() with mbedtls 3.2.0+
     *  since ticket write and parse callbacks take (mbedtls_ssl_session *)
     *  param instead of (mbedtls_ssl_context *) param) */
    if (conn->tls_conf->flags & TLS_CONN_DISABLE_SESSION_TICKET)
        mbedtls_ssl_conf_session_tickets_cb(&conn->tls_conf->conf, NULL, NULL, NULL);
    else
        mbedtls_ssl_conf_session_tickets_cb(&conn->tls_conf->conf, tls_mbedtls_ssl_ticket_write,
                                            tls_mbedtls_ssl_ticket_parse, conn);
#endif

    /* Multiple reads */
    while (tls->ssl.MBEDTLS_PRIVATE(state) != MBEDTLS_SSL_HANDSHAKE_OVER)
    {
        ret = mbedtls_ssl_handshake_step(&tls->ssl);

        if (ret < 0)
            break;
    }

#ifdef TLS_MBEDTLS_SESSION_TICKETS
    mbedtls_ssl_conf_session_tickets_cb(&conn->tls_conf->conf, tls_mbedtls_ssl_ticket_write,
                                        tls_mbedtls_ssl_ticket_parse, NULL);
#endif

    if (ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_READ)
    {
        wpa_printf(MSG_INFO, "%s: ret is %d", __func__, ret);
        goto end;
    }

    if (!global->conn->tls_io_data.out_data)
    {
        wpa_printf(MSG_INFO, "application data is null, adding one byte for ack");
        u8 *dummy                          = os_zalloc(1);
        global->conn->tls_io_data.out_data = wpabuf_alloc_ext_data(dummy, 0);
    }

end:
    return global->conn->tls_io_data.out_data;
}

struct wpabuf *tls_connection_encrypt(void *tls_ctx, struct tls_connection *conn, const struct wpabuf *in_data)
{
    /* Reset dangling pointer */
    conn->tls_io_data.out_data = NULL;

    ssize_t ret = mbedtls_ssl_write(&conn->tls->ssl, (unsigned char *)wpabuf_head(in_data), wpabuf_len(in_data));

    if (ret < wpabuf_len(in_data))
    {
        wpa_printf(MSG_ERROR, "%s:%d, not able to write whole data %d", __func__, __LINE__, ret);
    }

    return conn->tls_io_data.out_data;
}

struct wpabuf *tls_connection_decrypt(void *tls_ctx, struct tls_connection *conn, const struct wpabuf *in_data)
{
    unsigned char buf[1500];
    int ret                   = 0;
    conn->tls_io_data.in_data = wpabuf_dup(in_data);
    ret                       = mbedtls_ssl_read(&conn->tls->ssl, buf, 1500);
    if (ret < 0)
    {
        wpa_printf(MSG_ERROR, "%s:%d, not able to write whole data %d", __func__, __LINE__, ret);
        return NULL;
    }

    struct wpabuf *out = wpabuf_alloc_copy(buf, ret);

    return out;
}

int tls_connection_resumed(void *tls_ctx, struct tls_connection *conn)
{
    if (conn && conn->tls)
    {
        mbedtls_ssl_session *session = NULL;

        // If we have a session, then its resumed
        mbedtls_ssl_get_session(&conn->tls->ssl, session);

        if (session)
        {
            return 1;
        }
    }

    return 0;
}

/* cipher array should contain cipher number in mbedtls num as per IANA
 * Please see cipherlist is u8, therefore only initial ones are supported */
int tls_connection_set_cipher_list(void *tls_ctx, struct tls_connection *conn, u8 *ciphers)
{
    int i = 0;

    while (*ciphers != 0 && i < MAX_CIPHERSUITE)
    {
        conn->tls->ciphersuite[i] = ciphers[i];
        i++;
    }
    return 0;
}

int tls_get_version(void *tls_ctx, struct tls_connection *conn, char *buf, size_t buflen)
{
    const char *name;

    if (conn == NULL)
    {
        return -1;
    }

    name = mbedtls_ssl_get_version(&conn->tls->ssl);
    if (name == NULL)
    {
        return -1;
    }

    os_strlcpy(buf, name, buflen);

    return 0;
}

#ifdef TLS_MBEDTLS_EAP_TEAP
u16 tls_connection_get_cipher_suite(struct tls_connection *conn)
{
    if (conn == NULL)
        return 0;
    return (u16)mbedtls_ssl_get_ciphersuite_id_from_ssl(&conn->tls->ssl);
}
#endif

int tls_get_library_version(char *buf, size_t buf_len)
{
    return os_snprintf(buf, buf_len, "MbedTLS build=test run=test");
}

// Lifted from https://stackoverflow.com/a/47117431
char *strremove(char *str, const char *sub)
{
    char *p, *q, *r;
    if (*sub && (q = r = os_strstr(str, sub)) != NULL)
    {
        size_t len = os_strlen(sub);
        while ((r = os_strstr(p = r + len, sub)) != NULL)
        {
            os_memmove(q, p, r - p);
            q += r - p;
        }
        os_memmove(q, p, strlen(p) + 1);
    }
    return str;
}

// Lifted from: https://stackoverflow.com/a/779960
// You must free the result if result is non-NULL.
char *str_replace(char *orig, char *rep, char *with)
{
    char *result;  // the return string
    char *ins;     // the next insert point
    char *tmp;     // varies
    int len_rep;   // length of rep (the string to remove)
    int len_with;  // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;     // number of replacements

    // sanity checks and initialization
    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count)
    {
        ins = tmp + len_rep;
    }

    tmp = result = os_zalloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--)
    {
        ins       = strstr(orig, rep);
        len_front = ins - orig;
        tmp       = strncpy(tmp, orig, len_front) + len_front;
        tmp       = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

int tls_get_cipher(void *tls_ctx, struct tls_connection *conn, char *buf, size_t buflen)
{
    const char *name;
    if (conn == NULL)
    {
        return -1;
    }

    name = mbedtls_ssl_get_ciphersuite(&conn->tls->ssl);
    if (name == NULL)
    {
        return -1;
    }

    os_strlcpy(buf, name, buflen);

    // Translate to common format for hwsim tests to pass
    strremove(buf, "TLS-");
    strremove(buf, "WITH-");
    char *tmp = str_replace(buf, "AES-", "AES");
    os_memcpy(buf, tmp, buflen);
    os_free(tmp);

    return 0;
}

int tls_connection_enable_workaround(void *tls_ctx, struct tls_connection *conn)
{
    /* (see comment in src/eap_peer/eap_fast.c:eap_fast_init()) */
    /* XXX: is there a relevant setting for this in mbed TLS? */
    /* (do we even care that much about older CBC ciphers?) */
    return 0;
}

#ifdef TLS_MBEDTLS_SESSION_TICKETS

int tls_connection_client_hello_ext(
    void *tls_ctx, struct tls_connection *conn, int ext_type, const u8 *data, size_t data_len)
{
    /* (EAP-FAST and EAP-TEAP) */
    if (ext_type == MBEDTLS_TLS_EXT_SESSION_TICKET) /*(ext_type == 35)*/
        return tls_mbedtls_clienthello_session_ticket_prep(conn, data, data_len);

    return -1;
}

#endif /* TLS_MBEDTLS_SESSION_TICKETS */

int tls_connection_get_failed(void *tls_ctx, struct tls_connection *conn)
{
    return 0;
}

int tls_connection_get_read_alerts(void *tls_ctx, struct tls_connection *conn)
{
    return 0;
}

int tls_connection_get_write_alerts(void *tls_ctx, struct tls_connection *conn)
{
    return 0;
}

void tls_connection_set_success_data(struct tls_connection *conn, struct wpabuf *data)
{
}

void tls_connection_set_success_data_resumed(struct tls_connection *conn)
{
}

const struct wpabuf *tls_connection_get_success_data(struct tls_connection *conn)
{
    return NULL;
}

void tls_connection_remove_session(struct tls_connection *conn)
{
}

#ifdef TLS_MBEDTLS_EAP_TEAP
int tls_get_tls_unique(struct tls_connection *conn, u8 *buf, size_t max_len)
{
#if defined(MBEDTLS_SSL_RENEGOTIATION) /* XXX: renegotiation or resumption? */
    /* data from TLS handshake Finished message */
    size_t verify_len = conn->tls->ssl.MBEDTLS_PRIVATE(verify_data_len);
    char *verify_data = (conn->is_server ^ conn->resumed) ? conn->tls->ssl.MBEDTLS_PRIVATE(peer_verify_data) :
                                                            conn->tls->ssl.MBEDTLS_PRIVATE(own_verify_data);
    if (verify_len && verify_len <= max_len)
    {
        os_memcpy(buf, verify_data, verify_len);
        return (int)verify_len;
    }
#endif
    return -1;
}
#endif

static void tls_mbedtls_set_peer_subject(struct tls_connection *conn, const mbedtls_x509_crt *crt)
{
    if (conn->peer_subject)
        return;
    char buf[MBEDTLS_X509_MAX_DN_NAME_SIZE * 2];
    int buflen = mbedtls_x509_dn_gets(buf, sizeof(buf), &crt->subject);
    if (buflen >= 0 && (conn->peer_subject = os_malloc((size_t)buflen + 1)))
        os_memcpy(conn->peer_subject, buf, (size_t)buflen + 1);
}

#ifdef TLS_MBEDTLS_EAP_TEAP
const char *tls_connection_get_peer_subject(struct tls_connection *conn)
{
    if (!conn)
        return NULL;
    if (!conn->peer_subject)
    { /*(if not set during cert verify)*/
        const mbedtls_x509_crt *peer_cert = mbedtls_ssl_get_peer_cert(&conn->ssl);
        if (peer_cert)
            tls_mbedtls_set_peer_subject(conn, peer_cert);
    }
    return conn->peer_subject;
}
#endif

#ifdef TLS_MBEDTLS_EAP_TEAP
bool tls_connection_get_own_cert_used(struct tls_connection *conn)
{
    /* XXX: availability of cert does not necessary mean that client
     * received certificate request from server and then sent cert.
     * ? step handshake in tls_connection_handshake() looking for
     *   MBEDTLS_SSL_CERTIFICATE_REQUEST ? */
    return (conn->tls->has_client_cert && conn->tls->has_private_key);
}
#endif

char *tls_connection_peer_serial_num(void *tls_ctx, struct tls_connection *conn)
{
    return NULL;
}

int tls_connection_set_params(void *tls_ctx, struct tls_connection *conn, const struct tls_connection_params *params)
{
    int ret = 0;

    wpa_printf(MSG_INFO, " client_cert is %s, %p", params->client_cert, params);

    tls_context_t *tls = (tls_context_t *)os_zalloc(sizeof(tls_context_t));

    if (!tls)
    {
        wpa_printf(MSG_ERROR, "failed to allocate tls context");
        return -1;
    }
    if (!params)
    {
        wpa_printf(MSG_ERROR, "configuration is null");
        ret = -1;
        goto err;
    }

    ret = tls_create_mbedtls_handle(params, tls, 0);
    if (ret < 0)
    {
        wpa_printf(MSG_ERROR, "failed to create ssl handle");
        goto err;
    }
    mbedtls_ssl_set_bio(&tls->ssl, conn, tls_mbedtls_write, tls_mbedtls_read, NULL);
    conn->tls = (tls_context_t *)tls;

    mbedtls_ssl_conf_export_keys_ext_cb(&conn->tls->conf, tls_connection_export_keys_cb, conn);

    return ret;
err:
    os_free(tls);
    return ret;
}

int tls_global_set_params(void *tls_ctx, const struct tls_connection_params *params)
{
    int ret                   = 0;
    struct tls_global *global = tls_ctx;

    if (params->check_cert_subject)
        return -1; /* not yet supported */

    /* Currently, global parameters are only set when running in server
     * mode. */
    global->server = 1;

    wpa_printf(MSG_INFO, " server_cert is %s, %p", params->client_cert, params);

    tls_context_t *tls = (tls_context_t *)os_zalloc(sizeof(tls_context_t));

    if (!tls)
    {
        wpa_printf(MSG_ERROR, "failed to allocate tls context");
        return -1;
    }
    if (!params)
    {
        wpa_printf(MSG_ERROR, "configuration is null");
        ret = -1;
        goto err;
    }

    ret = tls_create_mbedtls_handle(params, tls, 1);
    if (ret < 0)
    {
        wpa_printf(MSG_ERROR, "failed to create ssl handle");
        goto err;
    }

    mbedtls_ssl_set_bio(&tls->ssl, global, tls_mbedtls_server_write, tls_mbedtls_server_read, NULL);
    global->tls = (tls_context_t *)tls;

#ifdef MBEDTLS_SSL_SESSION_TICKETS
#ifdef MBEDTLS_SSL_TICKET_C
    if (!(params->flags & TLS_CONN_DISABLE_SESSION_TICKET))

#ifdef TLS_MBEDTLS_SESSION_TICKETS
        mbedtls_ssl_conf_session_tickets_cb(&tls->conf, tls_mbedtls_ssl_ticket_write, tls_mbedtls_ssl_ticket_parse,
                                            NULL);
#else
        mbedtls_ssl_conf_session_tickets_cb(&tls->conf, mbedtls_ssl_ticket_write, mbedtls_ssl_ticket_parse,
                                            &tls->ticket_ctx);
#endif
#endif
#endif

    return ret;
err:
    os_free(tls);
    return ret;
}

int tls_connection_set_session_ticket_cb(void *tls_ctx,
                                         struct tls_connection *conn,
                                         tls_session_ticket_cb cb,
                                         void *ctx)
{
#ifdef TLS_MBEDTLS_SESSION_TICKETS
    if (!(conn->tls_conf->flags & TLS_CONN_DISABLE_SESSION_TICKET))
    {
        /* (EAP-FAST and EAP-TEAP) */
        conn->session_ticket_cb     = cb;
        conn->session_ticket_cb_ctx = ctx;
        return 0;
    }
#endif
    return -1;
}

int tls_connection_export_keys_cb(void *p_expkey,
                                  const unsigned char *secret,
                                  const unsigned char *kb,
                                  size_t maclen,
                                  size_t keylen,
                                  size_t ivlen,
                                  const unsigned char client_random[32],
                                  const unsigned char server_random[32],
                                  mbedtls_tls_prf_types tls_prf_type)
{
    struct tls_connection *conn = p_expkey;

    conn->expkey_keyblock_size = maclen + keylen + ivlen;

    os_memcpy(conn->randbytes, client_random, TLS_RANDOM_LEN);
    os_memcpy(conn->randbytes + TLS_RANDOM_LEN, server_random, TLS_RANDOM_LEN);
    os_memcpy(conn->master_secret, secret, TLS_MASTER_SECRET_LEN);
    conn->tls_prf_type = tls_prf_type;
    return 0;
}

static int tls_connection_prf(
    void *tls_ctx, struct tls_connection *conn, const char *label, int server_random_first, u8 *out, size_t out_len)
{
    int ret = 0;
    u8 seed[2 * TLS_RANDOM_LEN];
    mbedtls_ssl_context *ssl = &conn->tls->ssl;

    if (!ssl || !conn)
    {
        wpa_printf(MSG_ERROR, "TLS: %s, connection  info is null", __func__);
        return -1;
    }
    if (ssl->MBEDTLS_PRIVATE(state) != MBEDTLS_SSL_HANDSHAKE_OVER)
    {
        wpa_printf(MSG_ERROR, "TLS: %s, incorrect tls state=%d", __func__, ssl->MBEDTLS_PRIVATE(state));
        return -1;
    }

    if (server_random_first)
    {
        os_memcpy(seed, conn->randbytes + TLS_RANDOM_LEN, TLS_RANDOM_LEN);
        os_memcpy(seed + TLS_RANDOM_LEN, conn->randbytes, TLS_RANDOM_LEN);
    }
    else
    {
        os_memcpy(seed, conn->randbytes, 2 * TLS_RANDOM_LEN);
    }

    wpa_hexdump_key(MSG_MSGDUMP, "random", seed, 2 * TLS_RANDOM_LEN);
    wpa_hexdump_key(MSG_MSGDUMP, "master", conn->master_secret, TLS_MASTER_SECRET_LEN);

    if (conn->tls_prf_type == MBEDTLS_SSL_TLS_PRF_SHA384)
    {
        ret = tls_prf_sha384(conn->master_secret, TLS_MASTER_SECRET_LEN, label, seed, 2 * TLS_RANDOM_LEN, out, out_len);
    }
    else if (conn->tls_prf_type == MBEDTLS_SSL_TLS_PRF_SHA256)
    {
        ret = tls_prf_sha256(conn->master_secret, TLS_MASTER_SECRET_LEN, label, seed, 2 * TLS_RANDOM_LEN, out, out_len);
    }
    else
    {
        ret =
            tls_prf_sha1_md5(conn->master_secret, TLS_MASTER_SECRET_LEN, label, seed, 2 * TLS_RANDOM_LEN, out, out_len);
    }

    if (ret < 0)
    {
        wpa_printf(MSG_ERROR, "prf failed, ret=%d\n", ret);
    }
    wpa_hexdump_key(MSG_MSGDUMP, "key", out, out_len);

    return ret;
}

int tls_connection_export_key(void *tls_ctx,
                              struct tls_connection *conn,
                              const char *label,
                              const u8 *context,
                              size_t context_len,
                              u8 *out,
                              size_t out_len)
{
    return tls_connection_prf(tls_ctx, conn, label, 0, out, out_len);
}

int tls_connection_shutdown(void *tls_ctx, struct tls_connection *conn)
{
    struct tls_global *global = tls_ctx;

    conn->resumed = 0;

    if (conn->tls_io_data.in_data)
    {
        wpabuf_free(conn->tls_io_data.in_data);
    }
    conn->tls_io_data.in_data = NULL;

    /* outdata may have dangling pointer */
    conn->tls_io_data.out_data = NULL;

    return mbedtls_ssl_session_reset(&conn->tls->ssl);
}

int tls_connection_get_random(void *tls_ctx, struct tls_connection *conn, struct tls_random *data)
{
    mbedtls_ssl_context *ssl = &conn->tls->ssl;

    os_memset(data, 0, sizeof(*data));
    if (ssl->MBEDTLS_PRIVATE(state) == MBEDTLS_SSL_CLIENT_HELLO)
    {
        return -1;
    }

    data->client_random     = conn->randbytes;
    data->client_random_len = TLS_RANDOM_LEN;

    if (ssl->MBEDTLS_PRIVATE(state) != MBEDTLS_SSL_SERVER_HELLO)
    {
        data->server_random     = conn->randbytes + TLS_RANDOM_LEN;
        data->server_random_len = TLS_RANDOM_LEN;
    }

    return 0;
}
