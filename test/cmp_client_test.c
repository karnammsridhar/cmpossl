/*
 * Copyright 2007-2019 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright Nokia 2007-2019
 * Copyright Siemens AG 2015-2019
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "cmp_testlib.h"

#include "apps/cmp_mock_srv.h"

#ifndef NDEBUG /* tests need mock server, which is available only if !NDEBUG */

static const char *server_key_f;
static const char *server_cert_f;
static const char *client_key_f;
static const char *client_cert_f;
static const char *pkcs10_f;

typedef struct test_fixture {
    const char *test_case_name;
    OSSL_CMP_CTX *cmp_ctx;
    OSSL_CMP_SRV_CTX *srv_ctx;
    int expected;
    X509 *(*exec_cert_ses_cb) (OSSL_CMP_CTX *);
    STACK_OF(X509) *caPubs;
} CMP_SES_TEST_FIXTURE;

static EVP_PKEY *server_key = NULL;
static X509 *server_cert = NULL;
static EVP_PKEY *client_key = NULL;
static X509 *client_cert = NULL;
static unsigned char ref[CMP_TEST_REFVALUE_LENGTH];

/*
 * For these unit tests, the client abandons message protection, and for
 * error messages the mock server does so as well.
 * Message protection and verification is tested in cmp_lib_test.c
 */

static void tear_down(CMP_SES_TEST_FIXTURE *fixture)
{
    OSSL_CMP_CTX_free(fixture->cmp_ctx);
    ossl_cmp_mock_srv_free(fixture->srv_ctx);
    sk_X509_free(fixture->caPubs);
    OPENSSL_free(fixture);
}

static CMP_SES_TEST_FIXTURE *set_up(const char *const test_case_name)
{
    CMP_SES_TEST_FIXTURE *fixture;
    OSSL_CMP_CTX *srv_cmp_ctx = NULL;
    OSSL_CMP_CTX *ctx = NULL; /* for client */

    if (!TEST_ptr(fixture = OPENSSL_zalloc(sizeof(*fixture))))
        return NULL;
    fixture->test_case_name = test_case_name;
    if (!TEST_ptr(fixture->srv_ctx = ossl_cmp_mock_srv_new())
            || !OSSL_CMP_SRV_CTX_set_accept_unprotected(fixture->srv_ctx, 1)
            || !ossl_cmp_mock_srv_set1_certOut(fixture->srv_ctx, client_cert)
            || (srv_cmp_ctx =
                OSSL_CMP_SRV_CTX_get0_cmp_ctx(fixture->srv_ctx)) == NULL
            || !OSSL_CMP_CTX_set1_clCert(srv_cmp_ctx, server_cert)
            || !OSSL_CMP_CTX_set1_pkey(srv_cmp_ctx, server_key))
        goto err;
    if (!TEST_ptr(fixture->cmp_ctx = ctx = OSSL_CMP_CTX_new())
            || !OSSL_CMP_CTX_set_log_cb(fixture->cmp_ctx, print_to_bio_out)
            || !OSSL_CMP_CTX_set_transfer_cb(ctx, OSSL_CMP_CTX_server_perform)
            || !OSSL_CMP_CTX_set_transfer_cb_arg(ctx, fixture->srv_ctx)
            || !OSSL_CMP_CTX_set_option(ctx, OSSL_CMP_OPT_UNPROTECTED_SEND, 1)
            || !OSSL_CMP_CTX_set_option(ctx, OSSL_CMP_OPT_UNPROTECTED_ERRORS, 1)
            || !OSSL_CMP_CTX_set1_oldCert(ctx, client_cert)
            || !OSSL_CMP_CTX_set1_pkey(ctx, client_key)
            || !OSSL_CMP_CTX_set1_srvCert(ctx, server_cert)
            || !OSSL_CMP_CTX_set1_referenceValue(ctx, ref, sizeof(ref)))
        goto err;
    fixture->exec_cert_ses_cb = NULL;
    return fixture;

 err:
    tear_down(fixture);
    return NULL;
}

static int execute_exec_RR_ses_test(CMP_SES_TEST_FIXTURE *fixture)
{
    return TEST_int_eq(fixture->expected,
                       OSSL_CMP_exec_RR_ses(fixture->cmp_ctx) != NULL);
}

static int execute_exec_GENM_ses_test(CMP_SES_TEST_FIXTURE *fixture)
{
    STACK_OF(OSSL_CMP_ITAV) *itavs = NULL;
    if (!TEST_ptr(itavs = OSSL_CMP_exec_GENM_ses(fixture->cmp_ctx)))
        return 0;
    sk_OSSL_CMP_ITAV_pop_free(itavs, OSSL_CMP_ITAV_free);
    /* TODO: check if the returned value is the expected one (same as sent) */
    return 1;
}

