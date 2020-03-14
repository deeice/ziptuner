
# Use headless vlc (dummy interface) and a multistream playlist for storage. 
./ziptuner -p "vlc -I dummy -q >/dev/null 2>&1 " -s "killall vlc" playlist.pls
