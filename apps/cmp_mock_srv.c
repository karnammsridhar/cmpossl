/*
 * Copyright 2018-2020 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright Siemens AG 2018-2020
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or atf
 * https://www.openssl.org/source/license.html
 */

#ifndef OPENSSL_NO_CMP

# include <openssl/cmp.h>
# include <openssl/err.h>
# include <openssl/cmperr.h>
# include "cmp_mock_srv.h"

/* the context for the CMP mock server */
typedef struct
{
    X509 *certOut;              /* Certificate to be returned in cp/ip/kup */
    STACK_OF(X509) *chainOut;   /* Cert chain useful to validate certOut */
    STACK_OF(X509) *caPubsOut;  /* caPubs for ip */
    OSSL_CMP_PKISI *pkiStatusOut; /* to return in ip/cp/kup/rp unless polling */
    int sendError;              /* Always send error if true */

    OSSL_CMP_MSG *certReq;      /* ir/cr/p10cr/kur saved for polling */
    int certReqId;              /* id of last ir/cr/p10cr/kur, for polling */
    int pollCount;              /* Number of polls before cert response */
    int checkAfterTime;         /* time to wait for the next poll in seconds */
} mock_srv_ctx;


static void mock_srv_ctx_free(mock_srv_ctx *ctx)
{
    if (ctx == NULL)
        return;

    OSSL_CMP_PKISI_free(ctx->pkiStatusOut);
    X509_free(ctx->certOut);
    sk_X509_pop_free(ctx->chainOut, X509_free);
    sk_X509_pop_free(ctx->caPubsOut, X509_free);
    OSSL_CMP_MSG_free(ctx->certReq);
    OPENSSL_free(ctx);
}

static mock_srv_ctx *ctx_new(void)
{
    mock_srv_ctx *ctx = OPENSSL_zalloc(sizeof(mock_srv_ctx));

    if (ctx == NULL)
        goto err;

    if ((ctx->pkiStatusOut = OSSL_CMP_PKISI_new()) == NULL)
        goto err;

    ctx->certReqId = -1;

    /* all other elements are initialized to 0 or NULL, respectively */
    return ctx;
 err:
    mock_srv_ctx_free(ctx);
    return NULL;
}

int ossl_cmp_mock_srv_set1_certOut(OSSL_CMP_SRV_CTX *srv_ctx, X509 *cert)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    X509_free(ctx->certOut);
    if (X509_up_ref(cert)) {
        ctx->certOut = cert;
        return 1;
    }
    ctx->certOut = NULL;
    return 0;
}

int ossl_cmp_mock_srv_set1_chainOut(OSSL_CMP_SRV_CTX *srv_ctx,
                                    STACK_OF(X509) *chain)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL || chain == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    sk_X509_pop_free(ctx->chainOut, X509_free);
    return (ctx->chainOut = X509_chain_up_ref(chain)) != NULL;
}

int ossl_cmp_mock_srv_set1_caPubsOut(OSSL_CMP_SRV_CTX *srv_ctx,
                                     STACK_OF(X509) *caPubs)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL || caPubs == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    sk_X509_pop_free(ctx->caPubsOut, X509_free);
    return (ctx->caPubsOut = X509_chain_up_ref(caPubs)) != NULL;
}

int ossl_cmp_mock_srv_set_statusInfo(OSSL_CMP_SRV_CTX *srv_ctx, int status,
                                     int fail_info, const char *text)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    OSSL_CMP_PKISI_free(ctx->pkiStatusOut);
    return (ctx->pkiStatusOut =
            OSSL_CMP_STATUSINFO_new(status, fail_info, text)) != NULL;
}

int ossl_cmp_mock_srv_set_send_error(OSSL_CMP_SRV_CTX *srv_ctx, int val)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    ctx->sendError = val != 0;
    return 1;
}

int ossl_cmp_mock_srv_set_pollCount(OSSL_CMP_SRV_CTX *srv_ctx, int count)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    if (count < 0) {
        CMPerr(0, CMP_R_INVALID_ARGS);
        return 0;
    }
    ctx->pollCount = count;
    return 1;
}

