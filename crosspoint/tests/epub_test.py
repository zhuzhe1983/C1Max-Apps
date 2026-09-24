#!/usr/bin/env python3
"""Exercise the real native ZIP reader with generated EPUBs, including lazy faults."""
import pathlib, struct, subprocess, sys, tempfile, zipfile
probe = str(pathlib.Path(sys.argv[1]).resolve())
container = '<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>'
opf = '''<opf:package xmlns:opf="http://www.idpf.org/2007/opf"><opf:manifest>
<opf:item id="a" href="Text/../Text/one%20a.xhtml#start" media-type="application/xhtml+xml"/>
<opf:item id="b" href="Text/two.xhtml" media-type="application/xhtml+xml"/>
<opf:item id="skip" href="skip.xhtml" media-type="application/xhtml+xml"/>
</opf:manifest><opf:spine><opf:itemref idref="b"/><opf:itemref idref="skip" linear="no"/><opf:itemref idref="a"/></opf:spine></opf:package>'''
base = {'META-INF/container.xml': container, 'OPS/book.opf': opf,
        'OPS/Text/one a.xhtml': '<html><head><title>Not body</title></head><body><p>第一章 &amp; 汉字</p></body></html>',
        'OPS/Text/two.xhtml': '<html><body><p>SECOND</p><p>line 2</p></body></html>'}

def write(path, entries, method=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, 'w', method) as z:
        z.comment = b'comment PK\x05\x06, not an EOCD'
        for name, data in entries.items(): z.writestr(name, data)

def run(path, index=0, expected=None, error=None, mode='text'):
    r = subprocess.run([probe, str(path), str(index), mode], capture_output=True, text=True, timeout=20)
    if error:
        assert r.returncode == 1 and error in r.stderr, (path.name, r.returncode, r.stdout, r.stderr)
    else:
        assert r.returncode == 0, (path.name, r.stdout, r.stderr)
        if expected is not None: assert r.stdout.partition('\n')[2] == expected, repr(r.stdout)
    return r.stdout

def central_patch(path, name, offset, fmt, value):
    b = bytearray(path.read_bytes()); at=0
    while True:
        at=b.index(b'PK\x01\x02', at)
        length=struct.unpack_from('<H', b, at+28)[0]
        if b[at+46:at+46+length].decode() == name:
            struct.pack_into(fmt,b,at+offset,value);path.write_bytes(b);return
        at+=46+length

with tempfile.TemporaryDirectory(prefix='crosspoint-epub-') as tmp:
    p=pathlib.Path(tmp)/'test.epub'
    for method in [zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED]:
        write(p,base,method)
        assert 'chapters=2 ' in run(p,0,'\nSECOND\nline 2')
        run(p,1,'\n第一章 & 汉字')
        run(p,2,error='Invalid EPUB chapter index')
        run(p,error='Cancelled',mode='cancel')
    # Corruption of a later chapter must not prevent opening the first one.
    write(p,base);central_patch(p,'OPS/Text/one a.xhtml',16,'<I',0)
    run(p,0,'\nSECOND\nline 2');run(p,1,error='checksum')
    for name,offset,fmt,value,error in [
        ('OPS/Text/two.xhtml',8,'<H',1,'Encrypted'),
        ('OPS/Text/two.xhtml',24,'<I',2*1024*1024+1,'size limit'),
        ('OPS/Text/two.xhtml',20,'<I',8*1024*1024+1,'size limit'),
        ('META-INF/container.xml',42,'<I',0xffffff00,'Truncated'),
    ]:
        write(p,base);central_patch(p,name,offset,fmt,value);run(p,error=error)
    write(p,base);p.write_bytes(p.read_bytes()[:-10]);run(p,error='directory is missing')
    bad=dict(base);bad['META-INF/container.xml']='<container>';write(p,bad);run(p,error='metadata XML')
    bad=dict(base);bad['OPS/book.opf']=opf.replace('Text/two.xhtml','../../escape.xhtml');write(p,bad);run(p,error='escapes')
    bad=dict(base);bad['OPS/book.opf']=opf.replace('Text/two.xhtml','https://example.org/chapter');write(p,bad);run(p,error='Unsafe')
    bad=dict(base);bad['OPS/book.opf']=opf.replace('Text/two.xhtml','missing.xhtml');write(p,bad);run(p,error='missing a referenced chapter')
    # The old regex spine parser crashed on the user's 1743-entry book.
    many=dict(base);many['OPS/book.opf']='<package><manifest>'+''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(1750))+'</manifest><spine>'+''.join(f'<itemref idref="c{i}"/>' for i in range(1750))+'</spine></package>'
    for i in range(1750):many[f'OPS/c{i}.xhtml']=f'<body>Chapter {i}</body>'
    write(p,many);assert 'chapters=1750 ' in run(p,0,'Chapter 0');run(p,1749,'Chapter 1749')
print('EPUB stored/deflate, CRC, bounds, cancellation, URI/spine and 1750-chapter lazy-read tests passed')
