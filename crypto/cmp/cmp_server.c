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

#include <openssl/asn1t.h>

#include "cmp_local.h"

/* explicit #includes not strictly needed since implied by the above: */
#include <openssl/cmp.h>
#include <openssl/err.h>

typedef OSSL_CMP_MSG *(*cmp_srv_process_cb_t)
                      (OSSL_CMP_SRV_CTX *ctx, OSSL_CMP_MSG *msg);

/*
 * this structure is used to store the context for the CMP mock server
 */
struct OSSL_cmp_srv_ctx_st
{
    OSSL_CMP_CTX *ctx;          /* Client CMP context, partly reused for srv */

    OSSL_CMP_PKISI *pkiStatusOut; /* PKIStatusInfo to be returned */
    X509 *certOut;              /* Certificate to be returned in cp/ip/kup */
    STACK_OF(X509) *chainOut;   /* Cert chain useful to validate certOut */
    STACK_OF(X509) *caPubsOut;  /* caPubs for ip */

    OSSL_CMP_MSG *certReq;      /* ir/cr/p10cr/kur saved in case of polling */
    int certReqId;              /* id saved in case of polling */
    unsigned int pollCount;     /* Number of polls before cert response */
    int64_t checkAfterTime;     /* time to wait for the next poll in seconds */

    int grantImplicitConfirm;   /* Grant implicit confirmation if requested */
    int sendError;              /* Always send error if true */
    int sendUnprotectedErrors;  /* Send error and rejection msgs unprotected */
    int acceptUnprotectedRequests; /* Accept requests with no/invalid prot. */
    int acceptRAVerified;       /* Accept ir/cr/kur with POPO RAVerified */
    int encryptcert;            /* Encrypt certs in cert response message */

    /* callbacks for message processing */
    cmp_srv_process_cb_t process_ir_cb;
    cmp_srv_process_cb_t process_cr_cb;
    cmp_srv_process_cb_t process_p10cr_cb;
    cmp_srv_process_cb_t process_kur_cb;
    cmp_srv_process_cb_t process_pollreq_cb;
    cmp_srv_process_cb_t process_certconf_cb;
    cmp_srv_process_cb_t process_rr_cb;
    cmp_srv_process_cb_t process_error_cb;
    cmp_srv_process_cb_t process_genm_cb;

} /* OSSL_CMP_SRV_CTX */ ;

void OSSL_CMP_SRV_CTX_free(OSSL_CMP_SRV_CTX *srv_ctx)
{
    if (srv_ctx == NULL)
        return;

    X509_free(srv_ctx->certOut);
    sk_X509_pop_free(srv_ctx->chainOut, X509_free);
    sk_X509_pop_free(srv_ctx->caPubsOut, X509_free);
    OSSL_CMP_PKISI_free(srv_ctx->pkiStatusOut);
    OSSL_CMP_MSG_free(srv_ctx->certReq);
    OSSL_CMP_CTX_free(srv_ctx->ctx);
    OPENSSL_free(srv_ctx);
}

OSSL_CMP_CTX *OSSL_CMP_SRV_CTX_get0_ctx(const OSSL_CMP_SRV_CTX *srv_ctx)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return NULL;
    }
    return srv_ctx->ctx;
}

int OSSL_CMP_SRV_CTX_set_grant_implicit_confirm(OSSL_CMP_SRV_CTX *srv_ctx,
                                                int value)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    srv_ctx->grantImplicitConfirm = value != 0 ? 1 : 0;
    return 1;
}

int OSSL_CMP_SRV_CTX_set_accept_unprotected(OSSL_CMP_SRV_CTX *srv_ctx,
                                            int value)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    srv_ctx->acceptUnprotectedRequests = value != 0 ? 1 : 0;
    return 1;
}

int OSSL_CMP_SRV_CTX_set_send_unprotected_errors(OSSL_CMP_SRV_CTX *srv_ctx,
                                                 int value)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    srv_ctx->sendUnprotectedErrors = value != 0 ? 1 : 0;
    return 1;
}

int OSSL_CMP_SRV_CTX_set_statusInfo(OSSL_CMP_SRV_CTX *srv_ctx, int status,
                                    int fail_info, const char *text)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    OSSL_CMP_PKISI_free(srv_ctx->pkiStatusOut);
    return (srv_ctx->pkiStatusOut = ossl_cmp_statusinfo_new(status,
                                                            fail_info, text))
            != NULL;
}

