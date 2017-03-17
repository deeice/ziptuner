#!/bin/sh

cd /usr/share/ziptuner
TERM=xterm-color DIALOGRC=/usr/share/ziptuner/dialogrc.soho ziptuner -p "mplayertty4 -playlist " -s "killall mpg123; killall mplayer" /root/radio /root/.local/share/gmu/playlist.m3u
