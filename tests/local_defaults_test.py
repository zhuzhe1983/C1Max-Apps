#!/usr/bin/env python3
import json, pathlib, sys, tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from local_defaults import load_defaults
with tempfile.TemporaryDirectory() as tmp:
    p=pathlib.Path(tmp)/'private.json'
    good={'schema':1,'streamplayer':{'base':'https://media.example.test/emby','type':'Emby','username':'fixture','password':'fixture-password'},'crosspoint':{'url':'https://books.example.test/opds','user':'','password':''}}
    p.write_text(json.dumps(good));assert load_defaults(p)==good
    for bad in [[], {'schema':2}, {'schema':1,'token':'not-allowed'}, {'schema':1,'streamplayer':{'token':'not-allowed'}}, {'schema':1,'streamplayer':{'base':'https://user:password@example.test'}}, {'schema':1,'streamplayer':{'base':'file:///tmp/book'}}, {'schema':1,'streamplayer':{'type':'invalid'}}, {'schema':1,'crosspoint':{'url':'https://example.test/\nx'}}, {'schema':1,'crosspoint':{'url':34}}, {'schema':1,'streamplayer':{'password':'x'*2049}}]:
        p.write_text(json.dumps(bad))
        try:load_defaults(p)
        except ValueError:pass
        else:raise AssertionError('Invalid private config accepted')
    p.write_text('x'*16385)
    try:load_defaults(p)
    except ValueError:pass
    else:raise AssertionError('Oversized defaults accepted')
print('Deployment defaults validation passed; credentials/tokens cannot enter undeclared fields')