int OSSL_CMP_SRV_CTX_set1_certOut(OSSL_CMP_SRV_CTX *srv_ctx, X509 *cert)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    X509_free(srv_ctx->certOut);
    if (X509_up_ref(cert)) {
        srv_ctx->certOut = cert;
        return 1;
    }
    srv_ctx->certOut = NULL;
    return 0;
}

int OSSL_CMP_SRV_CTX_set1_chainOut(OSSL_CMP_SRV_CTX *srv_ctx,
                                   STACK_OF(X509) *chain)
{
    if (srv_ctx == NULL || chain == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    sk_X509_pop_free(srv_ctx->chainOut, X509_free);
    return (srv_ctx->chainOut = X509_chain_up_ref(chain)) != NULL;
}

int OSSL_CMP_SRV_CTX_set1_caPubsOut(OSSL_CMP_SRV_CTX *srv_ctx,
                                    STACK_OF(X509) *caPubs)
{
    if (srv_ctx == NULL || caPubs == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    sk_X509_pop_free(srv_ctx->caPubsOut, X509_free);
    return (srv_ctx->caPubsOut = X509_chain_up_ref(caPubs)) != NULL;
}

int OSSL_CMP_SRV_CTX_set_send_error(OSSL_CMP_SRV_CTX *srv_ctx, int error)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    srv_ctx->sendError = error != 0 ? 1 : 0;
    return 1;
}

int OSSL_CMP_SRV_CTX_set_checkAfterTime(OSSL_CMP_SRV_CTX *srv_ctx, int64_t sec)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    srv_ctx->checkAfterTime = sec;
    return 1;
}

int OSSL_CMP_SRV_CTX_set_pollCount(OSSL_CMP_SRV_CTX *srv_ctx, int64_t count)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    if (count < 0) {
        CMPerr(0, CMP_R_INVALID_ARGS);
        return 0;
    }
    srv_ctx->pollCount = count;
    return 1;
}

int OSSL_CMP_SRV_CTX_set_accept_raverified(OSSL_CMP_SRV_CTX *srv_ctx,
                                           int raverified)
{
    if (srv_ctx == NULL) {
        CMPerr(0, CMP_R_NULL_ARGUMENT);
        return 0;
    }
    srv_ctx->acceptRAVerified = raverified != 0 ? 1 : 0;
    return 1;
}

/*
 * Processes an ir/cr/p10cr/kur and returns a certification response.
 * Only handles the first certification request contained in certReq
 * returns an ip/cp/kup on success and NULL on error
 */
