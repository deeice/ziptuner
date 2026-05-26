#!/bin/sh

cd /usr/local/share/ziptuner

# First, put up a dialog to ask about auto-resume.  Then launch ziptuner.
if [ -f ziptuner.fav ] ; then
  LANG=C DIALOGRC=/usr/local/share/ziptuner/dialogrc.soho dialog --clear --title "Zippy Internet Radio Tuner" --yesno "Resume Autoplay?" 0 0
  [ $? -eq 0 ] && A="-a" || A=
fi

# Set up a pipe and a tail process to run sed filter on ziptuner.log for tailbox dialog in ziptuner.
#touch /tmp/ziptuner.log  # Could echo >ziptuner.log to restart from scratch.
{ echo; date; echo "===== ZIPTUNER STARTUP ====="; } >>/tmp/ziptuner.log 2>&1

tail -10000 -f /tmp/ziptuner.log | sed -u -e "/meta.*StreamTitle/I!d" -e "s/\r//g" -e "s/.*StreamTitle..//" -e "s/\x27;.*//" >/tmp/logpipe &
SED_PID=$!

# Get the Group leader PID of the entire tail pipeline. 
jobs -p %+ > /tmp/ziptuner.logpid
read -r TAILPIPE_PID < /tmp/ziptuner.logpid

LANG=C DIALOGRC=/usr/local/share/ziptuner/dialogrc.soho ziptuner $A -u \
 -p '{ echo; date; exec ffmpeg -hide_banner -sample_fmt s16 -icy 1 -i %s -f oss /dev/dsp -nostats -v 40 ; } >> /tmp/ziptuner.log 2>&1' \
 -s 'killall -2 ffmpeg 2>/dev/null' \
 -l /tmp/logpipe \
 -x rexima \
 /usr/share/radio /mnt/sd0/gmu/playlist.m3u 

# Cleanup.  -- stops option parsing.  -PID treats PID as the entire Process Group ID (PGID) 
#kill -- "-$TAILPIPE_PID"
kill $TAILPIPE_PID $SED_PID >/dev/null 2>&1
rm /tmp/ziptuner.logpid
rm /tmp/logpipe

# 4. Wait for ALL background jobs to finish and get reaped
wait
