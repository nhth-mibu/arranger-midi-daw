# Yamaha Playlists

A Playlist is a tab-separated file that points at Registration files:

```
TITLE	FILEPATH	REGIST	OPENVIEWER
A Walk In The Black Forest	I:/PSR-SX900 Playlist/Ballroom/A Walk In The Black Forest.rgt	0	OFF
```

`REGIST` is the Registration button number and `OPENVIEWER` is `ON` or `OFF`.
The reader and writer are in `arranger/core/Playlist.h`.