static OSSL_CMP_MSG *process_cert_request(OSSL_CMP_SRV_CTX *srv_ctx,
                                          OSSL_CMP_MSG *certReq)
{
    OSSL_CMP_MSG *msg = NULL;
    OSSL_CMP_PKISI *si = NULL;
    X509 *certOut = NULL;
    STACK_OF(X509) *chainOut = NULL, *caPubs = NULL;
    OSSL_CRMF_MSG *crm = NULL;
    int bodytype;

    if (!ossl_assert(srv_ctx != NULL && certReq != NULL))
        return NULL;

    switch (certReq->body->type) {
    case OSSL_CMP_PKIBODY_P10CR:
    case OSSL_CMP_PKIBODY_CR:
        bodytype = OSSL_CMP_PKIBODY_CP;
        break;
    case OSSL_CMP_PKIBODY_IR:
        bodytype = OSSL_CMP_PKIBODY_IP;
        break;
    case OSSL_CMP_PKIBODY_KUR:
        bodytype = OSSL_CMP_PKIBODY_KUP;
        break;
    default:
        CMPerr(0, CMP_R_UNEXPECTED_PKIBODY);
        return NULL;
    }

    if (certReq->body->type == OSSL_CMP_PKIBODY_P10CR) {
        srv_ctx->certReqId = OSSL_CMP_CERTREQID;
    } else {
        if ((crm = sk_OSSL_CRMF_MSG_value(certReq->body->value.cr,
                                          OSSL_CMP_CERTREQID)) == NULL) {
            CMPerr(0, CMP_R_CERTREQMSG_NOT_FOUND);
            return NULL;
        }
        srv_ctx->certReqId = OSSL_CRMF_MSG_get_certReqId(crm);
    }

    if (!ossl_cmp_verify_popo(certReq, srv_ctx->acceptRAVerified)) {
        /* Proof of possession could not be verified */
        if ((si = ossl_cmp_statusinfo_new(OSSL_CMP_PKISTATUS_rejection,
                                          1 << OSSL_CMP_PKIFAILUREINFO_badPOP,
                                          NULL)) == NULL)
            goto err;
    } else if (srv_ctx->pollCount > 0) {
        srv_ctx->pollCount--;
        if ((si = ossl_cmp_statusinfo_new(OSSL_CMP_PKISTATUS_waiting,
                                          OSSL_CMP_CERTREQID, NULL)) == NULL)
            goto err;
        OSSL_CMP_MSG_free(srv_ctx->certReq);
        if ((srv_ctx->certReq = OSSL_CMP_MSG_dup(certReq)) == NULL)
            goto err;
    } else {
        /*
         * TODO when implemented in CMP_certrep_new():
         * in case OSSL_CRMF_POPO_KEYENC, set srv_ctx->encryptcert = 1
         */
        certOut = srv_ctx->certOut;
        chainOut = srv_ctx->chainOut;
        caPubs = srv_ctx->caPubsOut;
        if (ossl_cmp_hdr_check_implicitConfirm(certReq->header)
                && srv_ctx->grantImplicitConfirm)
            OSSL_CMP_CTX_set_option(srv_ctx->ctx,
                                    OSSL_CMP_OPT_IMPLICITCONFIRM, 1);
        if ((si = OSSL_CMP_PKISI_dup(srv_ctx->pkiStatusOut)) == NULL)
            goto err;
    }

    msg = ossl_cmp_certRep_new(srv_ctx->ctx, bodytype, srv_ctx->certReqId, si,
                               certOut, chainOut, caPubs, srv_ctx->encryptcert,
                               srv_ctx->sendUnprotectedErrors);
    if (msg == NULL)
        CMPerr(0, CMP_R_ERROR_CREATING_CERTREP);

    OSSL_CMP_PKISI_free(si);
    return msg;

 err:
    OSSL_CMP_PKISI_free(si);
    return NULL;
}

static OSSL_CMP_MSG *process_rr(OSSL_CMP_SRV_CTX *srv_ctx, OSSL_CMP_MSG *req)
{
    OSSL_CMP_MSG *msg;
    OSSL_CMP_REVDETAILS *details;
    OSSL_CRMF_CERTID *certId;
    OSSL_CRMF_CERTTEMPLATE *tmpl;
    X509_NAME *issuer;
    ASN1_INTEGER *serial;

    if (!ossl_assert(srv_ctx != NULL && req != NULL))
        return NULL;

    if ((details = sk_OSSL_CMP_REVDETAILS_value(req->body->value.rr,
                                                OSSL_CMP_REVREQSID)) == NULL) {
        CMPerr(0, CMP_R_ERROR_PROCESSING_MSG);
        return NULL;
    }

    /* accept revocation only for the certificate we send in ir/cr/kur */
    tmpl = details->certDetails;
    issuer = OSSL_CRMF_CERTTEMPLATE_get0_issuer(tmpl);
    serial = OSSL_CRMF_CERTTEMPLATE_get0_serialNumber(tmpl);
    if (X509_NAME_cmp(issuer, X509_get_issuer_name(srv_ctx->certOut)) != 0
            || ASN1_INTEGER_cmp(serial,
                                X509_get0_serialNumber(srv_ctx->certOut)) != 0) {
        CMPerr(0, CMP_R_REQUEST_NOT_ACCEPTED);
        return NULL;
    }

    if ((certId = OSSL_CRMF_CERTID_gen(issuer, serial)) == NULL)
        return NULL;

    if ((msg = ossl_cmp_rp_new(srv_ctx->ctx, srv_ctx->pkiStatusOut, certId,
                               srv_ctx->sendUnprotectedErrors)) == NULL)
        CMPerr(0, CMP_R_ERROR_CREATING_RR);
    OSSL_CRMF_CERTID_free(certId);
    return msg;
}

