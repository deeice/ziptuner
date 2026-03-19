
# First, put up a dialog to ask about auto-resume.  Then launch ziptuner.
if [ -f ziptuner.fav ] ; then
  dialog --clear --title "Zippy Internet Radio Tuner" --yesno "Resume playing favorite?" 0 0
  [ $? -eq 0 ] && A="-a" || A=
fi


# Use headless mpv player, and use a multistream playlist for favorites. 
./ziptuner $A -p "mpv --terminal=no >/dev/null 2>&1 " -s "killall mpv" playlist.pls