static int execute_exec_certrequest_ses_test(CMP_SES_TEST_FIXTURE *fixture)
{
    X509 *res;

    if (fixture->expected == 0)
        return TEST_ptr_null(fixture->exec_cert_ses_cb(fixture->cmp_ctx));

    if (!TEST_ptr(res = fixture->exec_cert_ses_cb(fixture->cmp_ctx))
            || !TEST_int_eq(X509_cmp(res, client_cert), 0))
        return 0;
    /* TODO: check that cerfConf has been exchanged unless implicitConfirm */
    if (fixture->caPubs != NULL) {
        STACK_OF(X509) *caPubs = OSSL_CMP_CTX_get1_caPubs(fixture->cmp_ctx);
        int ret = TEST_int_eq(STACK_OF_X509_cmp(fixture->caPubs, caPubs), 0);

        sk_X509_pop_free(caPubs, X509_free);
        return ret;
    }
    return 1;
}

static int test_exec_RR_ses(void)
{
    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->expected = 1;
    EXECUTE_TEST(execute_exec_RR_ses_test, tear_down);
    return result;
}

static int test_exec_RR_ses_receive_error(void)
{
    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    ossl_cmp_mock_srv_set_statusInfo(fixture->srv_ctx,
                                     OSSL_CMP_PKISTATUS_rejection,
                                     OSSL_CMP_CTX_FAILINFO_signerNotTrusted,
                                     "test string");
    ossl_cmp_mock_srv_set_send_error(fixture->srv_ctx, 1);
    fixture->expected = 0;
    EXECUTE_TEST(execute_exec_RR_ses_test, tear_down);
    return result;
}

static int test_exec_IR_ses(void)
{
    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->exec_cert_ses_cb = OSSL_CMP_exec_IR_ses;
    fixture->expected = 1;
    fixture->caPubs = sk_X509_new_null();
    sk_X509_push(fixture->caPubs, server_cert);
    sk_X509_push(fixture->caPubs, server_cert);
    ossl_cmp_mock_srv_set1_caPubsOut(fixture->srv_ctx, fixture->caPubs);
    EXECUTE_TEST(execute_exec_certrequest_ses_test, tear_down);
    return result;
}

static int test_exec_IR_ses_poll(void)
{
    const int pollCount = 2;
    const int checkAfter = 1;

    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->exec_cert_ses_cb = OSSL_CMP_exec_IR_ses;
    fixture->expected = 1;
    ossl_cmp_mock_srv_set_pollCount(fixture->srv_ctx, pollCount);
    ossl_cmp_mock_srv_set_checkAfterTime(fixture->srv_ctx, checkAfter);
    EXECUTE_TEST(execute_exec_certrequest_ses_test, tear_down);
    /* TODO: check that 2 rounds are done or session takes 2..3 seconds */
    return result;
}

static int test_exec_IR_ses_poll_timeout(void)
{
    const int pollCount = 3;
    const int checkAfter = 1;
    const int tout = pollCount * checkAfter;

    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->exec_cert_ses_cb = OSSL_CMP_exec_IR_ses;
    fixture->expected = 0;
    ossl_cmp_mock_srv_set_pollCount(fixture->srv_ctx, pollCount + 1);
    ossl_cmp_mock_srv_set_checkAfterTime(fixture->srv_ctx, checkAfter);
    OSSL_CMP_CTX_set_option(fixture->cmp_ctx, OSSL_CMP_OPT_TOTALTIMEOUT, tout);
    EXECUTE_TEST(execute_exec_certrequest_ses_test, tear_down);
    return result;
}


static int test_exec_CR_ses(void)
{
    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->exec_cert_ses_cb = OSSL_CMP_exec_CR_ses;
    fixture->expected = 1;
    EXECUTE_TEST(execute_exec_certrequest_ses_test, tear_down);
    return result;
}

static int test_exec_CR_ses_implicit_confirm(void)
{
    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->exec_cert_ses_cb = OSSL_CMP_exec_CR_ses;
    fixture->expected = 1;
    OSSL_CMP_CTX_set_option(fixture->cmp_ctx,
                            OSSL_CMP_OPT_IMPLICITCONFIRM, 1);
    OSSL_CMP_SRV_CTX_set_grant_implicit_confirm(fixture->srv_ctx, 1);
    EXECUTE_TEST(execute_exec_certrequest_ses_test, tear_down);
    return result;
}