static OSSL_CMP_MSG *process_certConf(OSSL_CMP_SRV_CTX *srv_ctx,
                                      OSSL_CMP_MSG *req)
{
    OSSL_CMP_MSG *msg = NULL;
    OSSL_CMP_CERTSTATUS *status = NULL;
    ASN1_OCTET_STRING *tmp = NULL;
    int res = -1;
    int num = sk_OSSL_CMP_CERTSTATUS_num(req->body->value.certConf);

    if (num == 0) {
        OSSL_CMP_err("certificate rejected by client");
    } else {
        if (num > 1)
            OSSL_CMP_warn("All CertStatus but the first will be ignored");
        status = sk_OSSL_CMP_CERTSTATUS_value(req->body->value.certConf,
                                              OSSL_CMP_CERTREQID);
    }

    if (status != NULL) {
        /* check cert request id */
        if (ossl_cmp_asn1_get_int(status->certReqId) != srv_ctx->certReqId) {
            /* in case of error, invalid reqId -1 */
            CMPerr(0, CMP_R_UNEXPECTED_REQUEST_ID);
            return NULL;
        }

        /* check cert hash by recalculating it in place */
        tmp = status->certHash;
        status->certHash = NULL;
        if (ossl_cmp_certstatus_set_certHash(status, srv_ctx->certOut))
            res = status->certHash == NULL ? 0 /* avoiding SCA false positive */
                  : ASN1_OCTET_STRING_cmp(tmp, status->certHash) == 0;
        ASN1_OCTET_STRING_free(status->certHash);
        status->certHash = tmp;
        if (res == -1)
            return NULL;
        if (res == 0) {
            CMPerr(0, CMP_R_WRONG_CERT_HASH);
            return NULL;
        }

        if (status->statusInfo != NULL
                && status->statusInfo->status != OSSL_CMP_PKISTATUS_accepted) {
            int pki_status = ossl_cmp_pkisi_get_pkistatus(status->statusInfo);
            const char *str = ossl_cmp_PKIStatus_to_string(pki_status);

            OSSL_CMP_log2(INFO, "certificate rejected by client %s %s",
                          str == NULL ? "without" : "with",
                          str == NULL ? "PKIStatus" : str);
        }
    }

    if ((msg = ossl_cmp_pkiconf_new(srv_ctx->ctx)) == NULL) {
        CMPerr(0, CMP_R_ERROR_CREATING_PKICONF);
        return NULL;
    }

    return msg;
}

static OSSL_CMP_MSG *process_error(OSSL_CMP_SRV_CTX *srv_ctx,
                                   OSSL_CMP_MSG *req)
{
    OSSL_CMP_MSG *msg = ossl_cmp_pkiconf_new(srv_ctx->ctx);

    (void)req; /* TODO make use of parameter */
    if (msg == NULL) {
        CMPerr(0, CMP_R_ERROR_CREATING_PKICONF);
        return NULL;
    }

    return msg;
}

static OSSL_CMP_MSG *process_pollReq(OSSL_CMP_SRV_CTX *srv_ctx,
                                     OSSL_CMP_MSG *req)
{
    OSSL_CMP_MSG *msg = NULL;

    (void)req; /* TODO make use of parameter */
    if (!ossl_assert(srv_ctx != NULL && srv_ctx->certReq != NULL))
        return NULL;

    if (srv_ctx->pollCount == 0) {
        if ((msg = process_cert_request(srv_ctx, srv_ctx->certReq)) == NULL)
            CMPerr(0, CMP_R_ERROR_PROCESSING_CERTREQ);
    } else {
        srv_ctx->pollCount--;
        if ((msg = ossl_cmp_pollRep_new(srv_ctx->ctx, srv_ctx->certReqId,
                                        srv_ctx->checkAfterTime)) == NULL)
            CMPerr(0, CMP_R_ERROR_CREATING_POLLREP);
    }
    return msg;
}

/*
 * Processes genm and creates a genp message mirroring the contents of the
 * incoming message
 */
static OSSL_CMP_MSG *process_genm(OSSL_CMP_SRV_CTX *srv_ctx,
                                  OSSL_CMP_MSG *req)
{
    OSSL_CMP_MSG *msg = NULL;

    STACK_OF(OSSL_CMP_ITAV) *tmp = NULL;

    if (!ossl_assert(srv_ctx != NULL && srv_ctx->ctx != NULL && req != NULL))
        return NULL;

    tmp = srv_ctx->ctx->genm_ITAVs; /* Back up potential genm_ITAVs */
    srv_ctx->ctx->genm_ITAVs = req->body->value.genm;
    msg = ossl_cmp_genp_new(srv_ctx->ctx); /* may be NULL */
    srv_ctx->ctx->genm_ITAVs = tmp; /* restore genm_ITAVs */
    return msg;
}