int ossl_cmp_mock_srv_set_checkAfterTime(OSSL_CMP_SRV_CTX *srv_ctx, int sec)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    ctx->checkAfterTime = sec;
    return 1;
}

static OSSL_CMP_PKISI *process_cert_request(OSSL_CMP_SRV_CTX *srv_ctx,
                                            const OSSL_CMP_MSG *cert_req,
                                            int certReqId,
                                            X509 **certOut,
                                            STACK_OF(X509) **chainOut,
                                            STACK_OF(X509) **caPubs)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL || cert_req == NULL
            || certOut == NULL || chainOut == NULL || caPubs == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    if (ctx->sendError) {
        CMPerr(0, CMP_R_ERROR_PROCESSING_MSG);
        return 0;
    }

    *certOut = NULL;
    *chainOut = NULL;
    *caPubs = NULL;
    ctx->certReqId = certReqId;
    if (ctx->pollCount > 0) {
        ctx->pollCount--;
        OSSL_CMP_MSG_free(ctx->certReq);
        if ((ctx->certReq = OSSL_CMP_MSG_dup(cert_req)) == NULL)
            return NULL;
        if (!X509_dup(ctx->certOut))
            return NULL;
        return OSSL_CMP_STATUSINFO_new(OSSL_CMP_PKISTATUS_waiting, 0, NULL);
    } else {
        if ((*certOut = X509_dup(ctx->certOut)) == NULL)
            return NULL;
        if (ctx->chainOut != NULL
                && (*chainOut = X509_chain_up_ref(ctx->chainOut)) == NULL)
            return NULL;
        if (ctx->caPubsOut != NULL
            && (*caPubs = X509_chain_up_ref(ctx->caPubsOut)) == NULL)
            return NULL;
        return OSSL_CMP_PKISI_dup(ctx->pkiStatusOut);
    }
}

static OSSL_CMP_PKISI *process_rr(OSSL_CMP_SRV_CTX *srv_ctx,
                                  const OSSL_CMP_MSG *rr,
                                  X509_NAME *issuer, ASN1_INTEGER *serial)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL || rr == NULL || issuer == NULL || serial == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    if (ctx->sendError) {
        CMPerr(0, CMP_R_ERROR_PROCESSING_MSG);
        return 0;
    }

    /* accept revocation only for the certificate we sent in ir/cr/kur */
    if (X509_NAME_cmp(issuer, X509_get_issuer_name(ctx->certOut)) != 0
            || ASN1_INTEGER_cmp(serial,
                                X509_get0_serialNumber(ctx->certOut)) != 0)
        return NULL;
    return OSSL_CMP_PKISI_dup(ctx->pkiStatusOut);
}

static int process_genm(OSSL_CMP_SRV_CTX *srv_ctx,
                        const OSSL_CMP_MSG *genm,
                        STACK_OF(OSSL_CMP_ITAV) *in,
                        STACK_OF(OSSL_CMP_ITAV) **out)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL || genm == NULL || in == NULL || out == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    if (ctx->sendError) {
        CMPerr(0, CMP_R_ERROR_PROCESSING_MSG);
        return 0;
    }

    *out = in;
    return 1;
}

