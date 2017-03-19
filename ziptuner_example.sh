#!/bin/sh

# With a dark and edgy theme for the dialogs:
# Go to the ziptuner directory and run ziptuner,
# using mpg123 on an open vt to play mp3 streams,
# or mplayer on an open vt to play anything else.
# Use killall to stop any of the players.
# Save individual URL files to the ~/radio directory.
# Also append URLS to the gmu playlist file.

cd /usr/share/ziptuner

DIALOGRC=/usr/share/ziptuner/dialogrc.soho ziptuner -mp3 "mpg123tty4 -@ " -p "mplayertty4 -playlist " -s "killall mpg123; killall mplayer" ~/radio ~/.config/gmu/playlist.m3u