/*
 * Determines whether missing protection is allowed
 */
static int unprotected_exception(const OSSL_CMP_CTX *ctx,
                                 const OSSL_CMP_MSG *req,
                                 int invalid_protection,
                                 int accept_unprotected_requests)
{
    if (accept_unprotected_requests) {
        OSSL_CMP_log1(WARN, "ignoring %s protection of request message",
                      invalid_protection ? "invalid" : "missing");
        return 1;
    }
    if (req->body->type == OSSL_CMP_PKIBODY_ERROR && ctx->unprotectedErrors) {
        OSSL_CMP_warn("ignoring missing protection of error message");
        return 1;
    }
    return 0;
}

/*
 * Mocks the server/responder.
 * srv_ctx is the context of the server
 * returns 1 if a message was created and 0 on error
 */
static int process_request(OSSL_CMP_SRV_CTX *srv_ctx, OSSL_CMP_MSG *req,
                           OSSL_CMP_MSG **rsp)
{
    cmp_srv_process_cb_t process_cb = NULL;
    OSSL_CMP_CTX *ctx;

    if (!ossl_assert(srv_ctx != NULL && srv_ctx->ctx != NULL && req != NULL
                     && rsp != NULL))
        return 0;

    ctx = srv_ctx->ctx;
    *rsp = NULL;

    if (req->header->sender->type != GEN_DIRNAME) {
        CMPerr(0, CMP_R_SENDER_GENERALNAME_TYPE_NOT_SUPPORTED);
        return 0;
    }
    if (!X509_NAME_set(&ctx->recipient, req->header->sender->d.directoryName))
        return 0;

    if (ossl_cmp_msg_check_received(ctx, req, unprotected_exception,
                                    srv_ctx->acceptUnprotectedRequests) < 0) {
        CMPerr(0, CMP_R_FAILED_TO_RECEIVE_PKIMESSAGE);
        return 0;
    }
    if (srv_ctx->sendError) {
        if ((*rsp = ossl_cmp_error_new(ctx, srv_ctx->pkiStatusOut, -1, NULL,
                                       srv_ctx->sendUnprotectedErrors)))
            return 1;
        CMPerr(0, CMP_R_ERROR_CREATING_ERROR);
        return 0;
    }

    switch (req->body->type) {
    case OSSL_CMP_PKIBODY_IR:
        process_cb = srv_ctx->process_ir_cb;
        break;
    case OSSL_CMP_PKIBODY_CR:
        process_cb = srv_ctx->process_cr_cb;
        break;
    case OSSL_CMP_PKIBODY_P10CR:
        process_cb = srv_ctx->process_p10cr_cb;
        break;
    case OSSL_CMP_PKIBODY_KUR:
        process_cb = srv_ctx->process_kur_cb;
        break;
    case OSSL_CMP_PKIBODY_POLLREQ:
        process_cb = srv_ctx->process_pollreq_cb;
        break;
    case OSSL_CMP_PKIBODY_RR:
        process_cb = srv_ctx->process_rr_cb;
        break;
    case OSSL_CMP_PKIBODY_ERROR:
        process_cb = srv_ctx->process_error_cb;
        break;
    case OSSL_CMP_PKIBODY_CERTCONF:
        process_cb = srv_ctx->process_certconf_cb;
        break;
    case OSSL_CMP_PKIBODY_GENM:
        process_cb = srv_ctx->process_genm_cb;
        break;
    default:
        CMPerr(0, CMP_R_UNEXPECTED_PKIBODY);
        break;
    }
    if (process_cb == NULL)
        return 0;
    if ((*rsp = process_cb(srv_ctx, req)) == NULL)
        return 0;

    return 1;
}

/*
 * Mocks the server connection. Works similar to OSSL_CMP_MSG_http_perform.
 * A OSSL_CMP_SRV_CTX must be set as transfer_cb_arg
 * returns 0 on success and else a CMP error reason code defined in cmp.h
 */
