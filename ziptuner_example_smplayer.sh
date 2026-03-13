
# First, put up a dialog to ask about auto-resume.  Then launch ziptuner.
if [ -f ziptuner.fav ] ; then
  dialog --clear --title "Zippy Internet Radio Tuner" --yesno "Resume playing favorite?" 0 0
  [ $? -eq 0 ] && A="-a" || A=
fi


# Use smplayer and a multistream playlist for storage.  (not headless has volume ctrl)
./ziptuner $A -p "smplayer >/dev/null 2>&1 " -s "killall smplayer; killall mpv" playlist.pls