static int test_exec_KUR_ses(void)
{
    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->exec_cert_ses_cb = OSSL_CMP_exec_KUR_ses;
    fixture->expected = 1;
    EXECUTE_TEST(execute_exec_certrequest_ses_test, tear_down);
    return result;
}

static int test_exec_P10CR_ses(void)
{
    X509_REQ *req = NULL;

    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->exec_cert_ses_cb = OSSL_CMP_exec_P10CR_ses;
    fixture->expected = 1;
    if (!TEST_ptr(req = load_csr(pkcs10_f))
            || !TEST_true(OSSL_CMP_CTX_set1_p10CSR(fixture->cmp_ctx, req))) {
        tear_down(fixture);
        fixture = NULL;
    }
    X509_REQ_free(req);
    EXECUTE_TEST(execute_exec_certrequest_ses_test, tear_down);
    return result;
}

static int test_exec_GENM_ses(void)
{
    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    EXECUTE_TEST(execute_exec_GENM_ses_test, tear_down);
    return result;
}

static int execute_exchange_certConf_test(CMP_SES_TEST_FIXTURE *fixture)
{
    int res =
        ossl_cmp_exchange_certConf(fixture->cmp_ctx,
                                   OSSL_CMP_PKIFAILUREINFO_addInfoNotAvailable,
                                   "abcdefg");
    return TEST_int_eq(fixture->expected, res);
}

static int execute_exchange_error_test(CMP_SES_TEST_FIXTURE *fixture)
{
    int res =
        ossl_cmp_exchange_error(fixture->cmp_ctx,
                                OSSL_CMP_PKISTATUS_rejection,
                                1 << OSSL_CMP_PKIFAILUREINFO_unsupportedVersion,
                                "foo_status", 999, "foo_details");

    return TEST_int_eq(fixture->expected, res);
}

static int test_exchange_certConf(void)
{
    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->expected = 0; /* client should not send certConf immediately */
    if (!ossl_cmp_ctx_set0_newCert(fixture->cmp_ctx, X509_dup(client_cert))) {
        tear_down(fixture);
        fixture = NULL;
    }
    EXECUTE_TEST(execute_exchange_certConf_test, tear_down);
    return result;
}

static int test_exchange_error(void)
{
    SETUP_TEST_FIXTURE(CMP_SES_TEST_FIXTURE, set_up);
    fixture->expected = 1; /* client may send error any time */
    EXECUTE_TEST(execute_exchange_error_test, tear_down);
    return result;
}

void cleanup_tests(void)
{
    X509_free(server_cert);
    EVP_PKEY_free(server_key);
    X509_free(client_cert);
    EVP_PKEY_free(client_key);
    return;
}

int setup_tests(void)
{
    if (!test_skip_common_options()) {
        TEST_error("Error parsing test options\n");
        return 0;
    }

    if (!TEST_ptr(server_key_f = test_get_argument(0))
            || !TEST_ptr(server_cert_f = test_get_argument(1))
            || !TEST_ptr(client_key_f = test_get_argument(2))
            || !TEST_ptr(client_cert_f = test_get_argument(3))
            || !TEST_ptr(pkcs10_f = test_get_argument(4))) {
        TEST_error("usage: cmp_client_test server.key server.crt client.key client.crt client.csr\n");
        return 0;
    }

    if (!TEST_ptr(server_key = load_pem_key(server_key_f))
            || !TEST_ptr(server_cert = load_pem_cert(server_cert_f))
            || !TEST_ptr(client_key = load_pem_key(client_key_f))
            || !TEST_ptr(client_cert = load_pem_cert(client_cert_f))
            || !TEST_int_eq(1, RAND_bytes(ref, sizeof(ref)))) {
        cleanup_tests();
        return 0;
    }

    ADD_TEST(test_exec_RR_ses);
    ADD_TEST(test_exec_RR_ses_receive_error);
    ADD_TEST(test_exec_CR_ses);
    ADD_TEST(test_exec_CR_ses_implicit_confirm);
    ADD_TEST(test_exec_IR_ses);
    ADD_TEST(test_exec_IR_ses_poll);
    ADD_TEST(test_exec_IR_ses_poll_timeout);
    ADD_TEST(test_exec_KUR_ses);
    ADD_TEST(test_exec_P10CR_ses);
    ADD_TEST(test_exec_GENM_ses);
    ADD_TEST(test_exchange_certConf);
    ADD_TEST(test_exchange_error);
    return 1;
}

#else /* !defined (NDEBUG) */

int setup_tests(void)
{
    TEST_note("CMP session tests are disabled in this build (NDEBUG).");
    return 1;
}

#endif