int OSSL_CMP_mock_server_perform(OSSL_CMP_CTX *cmp_ctx, const OSSL_CMP_MSG *req,
                                 OSSL_CMP_MSG **rsp)
{
    OSSL_CMP_MSG *srv_req = NULL, *srv_rsp = NULL;
    OSSL_CMP_SRV_CTX *srv_ctx = NULL;
    OSSL_CMP_PKISI *si = NULL;
    OSSL_CMP_PKIFREETEXT *details = NULL;
    int error = 0;

    if (cmp_ctx == NULL || req == NULL || rsp == NULL)
        return CMP_R_NULL_ARGUMENT;
    *rsp = NULL;

    if ((srv_ctx = OSSL_CMP_CTX_get_transfer_cb_arg(cmp_ctx)) == NULL)
        return CMP_R_ERROR_TRANSFERRING_OUT;

    /* OSSL_CMP_MSG_dup encodes and decodes ASN.1, used for checking encoding */
    if ((srv_req = OSSL_CMP_MSG_dup(req)) == NULL) {
        error = CMP_R_ERROR_DECODING_MESSAGE;
        goto end;
    }

    if (!process_request(srv_ctx, srv_req, &srv_rsp)) {
        const char *data;
        int flags = 0;
        unsigned long err = ERR_peek_error_data(&data, &flags);

        error = CMP_R_ERROR_PROCESSING_MSG;
        if ((si = ossl_cmp_statusinfo_new(OSSL_CMP_PKISTATUS_rejection,
                                          1<<OSSL_CMP_PKIFAILUREINFO_badRequest,
                         /* TODO failure bit(s) may be could be more specific */
                                          NULL)) == NULL)
            goto end;
        if ((details = sk_ASN1_UTF8STRING_new_null()) == NULL)
            goto end;
        if (err != 0 && (flags & ERR_TXT_STRING) != 0 && data != NULL
                && !ossl_cmp_pkifreetext_push_str(details, data))
            goto end;
        srv_rsp = ossl_cmp_error_new(cmp_ctx, si,
                                     err != 0 ? ERR_GET_REASON(err) : -1,
                                     details, srv_ctx->sendUnprotectedErrors);
        if (srv_rsp == NULL)
            goto end;
        error = 0; /* no internal error, but CMP error reported to client */
    }

    /* OSSL_CMP_MSG_dup encodes and decodes ASN.1, used for checking encoding */
    if ((*rsp = OSSL_CMP_MSG_dup(srv_rsp)) == NULL) {
        error = CMP_R_ERROR_DECODING_MESSAGE;
        goto end;
    }

 end:
    sk_ASN1_UTF8STRING_pop_free(details, ASN1_UTF8STRING_free);
    OSSL_CMP_MSG_free(srv_req);
    OSSL_CMP_MSG_free(srv_rsp);
    OSSL_CMP_PKISI_free(si);

    return error;
}

/*
 * creates and initializes a OSSL_CMP_SRV_CTX structure
 * returns pointer to created CMP_SRV_ on success, NULL on error
 */
/* This declaration is here to avoid forward declarations of many functions */
OSSL_CMP_SRV_CTX *OSSL_CMP_SRV_CTX_new(void)
{
    OSSL_CMP_SRV_CTX *ctx = OPENSSL_zalloc(sizeof(OSSL_CMP_SRV_CTX));

    if (ctx == NULL)
        goto err;

    if ((ctx->ctx = OSSL_CMP_CTX_new()) == NULL)
        goto err;

    if ((ctx->pkiStatusOut = OSSL_CMP_PKISI_new()) == NULL)
        goto err;

    ctx->certReqId = OSSL_CMP_CERTREQID;
    ctx->checkAfterTime = 1;

    ctx->process_ir_cb = process_cert_request;
    ctx->process_cr_cb = process_cert_request;
    ctx->process_p10cr_cb = process_cert_request;
    ctx->process_kur_cb = process_cert_request;
    ctx->process_pollreq_cb = process_pollReq;
    ctx->process_certconf_cb = process_certConf;
    ctx->process_rr_cb = process_rr;
    ctx->process_error_cb = process_error;
    ctx->process_genm_cb = process_genm;

    /* all other elements are initialized to 0 or NULL, respectively */
    return ctx;
 err:
    OSSL_CMP_SRV_CTX_free(ctx);
    return NULL;
}