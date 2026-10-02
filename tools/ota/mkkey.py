#!/usr/bin/env python3
"""Convert an RSA-2048 public key (n in hex file or via openssl) to recovery v2 format."""
import sys, subprocess, re

def from_pem(pem):
    # openssl prints modulus
    out = subprocess.check_output(['openssl', 'rsa', '-pubin', '-in', pem, '-text', '-noout'])
    txt = out.decode()
    m = re.search(r'Modulus:\s*((?:\s*[0-9a-f]{2}:?)+)', txt)
    hexs = re.sub(r'[\s:]', '', m.group(1))
    n = int(hexs, 16)
    e = int(re.search(r'Exponent: (\d+)', txt).group(1))
    return n, e

def to_v2(n, e):
    assert e == 65537, 'device expects e=65537 (v2)'
    ln = 64
    assert n.bit_length() <= 32 * ln
    n0 = n & 0xffffffff
    n0inv = (-pow(n0, -1, 1 << 32)) & 0xffffffff
    R = 1 << (32 * ln)
    rr = (R * R) % n
    mod = [(n >> (32 * i)) & 0xffffffff for i in range(ln)]
    rrw = [(rr >> (32 * i)) & 0xffffffff for i in range(ln)]
    return 'v2 {%d,0x%08x,{%s},{%s}}' % (
        ln, n0inv, ','.join(map(str, mod)), ','.join(map(str, rrw)))

if __name__ == '__main__':
    n, e = from_pem(sys.argv[1])
    open(sys.argv[2], 'w').write(to_v2(n, e) + '\n')
    print('wrote', sys.argv[2])
