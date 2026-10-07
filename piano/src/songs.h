#ifndef C1_PIANO_SONGS_H
#define C1_PIANO_SONGS_H
/* Original compact melody arrangements of public-domain tunes; no recordings. */
typedef struct { unsigned char midi,units; } SongNote;
typedef struct { const char *title; const SongNote *notes; unsigned count,unit_ms; } Song;
#define N(m,u) {m,u}
static const SongNote ode[]={N(64,2),N(64,2),N(65,2),N(67,2),N(67,2),N(65,2),N(64,2),N(62,2),N(60,2),N(60,2),N(62,2),N(64,2),N(64,3),N(62,1),N(62,4),N(64,2),N(64,2),N(65,2),N(67,2),N(67,2),N(65,2),N(64,2),N(62,2),N(60,2),N(60,2),N(62,2),N(64,2),N(62,3),N(60,1),N(60,4)};
static const SongNote twinkle[]={N(60,2),N(60,2),N(67,2),N(67,2),N(69,2),N(69,2),N(67,4),N(65,2),N(65,2),N(64,2),N(64,2),N(62,2),N(62,2),N(60,4),N(67,2),N(67,2),N(65,2),N(65,2),N(64,2),N(64,2),N(62,4),N(67,2),N(67,2),N(65,2),N(65,2),N(64,2),N(64,2),N(62,4),N(60,2),N(60,2),N(67,2),N(67,2),N(69,2),N(69,2),N(67,4),N(65,2),N(65,2),N(64,2),N(64,2),N(62,2),N(62,2),N(60,4)};
static const SongNote jacques[]={N(60,2),N(62,2),N(64,2),N(60,2),N(60,2),N(62,2),N(64,2),N(60,2),N(64,2),N(65,2),N(67,4),N(64,2),N(65,2),N(67,4),N(67,1),N(69,1),N(67,1),N(65,1),N(64,2),N(60,2),N(67,1),N(69,1),N(67,1),N(65,1),N(64,2),N(60,2),N(60,2),N(67,2),N(60,4),N(60,2),N(67,2),N(60,4)};
static const SongNote elise[]={N(76,1),N(75,1),N(76,1),N(75,1),N(76,1),N(71,1),N(74,1),N(72,1),N(69,3),N(0,1),N(60,1),N(64,1),N(69,1),N(71,3),N(0,1),N(64,1),N(68,1),N(71,1),N(72,3),N(0,1),N(64,1),N(76,1),N(75,1),N(76,1),N(75,1),N(76,1),N(71,1),N(74,1),N(72,1),N(69,3),N(0,1),N(60,1),N(64,1),N(69,1),N(71,3),N(0,1),N(64,1),N(72,1),N(71,1),N(69,4)};
#define SONG(a,t,ms) {t,a,sizeof(a)/sizeof(a[0]),ms}
static const Song songs[]={SONG(ode,"ODE TO JOY",250),SONG(elise,"FUR ELISE - THEME",260),SONG(twinkle,"TWINKLE TWINKLE",230),SONG(jacques,"FRERE JACQUES",230)};
#define SONG_COUNT ((int)(sizeof songs/sizeof songs[0]))
static inline int song_duration(int song){int n=0;for(unsigned i=0;i<songs[song].count;i++)n+=songs[song].notes[i].units*songs[song].unit_ms;return n;}
static inline int song_note(int song,long long ms){if(song<0||song>=SONG_COUNT||ms<0)return 0;for(unsigned i=0;i<songs[song].count;i++){int d=songs[song].notes[i].units*songs[song].unit_ms;if(ms<d)return ms*100<d*88?songs[song].notes[i].midi:0;ms-=d;}return 0;}
#undef N
#undef SONG
#endif
