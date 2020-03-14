
# Use headless gmu server to play, and a multistream playlist for storage.
# The stop cmd could be improved to stop playing, but leave the server running.

./ziptuner -p "SDL_AUDIODRIVER=alsa LD_LIBRARY_PATH=libs.z2/ ./gmu -c gmu.zipit-z2.conf -p frontends.old/gmusrv.so $@ >/dev/null 2>&1 " -s "killall -9 gmu" playlist.m3u
