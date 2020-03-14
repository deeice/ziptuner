
# Use headless gmu server to play, and a multistream playlist for storage.
# The stop cmd could be improved to stop playing, but leave the server running.

#./ziptuner -p "SDL_AUDIODRIVER=alsa LD_LIBRARY_PATH=libs.z2/ ./gmu -c gmu.zipit-z2.conf -p frontends.old/gmusrv.so $@ >/dev/null 2>&1 " -s "gmu-cli x" playlist.m3u

# gmusrv, gmu-cli are deprecated in gmu 10 so use fake frontend (or try gmuhttp)
./ziptuner -p "SDL_AUDIODRIVER=alsa LD_LIBRARY_PATH=libs.z2/ ./gmu -c gmu.zipit-z2.conf -p dummy $@ >/dev/null 2>&1 " -s "killall -9 gmu" playlist.m3u