static void process_error(OSSL_CMP_SRV_CTX *srv_ctx, const OSSL_CMP_MSG *error,
                          OSSL_CMP_PKISI *statusInfo, ASN1_INTEGER *errorCode,
                          OSSL_CMP_PKIFREETEXT *errorDetails)
{
    BIO *bio = NULL;
    char *buf = NULL;
    char *sibuf;
    int i;
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL || error == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return;
    }

    bio = BIO_new_fp(stderr, BIO_NOCLOSE);
    if (bio == NULL)
        goto err;

    BIO_printf(bio, "got error:\n");

    if (statusInfo == NULL) {
        BIO_printf(bio, "pkiStatusInfo: absent\n");
    } else {
        buf = OPENSSL_malloc(OSSL_CMP_PKISI_BUFLEN);
        if (buf == NULL)
            goto err;

        sibuf = OSSL_CMP_snprint_PKIStatusInfo(statusInfo, buf,
                                               OSSL_CMP_PKISI_BUFLEN);

        BIO_printf(bio, "pkiStatusInfo: %s\n",
                   sibuf != NULL ? sibuf: "<invalid>");
    }

    if (errorCode == NULL)
        BIO_printf(bio, "errorCode: absent\n");
    else
        BIO_printf(bio, "errorCode: %ld\n", ASN1_INTEGER_get(errorCode));

    if (sk_ASN1_UTF8STRING_num(errorDetails) <= 0)
        BIO_printf(bio, "errorDetails: absent\n");
    else {
        BIO_printf(bio, "errorDetails:\n");
        for (i = 0; i < sk_ASN1_UTF8STRING_num(errorDetails); i++) {
            ASN1_STRING_print(bio, sk_ASN1_UTF8STRING_value(errorDetails, i));
            BIO_printf(bio, "\n");
        }
    }

 err:
    OPENSSL_free(buf);
    BIO_free(bio);
}

static int process_certConf(OSSL_CMP_SRV_CTX *srv_ctx,
                            const OSSL_CMP_MSG *certConf,
                            int certReqId, ASN1_OCTET_STRING *certHash)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);
    ASN1_OCTET_STRING *digest;

    if (ctx == NULL || certConf == NULL || certHash == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    if (ctx->sendError) {
        CMPerr(0, CMP_R_ERROR_PROCESSING_MSG);
        return 0;
    }

    if (certReqId != ctx->certReqId) {
        /* in case of error, invalid reqId -1 */
        CMPerr(0, CMP_R_UNEXPECTED_REQUEST_ID);
        return 0;
    }

    if ((digest = OSSL_CMP_X509_digest(ctx->certOut)) == NULL)
        return 0;
    if (ASN1_OCTET_STRING_cmp(certHash, digest) != 0) {
        ASN1_OCTET_STRING_free(digest);
        CMPerr(0, CMP_R_WRONG_CERT_HASH);
        return 0;
    }
    ASN1_OCTET_STRING_free(digest);
    return 1;
}

static int process_pollReq(OSSL_CMP_SRV_CTX *srv_ctx,
                           const OSSL_CMP_MSG *pollReq, int certReqId,
                           OSSL_CMP_MSG **certReq, int64_t *check_after)
{
    mock_srv_ctx *ctx = OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx);

    if (ctx == NULL || pollReq == NULL
            || certReq == NULL || check_after == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    if (ctx->sendError || ctx->certReq == NULL) {
        CMPerr(0, CMP_R_ERROR_PROCESSING_MSG);
        return 0;
    }

    if (ctx->pollCount == 0) {
        *certReq = ctx->certReq;
    } else {
        ctx->pollCount--;
        *certReq = NULL;
        *check_after = ctx->checkAfterTime;
    }
    return 1;
}

OSSL_CMP_SRV_CTX *ossl_cmp_mock_srv_new(void)
{
    OSSL_CMP_SRV_CTX *srv_ctx = OSSL_CMP_SRV_CTX_new();
    mock_srv_ctx *ctx = ctx_new();

    if (srv_ctx != NULL && ctx != NULL
            && OSSL_CMP_SRV_CTX_init(srv_ctx, ctx, process_cert_request,
                                     process_rr, process_genm, process_error,
                                     process_certConf, process_pollReq))
        return srv_ctx;

    mock_srv_ctx_free(ctx);
    OSSL_CMP_SRV_CTX_free(srv_ctx);
    return NULL;
}

void ossl_cmp_mock_srv_free(OSSL_CMP_SRV_CTX *srv_ctx)
{
    if (srv_ctx != NULL)
        mock_srv_ctx_free(OSSL_CMP_SRV_CTX_get0_custom_ctx(srv_ctx));
    OSSL_CMP_SRV_CTX_free(srv_ctx);
}


#endif /* !defined OPENSSL_NO_CMP */
