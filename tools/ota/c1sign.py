#!/usr/bin/env python3
"""
C1 Max recovery update.zip signer/verifier replica.
Algorithm reverse-engineered from /usr/sbin/recovery (verify_file @ 0x4145b0,
load_keys @ 0x4157ec, RSA_verify @ 0x4c4c90, mincrypt RSA_e_f4_verify).

Layout of a signed zip:
  [zip bytes incl. EOCD][signature comment][6-byte footer]
  footer = pack('<H', total) + b'\xff\xff' + pack('<H', total)
  total  = len(signature_comment) + 6
Signed bytes = zip[0 : eocd_offset + 20]   (EOCD comment-length field NOT covered)
Signature = RSA-2048 PKCS#1 v1.5 over SHA-1, placed as the LAST 256 bytes
of the comment (i.e. right before the footer). Anything before it is ignored.
Constraint: bytes 'PK\x05\x06' must not appear in comment[0:-6] region scanned
(offsets 4..eocd_size-3 of eocd+comment); a random 256-byte sig hits this with
prob ~6e-8 - deterministic, so re-roll by tweaking zip content if ever hit.
"""
import struct, hashlib, sys

SHA1_DER_PREFIX = bytes.fromhex('3021300906052b0e03021a05000414')

def load_v2_key(path):
    import re
    txt = open(path).read().strip()
    m = re.match(r'v(\d+)\s*\{(\d+),0x([0-9a-fA-F]+),\{([0-9,\s]+)\},\{([0-9,\s]+)\}\}', txt)
    if not m: raise ValueError('bad key format')
    ver, ln = int(m.group(1)), int(m.group(2))
    if ver != 2: raise ValueError('only v2 (e=65537) supported')
    mod = [int(x) for x in m.group(4).split(',')]
    n = sum(w << (32*i) for i, w in enumerate(mod))
    return n, 65537

def emsa_pkcs1_v15_encode(digest, em_len=256):
    T = SHA1_DER_PREFIX + digest
    ps = b'\xff' * (em_len - len(T) - 3)
    return b'\x00\x01' + ps + b'\x00' + T

def rsa_sign(em, d, n):
    return pow(int.from_bytes(em, 'big'), d, n).to_bytes(256, 'big')

def rsa_verify_sig(sig, digest, e, n):
    em = pow(int.from_bytes(sig, 'big'), e, n).to_bytes(256, 'big')
    return em == emsa_pkcs1_v15_encode(digest)

def find_eocd(blob):
    # zip may have no comment in our workflow; scan backwards for PK\x05\x06
    i = blob.rfind(b'PK\x05\x06')
    if i < 0: raise ValueError('no EOCD')
    return i

def signed_region_len(blob):
    # mimic verifier: footer last 6 bytes, comment_size = footer[4..5]
    footer = blob[-6:]
    assert footer[2] == 0xff and footer[3] == 0xff, 'no footer'
    comment_size = footer[4] | (footer[5] << 8)
    eocd_off = len(blob) - comment_size - 22
    assert blob[eocd_off:eocd_off+4] == b'PK\x05\x06', 'EOCD mismatch'
    return eocd_off + 20, comment_size, eocd_off

def sign_zip(zip_path, out_path, d, n):
    blob = open(zip_path, 'rb').read()
    eocd = find_eocd(blob)
    signed = blob[:eocd + 20]          # includes EOCD header except comment-len field
    digest = hashlib.sha1(signed).digest()
    sig = rsa_sign(emsa_pkcs1_v15_encode(digest), d, n)
    comment = sig                       # cert etc. could precede; sig must be last 256
    total = len(comment) + 6
    footer = struct.pack('<H', total) + b'\xff\xff' + struct.pack('<H', total)
    if b'PK\x05\x06' in comment:
        raise ValueError('signature contains EOCD magic - tweak zip and retry')
    open(out_path, 'wb').write(blob + comment + footer)
    return digest.hex()

def verify_zip(path, key_n, key_e=65537):
    blob = open(path, 'rb').read()
    sig_len, comment_size, eocd_off = signed_region_len(blob)
    digest = hashlib.sha1(blob[:sig_len]).digest()
    sig = blob[eocd_off + 22 + (comment_size - 6) - 256 : eocd_off + 22 + (comment_size - 6)]
    assert len(sig) == 256
    # EOCD-magic scan over eocd[4:] + comment (as device does)
    region = blob[eocd_off + 4 : eocd_off + 22 + comment_size]
    if region.find(b'PK\x05\x06') >= 0:
        return False, 'EOCD marker occurs after start of EOCD'
    return rsa_verify_sig(sig, digest, key_e, key_n), digest.hex()

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'verify':
        n, e = load_v2_key(sys.argv[3])
        ok, info = verify_zip(sys.argv[2], n, e)
        print('VERIFY', 'PASS' if ok else 'FAIL', info)
    elif cmd == 'mkkey':
        # read PEM PKCS#1/DER via openssl beforehand; here accept n from args file
        raise SystemExit('use mkkey.sh')
