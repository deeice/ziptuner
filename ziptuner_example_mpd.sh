
# First, put up a dialog to ask about auto-resume.  Then launch ziptuner.
if [ -f ziptuner.fav ] ; then
  dialog --clear --title "Zippy Internet Radio Tuner" --yesno "Resume playing favorite?" 0 0
  [ $? -eq 0 ] && A="-a" || A=
fi


# Use mpc frontend for mpd player, and use a multistream playlist for favorites. 
# '%s' tells ziptuner to insert the url there instead the end of the play string.
./ziptuner $A -p "(mpc add '%s' && mpc play) " -s "mpc -q clear" playlist.pls
