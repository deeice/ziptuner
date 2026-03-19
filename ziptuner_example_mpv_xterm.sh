
# First, put up a dialog to ask about auto-resume.  Then launch ziptuner.
if [ -f ziptuner.fav ] ; then
  dialog --clear --title "Zippy Internet Radio Tuner" --yesno "Resume playing favorite?" 0 0
  [ $? -eq 0 ] && A="-a" || A=
fi


# Use mpv in a small xterm window for stream info.  Use a multistream playlist for favorites. 
./ziptuner $A -p "urxvt -fg grey80 -bg grey30 -fn fixed -e mpv  " -s "killall mpv" playlist.pls
