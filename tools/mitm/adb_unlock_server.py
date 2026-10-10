#!/usr/bin/env python3
"""Fake api.mpen.com.cn that approves ADB debug authorization (adbAdmit).

Unlock path for a stock C1 Max (see docs/firmware-ota.md §0):
the stock About page's hidden debug gesture makes the device GET
https://api.mpen.com.cn/v1/pens/{penId}?action=adbAdmit and enables ADB
when the JSON reply contains "success": true. The vendor server refuses
(vendor defunct), so we answer it ourselves:

1. Run this server on a LAN machine:  sudo python3 adb_unlock_server.py
   (binds 443 with a self-signed cert for api.mpen.com.cn; the device does
   not verify TLS — verified on device 2026-10-02)
2. On the router, point api.mpen.com.cn at this machine (OpenWrt/dnsmasq:
   address=/api.mpen.com.cn/<this machine's IP>)
3. On the device: 设置 → 关于 → tap the 系统版本 row >=10 times within 5s.
   Toast 「调试菜单已开放-D」 means the request is in flight; when the fake
   answer lands, enable_adb.sh runs and ADB appears (replug USB if needed).

Everything not adbAdmit is transparently proxied to the real server so the
rest of the device keeps working while the hijack is in place. All request
paths are logged (this is also how you learn a device's real penId).
"""
import datetime
import http.server
import os
import ssl
import subprocess
import sys
import tempfile
import urllib.request

REAL_IP = os.environ.get('MPEN_REAL_IP', '39.98.109.39')
LISTEN = int(os.environ.get('MPEN_LISTEN_PORT', '443'))
LOG = os.environ.get('MPEN_LOG', '/tmp/mpen-mitm-requests.log')


def log(msg):
    line = '%s %s\n' % (datetime.datetime.now().isoformat(), msg)
    with open(LOG, 'a') as f:
        f.write(line)
    sys.stderr.write(line)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def _handle(self):
        log('REQ %s %s Host=%s' % (self.command, self.path,
                                   self.headers.get('Host')))
        if 'adbAdmit' in self.path:
            body = b'{"success":true}'
            log('  -> FAKED adbAdmit approval')
        else:
            try:
                ctx = ssl.create_default_context()
                ctx.check_hostname = False
                ctx.verify_mode = ssl.CERT_NONE
                req = urllib.request.Request(
                    'https://%s%s' % (REAL_IP, self.path),
                    headers={'Host': 'api.mpen.com.cn'})
                body = urllib.request.urlopen(req, context=ctx,
                                              timeout=10).read()
                log('  -> proxied %d bytes' % len(body))
            except Exception as e:
                body = b'{"errorCode":"500","errorMsg":"mitm proxy error"}'
                log('  -> proxy error: %s' % e)
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    do_GET = _handle
    do_POST = _handle

    def log_message(self, *args):
        pass


def make_cert(directory):
    os.makedirs(directory, exist_ok=True)
    cert = os.path.join(directory, 'cert.pem')
    key = os.path.join(directory, 'key.pem')
    if not os.path.exists(cert):
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048',
                        '-nodes', '-keyout', key, '-out', cert, '-days', '3650',
                        '-subj', '/CN=api.mpen.com.cn',
                        '-addext', 'subjectAltName=DNS:api.mpen.com.cn'],
                       check=True, capture_output=True)
    return cert, key


def main():
    cert, key = make_cert(tempfile.gettempdir() + '/mpen-mitm')
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    server = http.server.ThreadingHTTPServer(('0.0.0.0', LISTEN), Handler)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    log('=== mpen adbAdmit unlock server on :%d (real=%s) ==='
        % (LISTEN, REAL_IP))
    server.serve_forever()


if __name__ == '__main__':
    main()
