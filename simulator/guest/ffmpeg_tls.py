#!/usr/bin/env python3
"""为固定 FFmpeg 6.1.5 的 OpenSSL 后端补齐客户端主机名/IP 校验。"""
import pathlib
import sys

path = pathlib.Path(sys.argv[1]) / "libavformat/tls_openssl.c"
text = path.read_text()
marker = "/* C1SIM_TLS_HOST_VERIFY */"
if marker not in text:
    old = "    SSL_set_bio(p->ssl, bio, bio);\n"
    new = """    /* C1SIM_TLS_HOST_VERIFY */
    if (c->verify && !c->listen) {
        int ok = c->numerichost
            ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(p->ssl), c->host)
            : SSL_set1_host(p->ssl, c->host);
        if (!ok) {
            BIO_free(bio);
            ret = AVERROR(EINVAL);
            goto fail;
        }
    }
    SSL_set_bio(p->ssl, bio, bio);
"""
    if text.count(old) != 1:
        raise SystemExit("固定 FFmpeg TLS 源码与预期不符，拒绝静默跳过主机名校验。")
    path.write_text(text.replace(old, new))
