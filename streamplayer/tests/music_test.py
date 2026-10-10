import http.server,json,threading,subprocess,sys,urllib.parse
requests=[]
sessions=[]
stops=[]
reports=[]
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_POST(self):
        query=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if self.path.startswith('/Sessions/Playing'):
            reports.append((data,self.headers['X-Emby-Authorization']))
            self.send_response(204);self.end_headers();return
        profile=data['DeviceProfile'];tv=profile['Name']=='C1Max TV 720p'
        assert profile['TranscodingProfiles'][0]['Protocol']==('hls' if tv else 'http')
        assert data['MaxStreamingBitrate']==(2628000 if tv else 464000)
        assert query['SubtitleStreamIndex']==(['2'] if tv else ['-1'])
        assert query['SubtitleMethod']==(['Encode'] if tv else ['External'])
        assert query['MaxWidth']==(['1280'] if tv else ['400'])
        device='c1max-streamplayer-tv' if tv else 'c1max-streamplayer'
        assert query['DeviceId']==[device] and f'DeviceId="{device}"' in self.headers['X-Emby-Authorization']
        session='session-tv' if tv else 'session-local';sessions.append(session)
        body={'PlaySessionId':session,'MediaSources':[{'Id':'source','SupportsTranscoding':True,'RunTimeTicks':600000000,'MediaStreams':[{'Type':'Subtitle','Index':2,'Codec':'srt','DisplayTitle':'中文'}]}]}
        output=json.dumps(body).encode();self.send_response(200);self.send_header('Content-Length',str(len(output)));self.end_headers();self.wfile.write(output)
    def do_DELETE(self):
        q=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        stops.append(q);self.send_response(204);self.end_headers()
    def do_GET(self):
        u=urllib.parse.urlsplit(self.path);q=urllib.parse.parse_qs(u.query);requests.append((u.path,q,self.headers.get('X-Emby-Token')))
        if u.path.endswith('/Views'):body={'Items':[{'Id':'music','Name':'Music','CollectionType':'music'},{'Id':'films','Name':'Movies','CollectionType':'movies'}]}
        elif u.path.endswith('/Items') and q.get('SearchTerm')==['完全不存在']:body={'Items':[],'TotalRecordCount':0}
        elif u.path.endswith('/Items'):body={'Items':[{'Id':'track','Name':'Song','Type':'Audio','Album':'Original music','AlbumArtist':'Fixture','RunTimeTicks':1200000000}],'TotalRecordCount':4}
        elif u.path.startswith('/Audio/'):
            assert q['api_key']==['fixture + token'] and q['AudioSampleRate']==['44100'] and q['Static']==['false']
            body=b'ID3fixture'
        else:self.send_error(404);return
        data=body if isinstance(body,bytes) else json.dumps(body).encode();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
s=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=s.serve_forever,daemon=True).start()
try:
    subprocess.run([sys.argv[1],f'http://127.0.0.1:{s.server_port}'],check=True,timeout=15)
    assert sessions==['session-local','session-tv']
    assert stops==[{'DeviceId':['c1max-streamplayer'],'PlaySessionId':['session-local']},{'DeviceId':['c1max-streamplayer-tv'],'PlaySessionId':['session-tv']}]
    assert reports[0][0]['PlaySessionId']=='session-local' and 'DeviceId="c1max-streamplayer"' in reports[0][1]
    assert reports[1][0]['PlaySessionId']=='session-tv' and 'DeviceId="c1max-streamplayer-tv"' in reports[1][1] and reports[1][0]['IsPaused']
    assert requests[1][1]['IncludeItemTypes']==['Audio'] and requests[1][1]['StartIndex']==['3'] and requests[1][1]['Limit']==['128']
    assert requests[2][1]['IncludeItemTypes']==['Movie,Episode,Video'] and requests[2][1]['Limit']==['3']
    assert requests[3][1]['SearchTerm']==['北京 & 海'] and requests[3][1]['ParentId']==['中文 / &'] and requests[3][1]['StartIndex']==['6']
    assert requests[4][1]['SearchTerm']==['音乐'] and requests[4][1]['IncludeItemTypes']==['Audio'] and requests[4][1]['Limit']==['128']
    assert 'SearchTerm' not in requests[2][1]
    assert all(r[2]=='fixture + token' for r in requests[:6])
finally:s.shutdown();s.server_close()
