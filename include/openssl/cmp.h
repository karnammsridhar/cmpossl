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

#ifndef OPENSSL_CMP_H
# define OPENSSL_CMP_H

# include <openssl/opensslconf.h>
# ifndef OPENSSL_NO_CMP

#  include <openssl/crmf.h>
#  include <openssl/cmperr.h>
#  include <openssl/cmp_util.h>

/* explicit #includes not strictly needed since implied by the above: */
#  include <openssl/types.h>
#  include <openssl/safestack.h>
#  include <openssl/x509.h>
#  include <openssl/x509v3.h>

#  ifdef __cplusplus
extern "C" {
#  endif

#  define OSSL_CMP_PVNO 2

/*-
 *   PKIFailureInfo ::= BIT STRING {
 *   -- since we can fail in more than one way!
 *   -- More codes may be added in the future if/when required.
 *       badAlg              (0),
 *       -- unrecognized or unsupported Algorithm Identifier
 *       badMessageCheck     (1),
 *       -- integrity check failed (e.g., signature did not verify)
 *       badRequest          (2),
 *       -- transaction not permitted or supported
 *       badTime             (3),
 *       -- messageTime was not sufficiently close to the system time,
 *       -- as defined by local policy
 *       badCertId           (4),
 *       -- no certificate could be found matching the provided criteria
 *       badDataFormat       (5),
 *       -- the data submitted has the wrong format
 *       wrongAuthority      (6),
 *       -- the authority indicated in the request is different from the
 *       -- one creating the response token
 *       incorrectData       (7),
 *       -- the requester's data is incorrect (for notary services)
 *       missingTimeStamp    (8),
 *       -- when the timestamp is missing but should be there
 *       -- (by policy)
 *       badPOP              (9),
 *       -- the proof-of-possession failed
 *       certRevoked         (10),
 *          -- the certificate has already been revoked
 *       certConfirmed       (11),
 *          -- the certificate has already been confirmed
 *       wrongIntegrity      (12),
 *          -- invalid integrity, password based instead of signature or
 *          -- vice versa
 *       badRecipientNonce   (13),
 *          -- invalid recipient nonce, either missing or wrong value
 *       timeNotAvailable    (14),
 *          -- the TSA's time source is not available
 *       unacceptedPolicy    (15),
 *          -- the requested TSA policy is not supported by the TSA.
 *       unacceptedExtension (16),
 *          -- the requested extension is not supported by the TSA.
 *       addInfoNotAvailable (17),
 *          -- the additional information requested could not be
 *          -- understood or is not available
 *       badSenderNonce      (18),
 *          -- invalid sender nonce, either missing or wrong size
 *       badCertTemplate     (19),
 *          -- invalid cert. template or missing mandatory information
 *       signerNotTrusted    (20),
 *          -- signer of the message unknown or not trusted
 *       transactionIdInUse  (21),
 *          -- the transaction identifier is already in use
 *       unsupportedVersion  (22),
 *          -- the version of the message is not supported
 *       notAuthorized       (23),
 *          -- the sender was not authorized to make the preceding
 *          -- request or perform the preceding action
 *       systemUnavail       (24),
 *       -- the request cannot be handled due to system unavailability
 *       systemFailure       (25),
 *       -- the request cannot be handled due to system failure
 *       duplicateCertReq    (26)
 *       -- certificate cannot be issued because a duplicate
 *       -- certificate already exists
 *   }
 */
#  define OSSL_CMP_PKIFAILUREINFO_badAlg 0
#  define OSSL_CMP_PKIFAILUREINFO_badMessageCheck 1
#  define OSSL_CMP_PKIFAILUREINFO_badRequest 2
#  define OSSL_CMP_PKIFAILUREINFO_badTime 3
#  define OSSL_CMP_PKIFAILUREINFO_badCertId 4
#  define OSSL_CMP_PKIFAILUREINFO_badDataFormat 5
#  define OSSL_CMP_PKIFAILUREINFO_wrongAuthority 6
#  define OSSL_CMP_PKIFAILUREINFO_incorrectData 7
#  define OSSL_CMP_PKIFAILUREINFO_missingTimeStamp 8
#  define OSSL_CMP_PKIFAILUREINFO_badPOP 9
#  define OSSL_CMP_PKIFAILUREINFO_certRevoked 10
#  define OSSL_CMP_PKIFAILUREINFO_certConfirmed 11
#  define OSSL_CMP_PKIFAILUREINFO_wrongIntegrity 12
#  define OSSL_CMP_PKIFAILUREINFO_badRecipientNonce 13
#  define OSSL_CMP_PKIFAILUREINFO_timeNotAvailable 14
#  define OSSL_CMP_PKIFAILUREINFO_unacceptedPolicy 15
#  define OSSL_CMP_PKIFAILUREINFO_unacceptedExtension 16
#  define OSSL_CMP_PKIFAILUREINFO_addInfoNotAvailable 17
#  define OSSL_CMP_PKIFAILUREINFO_badSenderNonce 18
#  define OSSL_CMP_PKIFAILUREINFO_badCertTemplate 19
#  define OSSL_CMP_PKIFAILUREINFO_signerNotTrusted 20
#  define OSSL_CMP_PKIFAILUREINFO_transactionIdInUse 21
#  define OSSL_CMP_PKIFAILUREINFO_unsupportedVersion 22
#  define OSSL_CMP_PKIFAILUREINFO_notAuthorized 23
#  define OSSL_CMP_PKIFAILUREINFO_systemUnavail 24
#  define OSSL_CMP_PKIFAILUREINFO_systemFailure 25
#  define OSSL_CMP_PKIFAILUREINFO_duplicateCertReq 26
#  define OSSL_CMP_PKIFAILUREINFO_MAX 26
#  define OSSL_CMP_PKIFAILUREINFO_MAX_BIT_PATTERN \
    ((1 << (OSSL_CMP_PKIFAILUREINFO_MAX + 1)) - 1)
#  if OSSL_CMP_PKIFAILUREINFO_MAX_BIT_PATTERN > INT_MAX
#   error CMP_PKIFAILUREINFO_MAX bit pattern does not fit in type int
#  endif

typedef ASN1_BIT_STRING OSSL_CMP_PKIFAILUREINFO;

#  define OSSL_CMP_CTX_FAILINFO_badAlg (1 << 0)
#  define OSSL_CMP_CTX_FAILINFO_badMessageCheck (1 << 1)
#  define OSSL_CMP_CTX_FAILINFO_badRequest (1 << 2)
#  define OSSL_CMP_CTX_FAILINFO_badTime (1 << 3)
#  define OSSL_CMP_CTX_FAILINFO_badCertId (1 << 4)
#  define OSSL_CMP_CTX_FAILINFO_badDataFormat (1 << 5)
#  define OSSL_CMP_CTX_FAILINFO_wrongAuthority (1 << 6)
#  define OSSL_CMP_CTX_FAILINFO_incorrectData (1 << 7)
#  define OSSL_CMP_CTX_FAILINFO_missingTimeStamp (1 << 8)
#  define OSSL_CMP_CTX_FAILINFO_badPOP (1 << 9)
#  define OSSL_CMP_CTX_FAILINFO_certRevoked (1 << 10)
#  define OSSL_CMP_CTX_FAILINFO_certConfirmed (1 << 11)
#  define OSSL_CMP_CTX_FAILINFO_wrongIntegrity (1 << 12)
#  define OSSL_CMP_CTX_FAILINFO_badRecipientNonce (1 << 13)
#  define OSSL_CMP_CTX_FAILINFO_timeNotAvailable (1 << 14)
#  define OSSL_CMP_CTX_FAILINFO_unacceptedPolicy (1 << 15)
#  define OSSL_CMP_CTX_FAILINFO_unacceptedExtension (1 << 16)
#  define OSSL_CMP_CTX_FAILINFO_addInfoNotAvailable (1 << 17)
#  define OSSL_CMP_CTX_FAILINFO_badSenderNonce (1 << 18)
#  define OSSL_CMP_CTX_FAILINFO_badCertTemplate (1 << 19)
#  define OSSL_CMP_CTX_FAILINFO_signerNotTrusted (1 << 20)
#  define OSSL_CMP_CTX_FAILINFO_transactionIdInUse (1 << 21)
#  define OSSL_CMP_CTX_FAILINFO_unsupportedVersion (1 << 22)
#  define OSSL_CMP_CTX_FAILINFO_notAuthorized (1 << 23)
#  define OSSL_CMP_CTX_FAILINFO_systemUnavail (1 << 24)
#  define OSSL_CMP_CTX_FAILINFO_systemFailure (1 << 25)
#  define OSSL_CMP_CTX_FAILINFO_duplicateCertReq (1 << 26)

/*-
 *   PKIStatus ::= INTEGER {
 *       accepted                (0),
 *       -- you got exactly what you asked for
 *       grantedWithMods        (1),
 *       -- you got something like what you asked for; the
 *       -- requester is responsible for ascertaining the differences
 *       rejection              (2),
 *       -- you don't get it, more information elsewhere in the message
 *       waiting                (3),
 *       -- the request body part has not yet been processed; expect to
 *       -- hear more later (note: proper handling of this status
 *       -- response MAY use the polling req/rep PKIMessages specified
 *       -- in Section 5.3.22; alternatively, polling in the underlying
 *       -- transport layer MAY have some utility in this regard)
 *       revocationWarning      (4),
 *       -- this message contains a warning that a revocation is
 *       -- imminent
 *       revocationNotification (5),
 *       -- notification that a revocation has occurred
 *       keyUpdateWarning       (6)
 *       -- update already done for the oldCertId specified in
 *       -- CertReqMsg
 *   }
 */
#  define OSSL_CMP_PKISTATUS_accepted 0
#  define OSSL_CMP_PKISTATUS_grantedWithMods 1
#  define OSSL_CMP_PKISTATUS_rejection 2
#  define OSSL_CMP_PKISTATUS_waiting 3
#  define OSSL_CMP_PKISTATUS_revocationWarning 4
#  define OSSL_CMP_PKISTATUS_revocationNotification 5
#  define OSSL_CMP_PKISTATUS_keyUpdateWarning 6

typedef ASN1_INTEGER OSSL_CMP_PKISTATUS;
DECLARE_ASN1_ITEM(OSSL_CMP_PKISTATUS)

#  define OSSL_CMP_CERTORENCCERT_CERTIFICATE 0
#  define OSSL_CMP_CERTORENCCERT_ENCRYPTEDCERT 1

/* data type declarations */
typedef struct ossl_cmp_ctx_st OSSL_CMP_CTX;
typedef struct ossl_cmp_pkiheader_st OSSL_CMP_PKIHEADER;
DECLARE_ASN1_FUNCTIONS(OSSL_CMP_PKIHEADER)
typedef struct ossl_cmp_msg_st OSSL_CMP_MSG;
DECLARE_ASN1_DUP_FUNCTION(OSSL_CMP_MSG)
DECLARE_ASN1_ENCODE_FUNCTIONS(OSSL_CMP_MSG, OSSL_CMP_MSG, OSSL_CMP_MSG)
typedef struct ossl_cmp_certstatus_st OSSL_CMP_CERTSTATUS;
DEFINE_STACK_OF(OSSL_CMP_CERTSTATUS)
typedef struct ossl_cmp_itav_st OSSL_CMP_ITAV;
DECLARE_ASN1_DUP_FUNCTION(OSSL_CMP_ITAV)
DEFINE_STACK_OF(OSSL_CMP_ITAV)
typedef struct ossl_cmp_revrepcontent_st OSSL_CMP_REVREPCONTENT;
typedef struct ossl_cmp_pkisi_st OSSL_CMP_PKISI;
DECLARE_ASN1_FUNCTIONS(OSSL_CMP_PKISI)
DECLARE_ASN1_DUP_FUNCTION(OSSL_CMP_PKISI)
DEFINE_STACK_OF(OSSL_CMP_PKISI)
typedef struct ossl_cmp_certrepmessage_st OSSL_CMP_CERTREPMESSAGE;
DEFINE_STACK_OF(OSSL_CMP_CERTREPMESSAGE)
typedef struct ossl_cmp_pollrep_st OSSL_CMP_POLLREP;
typedef STACK_OF(OSSL_CMP_POLLREP) OSSL_CMP_POLLREPCONTENT;
typedef struct ossl_cmp_certresponse_st OSSL_CMP_CERTRESPONSE;
DEFINE_STACK_OF(OSSL_CMP_CERTRESPONSE)
typedef STACK_OF(ASN1_UTF8STRING) OSSL_CMP_PKIFREETEXT;

/*
 * function DECLARATIONS
 */

/* from cmp_asn.c */
OSSL_CMP_ITAV *OSSL_CMP_ITAV_create(ASN1_OBJECT *type, ASN1_TYPE *value);
void OSSL_CMP_ITAV_set0(OSSL_CMP_ITAV *itav, ASN1_OBJECT *type,
                        ASN1_TYPE *value);
ASN1_OBJECT *OSSL_CMP_ITAV_get0_type(const OSSL_CMP_ITAV *itav);
ASN1_TYPE *OSSL_CMP_ITAV_get0_value(const OSSL_CMP_ITAV *itav);
int OSSL_CMP_ITAV_push0_stack_item(STACK_OF(OSSL_CMP_ITAV) **itav_sk_p,
                                   OSSL_CMP_ITAV *itav);
void OSSL_CMP_ITAV_free(OSSL_CMP_ITAV *itav);
void OSSL_CMP_MSG_free(OSSL_CMP_MSG *msg);

/* from cmp_ctx.c */
OSSL_CMP_CTX *OSSL_CMP_CTX_new(void);
void OSSL_CMP_CTX_free(OSSL_CMP_CTX *ctx);
int OSSL_CMP_CTX_reinit(OSSL_CMP_CTX *ctx);
/* various CMP options: */
#  define OSSL_CMP_OPT_LOG_VERBOSITY 0
#  define OSSL_CMP_OPT_MSGTIMEOUT 1
#  define OSSL_CMP_OPT_TOTALTIMEOUT 2
#  define OSSL_CMP_OPT_VALIDITYDAYS 3
#  define OSSL_CMP_OPT_SUBJECTALTNAME_NODEFAULT 4
#  define OSSL_CMP_OPT_SUBJECTALTNAME_CRITICAL 5
#  define OSSL_CMP_OPT_POLICIES_CRITICAL 6
#  define OSSL_CMP_OPT_POPOMETHOD 7
#  define OSSL_CMP_OPT_DIGEST_ALGNID 8
#  define OSSL_CMP_OPT_OWF_ALGNID 9
#  define OSSL_CMP_OPT_MAC_ALGNID 10
#  define OSSL_CMP_OPT_REVOCATION_REASON 11
#  define OSSL_CMP_OPT_IMPLICITCONFIRM 12
#  define OSSL_CMP_OPT_DISABLECONFIRM 13
#  define OSSL_CMP_OPT_UNPROTECTED_SEND 14
#  define OSSL_CMP_OPT_UNPROTECTED_ERRORS 15
#  define OSSL_CMP_OPT_IGNORE_KEYUSAGE 16
#  define OSSL_CMP_OPT_PERMIT_TA_IN_EXTRACERTS_FOR_IR 17
int OSSL_CMP_CTX_set_option(OSSL_CMP_CTX *ctx, int opt, int val);
int OSSL_CMP_CTX_get_option(const OSSL_CMP_CTX *ctx, int opt);
/* CMP-specific callback for logging and outputting the error queue: */
int OSSL_CMP_CTX_set_log_cb(OSSL_CMP_CTX *ctx, OSSL_cmp_log_cb_t cb);
#  define OSSL_CMP_CTX_set_log_verbosity(ctx, level) \
    OSSL_CMP_CTX_set_option(ctx, OSSL_CMP_OPT_LOG_VERBOSITY, level)
void OSSL_CMP_CTX_print_errors(OSSL_CMP_CTX *ctx);
/* message transfer: */
int OSSL_CMP_CTX_set1_serverPath(OSSL_CMP_CTX *ctx, const char *path);
int OSSL_CMP_CTX_set1_serverName(OSSL_CMP_CTX *ctx, const char *name);
int OSSL_CMP_CTX_set_serverPort(OSSL_CMP_CTX *ctx, int port);
int OSSL_CMP_CTX_set1_proxyName(OSSL_CMP_CTX *ctx, const char *name);
int OSSL_CMP_CTX_set_proxyPort(OSSL_CMP_CTX *ctx, int port);
#  define OSSL_CMP_DEFAULT_PORT 80
typedef BIO *(*OSSL_cmp_http_cb_t) (OSSL_CMP_CTX *ctx, BIO *hbio,
                                    unsigned long detail);
int OSSL_CMP_CTX_set_http_cb(OSSL_CMP_CTX *ctx, OSSL_cmp_http_cb_t cb);
int OSSL_CMP_CTX_set_http_cb_arg(OSSL_CMP_CTX *ctx, void *arg);
void *OSSL_CMP_CTX_get_http_cb_arg(const OSSL_CMP_CTX *ctx);
typedef int (*OSSL_cmp_transfer_cb_t) (OSSL_CMP_CTX *ctx,
                                       const OSSL_CMP_MSG *req,
                                       OSSL_CMP_MSG **res);
int OSSL_CMP_CTX_set_transfer_cb(OSSL_CMP_CTX *ctx, OSSL_cmp_transfer_cb_t cb);
int OSSL_CMP_CTX_set_transfer_cb_arg(OSSL_CMP_CTX *ctx, void *arg);
void *OSSL_CMP_CTX_get_transfer_cb_arg(const OSSL_CMP_CTX *ctx);
/* server authentication: */
int OSSL_CMP_CTX_set1_srvCert(OSSL_CMP_CTX *ctx, X509 *cert);
int OSSL_CMP_CTX_set1_expected_sender(OSSL_CMP_CTX *ctx, const X509_NAME *name);
int OSSL_CMP_CTX_set0_trustedStore(OSSL_CMP_CTX *ctx, X509_STORE *store);
X509_STORE *OSSL_CMP_CTX_get0_trustedStore(const OSSL_CMP_CTX *ctx);
int OSSL_CMP_CTX_set1_untrusted_certs(OSSL_CMP_CTX *ctx, STACK_OF(X509) *certs);
STACK_OF(X509) *OSSL_CMP_CTX_get0_untrusted_certs(const OSSL_CMP_CTX *ctx);
/* client authentication: */
int OSSL_CMP_CTX_set1_clCert(OSSL_CMP_CTX *ctx, X509 *cert);
int OSSL_CMP_CTX_set1_pkey(OSSL_CMP_CTX *ctx, EVP_PKEY *pkey);
int OSSL_CMP_CTX_set1_referenceValue(OSSL_CMP_CTX *ctx,
                                     const unsigned char *ref, int len);
int OSSL_CMP_CTX_set1_secretValue(OSSL_CMP_CTX *ctx, const unsigned char *sec,
                                  const int len);
/* CMP message header and extra certificates: */
int OSSL_CMP_CTX_set1_recipient(OSSL_CMP_CTX *ctx, const X509_NAME *name);
int OSSL_CMP_CTX_push0_geninfo_ITAV(OSSL_CMP_CTX *ctx, OSSL_CMP_ITAV *itav);
int OSSL_CMP_CTX_set1_extraCertsOut(OSSL_CMP_CTX *ctx,
                                    STACK_OF(X509) *extraCertsOut);
/* certificate template: */
int OSSL_CMP_CTX_set0_newPkey(OSSL_CMP_CTX *ctx, int priv, EVP_PKEY *pkey);
EVP_PKEY *OSSL_CMP_CTX_get0_newPkey(const OSSL_CMP_CTX *ctx, int priv);
int OSSL_CMP_CTX_set1_issuer(OSSL_CMP_CTX *ctx, const X509_NAME *name);
int OSSL_CMP_CTX_set1_subjectName(OSSL_CMP_CTX *ctx, const X509_NAME *name);
int OSSL_CMP_CTX_push1_subjectAltName(OSSL_CMP_CTX *ctx, const GENERAL_NAME *name);
int OSSL_CMP_CTX_set0_reqExtensions(OSSL_CMP_CTX *ctx, X509_EXTENSIONS *exts);
int OSSL_CMP_CTX_reqExtensions_have_SAN(OSSL_CMP_CTX *ctx);
int OSSL_CMP_CTX_push0_policy(OSSL_CMP_CTX *ctx, POLICYINFO *pinfo);
int OSSL_CMP_CTX_set1_oldCert(OSSL_CMP_CTX *ctx, X509 *cert);
int OSSL_CMP_CTX_set1_p10CSR(OSSL_CMP_CTX *ctx, const X509_REQ *csr);
/* misc body contents: */
int OSSL_CMP_CTX_push0_genm_ITAV(OSSL_CMP_CTX *ctx, OSSL_CMP_ITAV *itav);
/* certificate confirmation: */
typedef int (*OSSL_cmp_certConf_cb_t) (OSSL_CMP_CTX *ctx, X509 *cert,
                                       int fail_info, const char **txt);
int OSSL_CMP_CTX_set_certConf_cb(OSSL_CMP_CTX *ctx, OSSL_cmp_certConf_cb_t cb);
int OSSL_CMP_CTX_set_certConf_cb_arg(OSSL_CMP_CTX *ctx, void *arg);
void *OSSL_CMP_CTX_get_certConf_cb_arg(const OSSL_CMP_CTX *ctx);
/* result fetching: */
int OSSL_CMP_CTX_get_status(const OSSL_CMP_CTX *ctx);
OSSL_CMP_PKIFREETEXT *OSSL_CMP_CTX_get0_statusString(const OSSL_CMP_CTX *ctx);
int OSSL_CMP_CTX_get_failInfoCode(const OSSL_CMP_CTX *ctx);
#  define OSSL_CMP_PKISI_BUFLEN 1024
X509 *OSSL_CMP_CTX_get0_newCert(const OSSL_CMP_CTX *ctx);
STACK_OF(X509) *OSSL_CMP_CTX_get1_caPubs(const OSSL_CMP_CTX *ctx);
STACK_OF(X509) *OSSL_CMP_CTX_get1_extraCertsIn(const OSSL_CMP_CTX *ctx);
int OSSL_CMP_CTX_set1_transactionID(OSSL_CMP_CTX *ctx,
                                    const ASN1_OCTET_STRING *id);
int OSSL_CMP_CTX_set1_senderNonce(OSSL_CMP_CTX *ctx,
                                  const ASN1_OCTET_STRING *nonce);

/* from cmp_status.c */
char *OSSL_CMP_CTX_snprint_PKIStatus(const OSSL_CMP_CTX *ctx, char *buf,
                                     size_t bufsize);
char *OSSL_CMP_snprint_PKIStatusInfo(const OSSL_CMP_PKISI *statusInfo, char *buf,
                                     size_t bufsize);
OSSL_CMP_PKISI *
OSSL_CMP_STATUSINFO_new(int status, int fail_info, const char *text);

/* from cmp_hdr.c */
ASN1_OCTET_STRING *OSSL_CMP_HDR_get0_transactionID(const OSSL_CMP_PKIHEADER *hdr);
ASN1_OCTET_STRING *OSSL_CMP_HDR_get0_recipNonce(const OSSL_CMP_PKIHEADER *hdr);

/* from cmp_msg.c */
OSSL_CMP_PKIHEADER *OSSL_CMP_MSG_get0_header(const OSSL_CMP_MSG *msg);
OSSL_CMP_MSG *OSSL_d2i_CMP_MSG_bio(BIO *bio, OSSL_CMP_MSG **msg);
int OSSL_i2d_CMP_MSG_bio(BIO *bio, const OSSL_CMP_MSG *msg);

/* from cmp_vfy.c */
int OSSL_CMP_validate_msg(OSSL_CMP_CTX *ctx, const OSSL_CMP_MSG *msg);
int OSSL_CMP_validate_cert_path(OSSL_CMP_CTX *ctx,
                                X509_STORE *trusted_store, X509 *cert);
int OSSL_CMP_print_cert_verify_cb(int ok, X509_STORE_CTX *ctx);

/*
 * from cmp_http.c
 */
/*
 * TODO dvo: push generic defs upstream with extended load_cert_crl_http(),
 * simplifying also other uses, e.g., in query_responder() in apps/ocsp.c
 */
#  if !defined(OPENSSL_NO_OCSP) && !defined(OPENSSL_NO_SOCK)
int OSSL_CMP_proxy_connect(BIO *bio, OSSL_CMP_CTX *ctx,
                           BIO *bio_err, const char *prog);
int OSSL_CMP_MSG_http_perform(OSSL_CMP_CTX *ctx, const OSSL_CMP_MSG *msg,
                              OSSL_CMP_MSG **out);
int OSSL_CMP_load_cert_crl_http_timeout(const char *url, int req_timeout,
                                        X509 **pcert, X509_CRL **pcrl,
                                        BIO *bio_err);
#  endif

/* from cmp_client.c */
X509 *OSSL_CMP_exec_IR_ses(OSSL_CMP_CTX *ctx);
X509 *OSSL_CMP_exec_CR_ses(OSSL_CMP_CTX *ctx);
X509 *OSSL_CMP_exec_P10CR_ses(OSSL_CMP_CTX *ctx);
X509 *OSSL_CMP_exec_KUR_ses(OSSL_CMP_CTX *ctx);
X509 *OSSL_CMP_exec_RR_ses(OSSL_CMP_CTX *ctx);
STACK_OF(OSSL_CMP_ITAV) *OSSL_CMP_exec_GENM_ses(OSSL_CMP_CTX *ctx);
int OSSL_CMP_certConf_cb(OSSL_CMP_CTX *ctx, X509 *cert, int fail_info,
                         const char **text);

/* from cmp_server.c */
typedef struct ossl_cmp_srv_ctx_st OSSL_CMP_SRV_CTX;
int OSSL_CMP_SRV_process_request(OSSL_CMP_SRV_CTX *srv_ctx,
                                 const OSSL_CMP_MSG *req,
                                 OSSL_CMP_MSG **rsp);
int OSSL_CMP_CTX_server_perform(OSSL_CMP_CTX *cmp_ctx, const OSSL_CMP_MSG *req,
                                OSSL_CMP_MSG **res);
OSSL_CMP_SRV_CTX *OSSL_CMP_SRV_CTX_new(void);
void OSSL_CMP_SRV_CTX_free(OSSL_CMP_SRV_CTX *srv_ctx);
typedef OSSL_CMP_PKISI *(*OSSL_CMP_SRV_cert_request_cb_t)
    (OSSL_CMP_SRV_CTX *srv_ctx, const OSSL_CMP_MSG *req, int certReqId,
     const OSSL_CRMF_MSG *crm, const X509_REQ *p10cr,
     X509 **certOut, STACK_OF(X509) **chainOut, STACK_OF(X509) **caPubs);
typedef OSSL_CMP_PKISI *(*OSSL_CMP_SRV_rr_cb_t)(OSSL_CMP_SRV_CTX *srv_ctx,
                                                const OSSL_CMP_MSG *req,
                                                const X509_NAME *issuer,
                                                const ASN1_INTEGER *serial);
typedef int (*OSSL_CMP_SRV_genm_cb_t)(OSSL_CMP_SRV_CTX *srv_ctx,
                                      const OSSL_CMP_MSG *req,
                                      const STACK_OF(OSSL_CMP_ITAV) *in,
                                      STACK_OF(OSSL_CMP_ITAV) **out);
typedef void (*OSSL_CMP_SRV_error_cb_t)(OSSL_CMP_SRV_CTX *srv_ctx,
                                        const OSSL_CMP_MSG *req,
                                        const OSSL_CMP_PKISI *statusInfo,
                                        const ASN1_INTEGER *errorCode,
                                        const OSSL_CMP_PKIFREETEXT *errorDetails);
typedef int (*OSSL_CMP_SRV_certConf_cb_t)(OSSL_CMP_SRV_CTX *srv_ctx,
                                          const OSSL_CMP_MSG *req,
                                          int certReqId,
                                          const ASN1_OCTET_STRING *certHash);
typedef int (*OSSL_CMP_SRV_pollReq_cb_t)(OSSL_CMP_SRV_CTX *srv_ctx,
                                         const OSSL_CMP_MSG *req, int certReqId,
                                         OSSL_CMP_MSG **certReq,
                                         int64_t *check_after);
int OSSL_CMP_SRV_CTX_init(OSSL_CMP_SRV_CTX *srv_ctx, void *custom_ctx,
                          OSSL_CMP_SRV_cert_request_cb_t process_cert_request,
                          OSSL_CMP_SRV_rr_cb_t process_rr,
                          OSSL_CMP_SRV_genm_cb_t process_genm,
                          OSSL_CMP_SRV_error_cb_t process_error,
                          OSSL_CMP_SRV_certConf_cb_t process_certConf,
                          OSSL_CMP_SRV_pollReq_cb_t process_pollReq);
OSSL_CMP_CTX *OSSL_CMP_SRV_CTX_get0_cmp_ctx(const OSSL_CMP_SRV_CTX *srv_ctx);
void *OSSL_CMP_SRV_CTX_get0_custom_ctx(const OSSL_CMP_SRV_CTX *srv_ctx);
int OSSL_CMP_SRV_CTX_set_send_unprotected_errors(OSSL_CMP_SRV_CTX *srv_ctx,
                                                 int val);
int OSSL_CMP_SRV_CTX_set_accept_unprotected(OSSL_CMP_SRV_CTX *srv_ctx, int val);
int OSSL_CMP_SRV_CTX_set_accept_raverified(OSSL_CMP_SRV_CTX *srv_ctx, int val);
int OSSL_CMP_SRV_CTX_set_grant_implicit_confirm(OSSL_CMP_SRV_CTX *srv_ctx,
                                                int val);

#  ifdef  __cplusplus
}
#  endif
# endif /* !defined OPENSSL_NO_CMP */
#endif /* !defined OPENSSL_CMP_H */
