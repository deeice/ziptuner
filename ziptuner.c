#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"

// ------------ Setup --------------------------

#define CMD_OUT_MAX 1024


int  destnum = 1;
char *destdir = "."; // Default playlist save location (if none specified) is here.
char **dest = &destdir;
char *destfile = ".";
char srch_url[512] = "";
char srch_str[512] = "";
char pls_url[512] = "";

/*
First get a list to choose from.
http://www.radio-browser.info/webservice/json/tags
http://www.radio-browser.info/webservice/json/stations/bytag

http://www.radio-browser.info/webservice/json/countries
http://www.radio-browser.info/webservice/json/stations/bycountry/searchterm 

http://www.radio-browser.info/webservice/json/states/USA/
http://www.radio-browser.info/webservice/json/stations/bystate/searchterm 

http://www.radio-browser.info/webservice/json/languages
http://www.radio-browser.info/webservice/json/stations/bylanguage/searchterm 

http://www.radio-browser.info/webservice/json/stations/byname/searchterm 

http://www.radio-browser.info/webservice/json/codecs 
http://www.radio-browser.info/webservice/json/stations/bycodec/searchterm 

http://www.radio-browser.info/webservice/v2/pls/url/nnnnn
http://www.radio-browser.info/webservice/v2/m3u/url/nnnnn
*/

// ---------------------------------------------


struct MemoryStruct {
  char *memory;
  size_t size;
};

struct ifreq ifr_eth;
struct ifreq ifr_wlan;
int int_connection=0;

int width, height;

char cmd_out[CMD_OUT_MAX];  
char *cmd = cmd_out;

char ext[32] = ".m3u";

FILE *fp;
char buff[256];

char *play = NULL; // mpg123tty4
char *stop = NULL; // killall mpg123; killall mplayer
char *codecs[16];
char *players[16];
int np = 0;
int choice = 0;
int U2L = 0;
int previtem = 0;
	
/************************************************/
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
  mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);
  if(mem->memory == NULL) {
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
  return realsize;
}

/************************************************/
void get_ifaddress(char *intName,struct ifreq *ifr) {
 int fd; 
 fd = socket(AF_INET, SOCK_DGRAM, 0);
 ifr->ifr_addr.sa_family = AF_INET;
 strncpy(ifr->ifr_name, intName, IFNAMSIZ-1);
 ioctl(fd, SIOCGIFADDR, ifr);
 close(fd);
 //printf("%s\n", inet_ntoa(((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr));
}

/************************************************/
void get_int_ip() {
 int_connection=0;
 bzero(&ifr_eth,sizeof(ifr_eth)); 
 get_ifaddress((char *)"eth0",&ifr_eth);
 bzero(&ifr_wlan,sizeof(ifr_wlan)); 
 get_ifaddress((char *)"wlan0",&ifr_wlan);
 if (
   ((struct sockaddr_in *)&ifr_eth.ifr_addr)->sin_addr.s_addr!=0 ||
   ((struct sockaddr_in *)&ifr_wlan.ifr_addr)->sin_addr.s_addr!=0
  ) {
  int_connection=1;
 }
}


/************************************************/
char *utf8tolatin(char *s) {
  char *p=s;
  for (; *s; s++) {
    if (((*s & 0xFC) == 0xC0) && ((*(s+1) & 0xC0) == 0x80))
      *p++ = ((*s & 0x03) << 6) | (*++s & 0x3F);
    else 
      *p++ = *s;
  }
  *p = 0;
}

/************************************************/
void gotnone(void) {
  //printf("messagebox Got nothing\n");
  cmd = cmd_out;
  sprintf(cmd, "dialog --clear --title \"Sorry.\" ");
  strcat(cmd,"--backtitle \"ziptuner\" ");
  strcat(cmd,"--msgbox \"None Found.\"");
#if 0
  sprintf(cmd+strlen(cmd)," %d %d", 6, 20);
#else
  sprintf(cmd+strlen(cmd)," %d %d", 8, 20);
#endif
  system ( cmd ) ;
}

/************************************************/
void playit(char * item_url, char *codec)
{
  int j;
  char *playcmd = play; // Start with default play command.
  
  // If we know the codec then check for a matching play command.
  if (codec != NULL)
  {
    for (j=0; j<strlen(codec); j++)
      codec[j] = tolower(codec[j]);
    // Do strncmp on codecs so aac can cover both aac and aac+
    // Also strncmp("", s, 0) will  match anything so we can have a fallback codec.
    for (j=0; j<np; j++) { // look for a codec match in the list of players.
      if (!strncmp(codecs[j], codec, strlen(codecs[j]))){
	playcmd = players[j];
	break;
      }
    }
  }

  // Relocate playcmd into buff so we can add playlist (or url) before launch.
  strcpy(buff, playcmd);
  playcmd = buff;
  
  // If its a stream and not a playlist then...
  if (!(strstr(item_url,".m3u") || strstr(item_url,".pls"))) {
    char *p = strstr(playcmd,"-@");      // remove mpg123 arg that says its a playlist.
    if (p) 
      for (j=0; j<2; j++) p[j] = ' '; 
    if (p = strstr(playcmd,"-playlist")) // remove mplayer arg that says its a playlist.
      for (j=0; j<9; j++) p[j] = ' '; 
  }
  
  //printf ("\n%s \"%s\"\n", playcmd, item_url);

  // Launch the player, after stopping any currently running player first.
  sprintf(playcmd+strlen(playcmd), " \"%s\" &", item_url);
  if (stop)
    system ( stop ); // This lets us kill any player, if multiple available.
  system ( playcmd );
}

/************************************************/
void saveurl(char *filename, char *playlist)
{
  char *s;
  int i;
  FILE *fp;
  
  if (U2L)
    utf8tolatin(filename);
  for (s = strpbrk(filename, "\""); s; s = strpbrk(s, "\""))
    *s = '-'; // Quotes inside strings make for ugly filenames.
  for (s = strpbrk(filename, " "); s; s = strpbrk(s, " "))
    *s = '_'; // Remove spaces from filenames.
  for (s=strpbrk(filename,"'`;()&|\\/"); s; s=strpbrk(s,"'`;()&|\\/"))
    *s = '-'; // Remove other bad chars from filenames.
  
  // Loop through destinations and save the playlist
  //printf("Loop through destfiles/dirs and save the playlist\n");
  for (i=0; i < destnum; i++) {
    struct stat path_stat;
    int fileexists = (-1 != access(dest[i], F_OK));
    
    destfile = dest[i];
    //printf("dest[%d] = %s (exists = %d)\n", i, destfile, fileexists);
    
    if (fileexists && (0 == stat(destfile, &path_stat)) && S_ISDIR(path_stat.st_mode)) { 
      //printf("Found directory\n");
      // If directory, create a new file.
      sprintf(buff, "%s/%s%s",destfile, filename, ext);
      if (fp = fopen(buff, "w")){
	fprintf(fp, playlist); 
	fclose(fp);
      }
      //printf("Make new file %s\n",buff);
    }
    else { // destfile is a file.  Append to it in proper format.
      char *p = playlist;
      
      if (fileexists) {// We must append, and maybe strip some things.
	//printf("Append to file %s\n",destfile);
	if (strstr(destfile,".m3u")) {
	  if (!strcmp(ext, ".m3u")) {
	    //if (s = strstr(playlist,"#EXTM3U")) p = s+7;
	    if (s = strstr(playlist,"#EXTINF:")) p = s;
	  }
	  // Just grab the urls.  Fix this to handle multiple urls.
	  else if (s = strstr(playlist,"http")) p = s;
	}
	else { // Appending to a .pls file
	  if (!strcmp(ext, ".pls")) {
	    if (s = strstr(playlist,"File1=http")) p = s;
	  }
	  else { // Appending to .pls, but we fetched .m3u (FIXME)
	    if (s = strstr(playlist,"http")) p = s;
	  }
	}
      }
      else { // (FIXME)
	//printf("Create file %s\n",destfile);
	// For now, just create new file and dump whatever format we got in it.
      }
      //printf("Opening %s\n",destfile);
      if (fp = fopen(destfile, "a")){
	//printf("Writing %s\n",p);
	fprintf(fp, p); 
	fclose(fp);
      }
    }
  }
}

/************************************************/
CURL *curl_handle;
CURLcode res;
struct MemoryStruct chunk;

/************************************************/
int do_curl(char *url)
{
  chunk.memory = (char *)malloc(1); /* will be grown as needed by WriteMemoryCallback() */ 
  chunk.size = 0;
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
  res = curl_easy_perform(curl_handle);
  if(res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",curl_easy_strerror(res));
  }

  return res;
}

/************************************************/
int get_url(char *the_url) {
  int retval = 1;
  int rerun = 1;
  char *s, *playlist;
  
  cmd = NULL; // Do not free cmd when done unless we malloc for station pick dialog.
  if ((CURLE_OK != do_curl(the_url)) || (chunk.size <= 0))
    retval = 0;
  else {
    cJSON *json = cJSON_Parse(chunk.memory); 
    int n = 0;
    //printf("%s\n",chunk.memory); exit(0);
    if (json) 
      n = cJSON_GetArraySize(json);
    if (n <= 0)
      retval = 0;
    else {
      int i = 0;
      //printf("found %d tags\n",n); exit(0);
      cmd = malloc(chunk.size + strlen(srch_str) + 256); // extra space for "dialog..."
      sprintf(cmd, "dialog --default-item % 10d ", previtem);
      sprintf(cmd+strlen(cmd),"--clear --title \"Pick a station\" ");
      sprintf(cmd+strlen(cmd),"--cancel-label \"Back\" ");

      if (play) {
	  sprintf(cmd+strlen(cmd),"--ok-label \"Play\" ");
	  sprintf(cmd+strlen(cmd),"--extra-button --extra-label \"Save\" ");
	  if (stop) { // Use Help button for Stop, but only if play is available.
	    sprintf(cmd+strlen(cmd),"--help-button --help-label \"Stop\" ");
	  }
      }
      else {
	  sprintf(cmd+strlen(cmd),"--ok-label \"Save\" ");
      }
      sprintf(cmd+strlen(cmd),"--menu \"%d Stations matching <%s>\"", n, srch_str);
      sprintf(cmd+strlen(cmd)," %d %d %d ", height-3, width-6, height-9);

      for (i=0; i<n; i++){
	cJSON *item = cJSON_GetArrayItem(json, i);
	char *id = cJSON_GetObjectItem(item,"id")->valuestring;
	char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	char *item_url = cJSON_GetObjectItem(item,"url")->valuestring;
	char *codec = cJSON_GetObjectItem(item,"codec")->valuestring;
	char *bitrate = cJSON_GetObjectItem(item,"bitrate")->valuestring;
	//printf("% 3d %s\n",i,name);
	strcat(cmd," ");
	sprintf(cmd+strlen(cmd),"%d",i+1);
	//strcat(cmd,"\"");
	strcat(cmd," \"");
#ifndef NOCODEC
	int j;
	for (j=0; j<strlen(codec); j++)
	  codec[j] = tolower(codec[j]);
	if (!strcmp(codec, "unknown"))
	  codec[0] = 0;
	if (strcmp(bitrate, "0"))
	  sprintf(cmd+strlen(cmd),"% 4s % 3s . ",codec,bitrate);
	else
	  sprintf(cmd+strlen(cmd),"% 4s     . ",codec);
#endif
	for (s = strpbrk(name, "\""); s; s = strpbrk(s, "\""))
	  *s = '-'; // Quotes inside strings confuse Dialog.
	if (U2L) {
#if 0
	  if (fp = fopen("utf2lat.txt", "a")){
	    fprintf(fp, "%s\n",name); 
	    utf8tolatin(name);
	    fprintf(fp, "%s\n",name); 
	    fclose(fp);
	  }
	  else 
#endif
	    utf8tolatin(name);
	}
	strcat(cmd,name);
	strcat(cmd,"\"");
      }
      //printf("\n\%s\n",cmd); exit (0);
      strcat(cmd, " 2>/tmp/ziptuner.tmp");
#if 0      
      if ((fp = fopen("response.json", "w"))){
	fprintf(fp,"found %d tags\n",n);
	fprintf(fp, chunk.memory); 
	fprintf(fp,"\n\n");
	fprintf(fp, cmd); 
	fprintf(fp,"\n");
	fclose(fp);
      }
#endif
      while (rerun) {
	rerun = 0; // Only rerun if we hit play or stop.
	// Replace beginning of cmd with remembered previous selection.
	sprintf(cmd, "dialog --default-item % 10d", previtem);
	cmd[strlen(cmd)] = ' ';
	//printf("\n%s\n", cmd);
	choice = system ( cmd ) ;
	//printf("dialog => %d, 0x%08x\n",choice,choice);
	// Seems to return dialog return value shifted left 8 | signal id in the low 7 bits
	// And bit 7 tells if there was a coredump.
	// Or -1 (32 bits) if system failed.
	// So really anything but 0=ok or 0x300=xtra means cancel/die (maybe 0x200=help?)
	if (stop && (choice == 0x200)) {
	  system ( stop ) ;
	  //printf("\n\n%s\n",stop);exit(0);
	  rerun = 1;
	  continue;
	  }
	buff[0] = 0;
	if (fp = fopen("/tmp/ziptuner.tmp", "r")) {
  	  while (fgets(buff, 255, fp) != NULL)
	    {}
 	  fclose(fp);
	}
	if (1 == sscanf(buff, "%d", &i)){
	  cJSON *item = cJSON_GetArrayItem(json, i-1);
	  char *id = cJSON_GetObjectItem(item,"id")->valuestring;
	  char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	  char *item_url = cJSON_GetObjectItem(item,"url")->valuestring;
	  char *codec = cJSON_GetObjectItem(item,"codec")->valuestring;
	  previtem = i;
	  if (fp = fopen("ziptuner.item", "w")){
	    fprintf(fp, "%d", previtem); 
	    fclose(fp);
	  }
	  /* If we hit play, play the playlist in the background and rerun the list. */
          if (play && (choice == 0)) {
	    playit(item_url, codec);
	    rerun = 1;
	    continue;
	  }

	  /* Did NOT hit play, so we need to fetch the playlist and save it. */
	  rerun = 1;
	  sprintf(pls_url, "http://www.radio-browser.info/webservice/v2/m3u/url/%s",id);
	  /* Start over with curl */
	  curl_easy_cleanup(curl_handle);     /* cleanup curl stuff */ 
	  free(chunk.memory);
	  if (CURLE_OK != do_curl(pls_url))
	    continue;

	  //*****************************************
	  // Verify we got a playlist.  If not, try the url from the big list.
	  playlist = chunk.memory;
	  if (strstr(playlist, "did not find station")) { //"did not find station with matching id"
	    playlist = NULL;
	    if(!strstr(item_url,".pls") && !strstr(item_url,".m3u")) {
	      //printf("\nDid NOT find station.  Using item_url.\n");
	      //sprintf(url,"[playlist]\nFile1=%s\n",item_url);
	      sprintf(pls_url,"%s\n",item_url); // Just a link should work for m3u file...
	      playlist = pls_url;
	    }
	    else {
	      //printf("\nDid NOT find station.  Fetching item_url.\n");
	      if (strstr(item_url,".pls")) 
		sprintf(ext, ".pls"); // item_url has .pls extension, so output should too.
	      
	      /* Start over */
	      curl_easy_cleanup(curl_handle);     /* cleanup curl stuff */ 
	      free(chunk.memory);
	      if (CURLE_OK != do_curl(item_url))
		continue;
	      //printf("%d; %s\n",chunk.size,chunk.memory);
	      playlist = chunk.memory;
	    }
	  }
	  
	  //************ Save the playlist **********
	  if (playlist){ // Fix the filename and then save the playlist (if we got one).
	    saveurl(name, playlist);
	  }	
	  sprintf(ext, ".m3u"); // Restore default .m3u file extension, just in case.
	  //*****************************************
	}
      }
    }

    if (json) 
      cJSON_Delete(json);
  }

  if (cmd)
    free(cmd);
  if (0 == retval) {
    gotnone();
  }
  curl_easy_cleanup(curl_handle);     /* cleanup curl stuff */ 
  free(chunk.memory);
  chunk.size = 0;
  /* we're done with libcurl, so clean it up */ 
  curl_global_cleanup();

  if (choice == 0x100) {
    //printf("quit\n");
    return 0;
  }

  return retval;
}

/************************************************/
int get_srch_str_from_list(char *the_url) {
  int retval = 0;
  char *s, *playlist;
  
  cmd = NULL; // Do not free cmd when done unless we malloc for station pick dialog.
  if ((CURLE_OK != do_curl(the_url)) || (chunk.size <= 0))
    retval = 0;
  else {
    cJSON *json = cJSON_Parse(chunk.memory); 
    int n = 0;
    //printf("%s\n",chunk.memory); exit(0);
    if (json) 
      n = cJSON_GetArraySize(json);
    if (n > 0) {
      int i = 0;
      int j = 0;
      cmd = malloc(chunk.size + strlen(srch_str) + 256); // extra space for "dialog..."
      sprintf(cmd, "dialog --clear --title \"Pick from list\" ");
      sprintf(cmd+strlen(cmd),"--menu \"%d %s\"", n, srch_str);
      sprintf(cmd+strlen(cmd)," %d %d %d ", height-3, width-6, height-9);
      for (i=0; i<n; i++){
	cJSON *item = cJSON_GetArrayItem(json, i);
	char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	char *count = cJSON_GetObjectItem(item,"stationcount")->valuestring;
	int k = atoi(count);
	if (k < 2)
	  j++;
	strcat(cmd," ");
	sprintf(cmd+strlen(cmd),"%d",i+1);
	//strcat(cmd,"\"");
	strcat(cmd," \"");
	sprintf(cmd+strlen(cmd),"% 5d . ",k);
	for (s = strpbrk(name, "\""); s; s = strpbrk(s, "\""))
	  *s = '-'; // Quotes inside strings confuse Dialog.
	if (U2L) {
	    utf8tolatin(name);
	}
	strcat(cmd,name);
	strcat(cmd,"\"");
      }
      //printf("\n\%s\n",cmd); exit (0);
      strcat(cmd, " 2>/tmp/ziptuner.tmp");
      choice = system ( cmd ) ;
      //printf("%s\n",chunk.memory); 
      //printf("found %d tags\n",n);
      //printf("%d bogus tags\n",j);

      // Need to get result and store it in srch_str;
      if (fp = fopen("/tmp/ziptuner.tmp", "r")) {
	if (1 == fscanf(fp, "%d", &i)) {
	  cJSON *item = cJSON_GetArrayItem(json, i-1);
	  char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	  strcpy(srch_str, name);
	  if (strlen(srch_str)) {
	    if (s = strpbrk(srch_str, "\r\n"))
	      *s = 0;
	    strcat(srch_url, srch_str);
	    retval = 1;
	  }
	}
	fclose(fp);
      }
    }
  }
  
  // Do I need choice for anything?
  //printf("srchstr: <%s>\n", srch_str);
  //printf("srchurl: <%s>\n", srch_url);
  //printf("cmdl: <%s>\n", cmd);
  //exit(0);

  if (cmd)
    free(cmd);
#if 0
  if (0 == retval) {
    gotnone();
  }
#endif
  curl_easy_cleanup(curl_handle);     /* cleanup curl stuff */ 
  free(chunk.memory);
  chunk.size = 0;
  /* we're done with libcurl, so clean it up */ 
  curl_global_cleanup();

  return retval;
}

/************************************************/
  // Make room for 255 favorite stations.  
  // Shouldn't need more than a dozen, but *could* realloc for collectors...
/************************************************/
char *names[256] = {NULL}; // This is the onscreen station names for dialog. 
char *files[256] = {NULL}; // This is the playlist filenames or url lines.
char  isdir[256] = {0};     // This tells if the playlist is a directory.

/************************************************/
void clean_favs(void) // Cleanup allocated (strdup) strings
{
  int i;
  
  for (i=0; i<256; i++){
    if (NULL == names[i])
      break;
    free(files[i]);
    free(names[i]);
    isdir[i] = 0;
  }
  files[0] = NULL;
  names[0] = NULL;
}

/************************************************/
// Need some code to dig into the file instead of the dirs
// Open the file and scan for names after #EXTINF: and urls on the next line.
/************************************************/
int get_favs_from_file(void)
{
  FILE *fp; 
  char *p,*s;
  int j,i = 0;
  
  //printf("Found playlist file\n");

  fp = fopen(destfile, "r");
  if (fp == NULL)
    return 0;
  
  while (fgets(buff, 255, fp) != NULL)
  {
    // NOTE: After #EXTINF: should be "nnn,StationName" (nnn is play time secs)
    if (p = strstr(buff, "#EXTINF:")){
      p += 8;
      if (s = strchr(p, '\n'))      // Snip trailing linefeed.
	*s = 0;
      j = strspn(p, " 01234567");   // Skip timestamp and comma.
      if (p[j] == ',')
	p = p + j+1;
      strcpy(pls_url, p);
      if (fgets(buff, 255, fp) == NULL)
	break;
      if (s = strchr(buff, '\n'))   // Snip trailing linefeed.
	*s = 0;
      names[i] = strdup(pls_url);
      files[i] = strdup(buff);
      isdir[i] = 0;
      if (++i == 255) break;
    }
  }
  fclose(fp);

  names[i] = NULL;
  return i;
}

/************************************************/
#include <dirent.h>

/************************************************/
int get_favs_from_dir(void)
{
  DIR *dir;
  struct dirent *dent;
  char *s;
  int i = 0;

  //printf("Found playlist directory\n");

  // Maybe call scandir() to filter .m3u and .pls files?

  dir = opendir(destfile);
  if (dir == NULL) 
    return 0;
  strcpy(srch_url, destfile);
  strcat(srch_url, "/");
  
  while ((dent = readdir(dir)) != NULL)
  {
    destfile = dent->d_name;
    strcpy(pls_url, srch_url);
    strcat(pls_url, destfile);
    
    if (strstr(destfile, ".m3u") || strstr(destfile, ".pls")){
      strcpy(buff, destfile);
      files[i] = strdup(pls_url); // Save the station playlist Filename.
      isdir[i] = 1;           // And remember its in a playlist dir (not file)
      if ((s = strstr(buff, ".m3u")) || (s = strstr(buff, ".pls")))
	*s = 0;
      for (s = strpbrk(buff, "_"); s; s = strpbrk(s, "_"))
	*s = ' '; // Restore spaces in filenames.
      //printf("%s\n", buff);
      names[i] = strdup(buff); // Save the station Name

      if (++i == 255) break;
    } 
  }
  closedir(dir);

  return i;
}

/************************************************/
void get_favs()
{
  // Find all .m3u and .pls files in the save dirs (including .)
  int rerun;
  char *s;
  int i,j, k;
  int item;

  //printf("get favs\n");
  cmd = cmd_out;

 scanfavs:
  rerun = 1;
  i = 0;
  item = 0;
  sprintf(cmd, "dialog --default-item % 10d ", previtem);
  sprintf(cmd+strlen(cmd),"--clear --title \"Pick a station\" ");
  sprintf(cmd+strlen(cmd),"--cancel-label \"Back\" ");
  if (play) {
    sprintf(cmd+strlen(cmd),"--ok-label \"Play\" ");
    sprintf(cmd+strlen(cmd),"--extra-button --extra-label \"Delete\" ");
  }
  if (stop) { // Use Help button for Stop.
    sprintf(cmd+strlen(cmd),"--help-button --help-label \"Stop\" ");
    //sprintf(cmd+strlen(cmd),"--extra-button --extra-label \"Stop\" ");
  }
  //sprintf(cmd+strlen(cmd),"--menu \"%d Saved Stations\"", n);
  sprintf(cmd+strlen(cmd),"--menu \"Saved Stations\"");
  sprintf(cmd+strlen(cmd)," %d %d %d ", height-3, width-6, height-9);

  // Loop through destinations and find playlists
  //  for (j=0; j < destnum; j++) {
  // ALL dest dirs should have the SAME saved playlists, so just use first.
  // Otherwise I would have to check for dups, and that's too much work.
  // Also challenging if one dest is a dir, and one is a playlist file...
  for (j=0; j < 1; j++) { 
    struct stat path_stat;
    destfile = dest[j];
    int fileexists = (-1 != access(destfile, F_OK));
    //printf("dest[%d] = %s (exists = %d)\n", j, destfile, fileexists);
    if (!fileexists)
      continue;
    if (0 != stat(destfile, &path_stat))
      continue;
    if (!S_ISDIR(path_stat.st_mode)) 
      i = get_favs_from_file();
    else 
      i = get_favs_from_dir();

    // Add all station names found to the dialog list.
    for (k=0; k<i; k++){
      sprintf(cmd+strlen(cmd)," %d \"%s\"",k+1,names[k]);
    }
  }
      
  if (i == 0) // Give up if no saved stations found.
  {
    gotnone();
    return;
  }

  strcat(cmd, " 2>/tmp/ziptuner.tmp");

  //printf("After srch, dest[%d] = <%s>\n", j, destfile);
  
  //*****************************
  while (rerun) {
    rerun = 0; // Only rerun if we hit play or stop.
    // Replace beginning of cmd with remembered previous selection.
    sprintf(cmd, "dialog --default-item % 10d", previtem);
    cmd[strlen(cmd)] = ' ';

    //printf("\n%s\n", cmd);
    choice = system ( cmd ) ;
   
    //printf("choice = %d\n",choice);
    if (stop && (choice == 0x200)) { // 0x200=help button
      system ( stop ) ;
      //printf("\n\n%s\n",stop);exit(0);
      rerun = 1;
      continue;
    }
    if (choice == 0x100) {
      //printf("quit\n");
      clean_favs();
      return;
    }

    // Need to get result and store it in previtem
    if (fp = fopen("/tmp/ziptuner.tmp", "r")) {
      if (1 == fscanf(fp, "%d", &i)) {
	//printf("item = %d\n",i);
      }
      else continue;
      fclose(fp);
    }
    else continue;

    if (choice == 0x300) { // Extra button means delete
      /*  (need to write some code to find and delete this playlist) */
      /* Probably need to rescan dir (or remove deleted from array before rerun). */

      // NOTE: This ONLY deletes playlist files, not entries in a playlist file.
      if (isdir[i-1])
	unlink(files[i-1]);
      else{
	// del_fav_in_file(i-1);
      }
      clean_favs();
      goto scanfavs;
    }
    
    previtem = i;

    /* If we hit play, play the playlist in the background and rerun the list. */
    if (play && (choice == 0)) {
      playit(files[i-1], NULL); // Now play the station,
      rerun = 1;       // and redisplay the list in case we want to change it.
      continue;
    }
  }

  clean_favs();
}

/************************************************/
int parse_args(int argc, char **argv){
  char *s;

  /* Get terminal size for better dialog size estimates. */
  struct winsize ws;
  width = height = -1;
  if (ioctl(fileno(stdout), TIOCGWINSZ, &ws) >= 0) {
    width = ws.ws_col;
    height = ws.ws_row;
  }
  if (width < 5)
    width = 80;
  if (height < 2)
    height = 23;
  //printf("W,H = (%d, %d)\n",width,height); exit(0);
 
  //printf("processing(%d, %s)\n",argc,*argv);   
  /* Just in case I want more args later... */
  for(--argc,++argv; (argc>0) && (**argv=='-'); --argc,++argv)
  {
    char c = (*argv)[1];
    if (strlen(*argv) > 2) {
      if (argc > 1){
	if (np < 15){
	  int j;
	  codecs[np] = (*argv)+1;
	  for (j=0; j<strlen(codecs[np]); j++)
	    codecs[np][j] = tolower(codecs[np][j]);
	  players[np] = *++argv;
	  np++;
	}
	argc--;
      }
    } 
    else switch(c)
    {
    case 'p':
      if (argc > 1){
	play = *++argv;
	argc--;
      }
      break;
    case 's':
      if (argc > 1){
	stop = *++argv;
	argc--;
      }
      break;
    case 'u':
      U2L =1;
      break;
    case 'h':
    case '?':
      printf("\n-- ziptuner -- internet radio playlist fetcher.\n"
	     "\nUsage:  \n"
	     "ziptuner [-p command] [-s command] [destination] ...\n"
	     "\n"
	     "  -p sets a command for the play button.\n"
	     "  -s sets a command for the stop button.\n"
	     "  -u Convert Latin1 UTF-8 chars to iso-8859-1\n"
	     "  Multiple destinations allowed (files or folders)\n"
	     "\n"
	     "eg:\n  "
	     "ziptuner -p \"mpg123 -q -@ \" ~/my/playlist/folder\n\n"
	     );
      exit(0);
    default:
      // ignore it
      break;
    }
  }
  // Everything after options is playlist save locations.
  if (argc > 0) {
    destfile = argv[0];
    dest = argv;
    destnum = argc;
  }
  //printf("\nplay = <%s>\ndest = <%s>\nnum =%d [%d]\n",play,*dest,destnum, argc);

  // Now I should put play on the end of the players list with a "" codec, and do np++
  if (play){
    codecs[np] = ""; // blank codec matches all for fallback.
    players[np++] = play;
  }
  else if (np)  // If we got players but no default, use the first as default.
    play = players[0]; // Need to set play to non-null to activate play button.
  /* for (i=0; i<np; i++) */
  /*   printf("Codec=<%s> Playcmd=<%s>\n", codecs[i], players[i]); */
}

/************************************************/
int main(int argc, char **argv){
  char *s;
  FILE *fp;
  int i,j;

  parse_args(argc, argv);

  // Main loop of main menu (need to make it a loop instead of a goto)
 retry:
  j=play?1:0; // Add an extra line to menu for favs, if play is available.
  sprintf(srch_url, "http://www.radio-browser.info/webservice/json/stations/");
  sprintf(cmd, "dialog --clear --title \"Zippy Internet Radio Tuner\" ");
  sprintf(cmd+strlen(cmd),"--cancel-label \"Quit\" ");
  if (stop) { // Use Help button for Stop, else we must swap Extra,Cancel buttons.
    sprintf(cmd+strlen(cmd),"--help-button --help-label \"Stop\" ");
    //sprintf(cmd+strlen(cmd),"--extra-button --extra-label \"Stop\" ");
  }
  if ((width < 80) || (height < 23)) { // Fit dialog to small screen.
    strcat(cmd,"--menu \"Select Type of Search\"");
    sprintf(cmd+strlen(cmd)," %d %d %d", height-3, width-6, height-9);
  }
  else { // The screen is large, so display backtitle and small dialog.
    strcat(cmd,"--backtitle \"ziptuner\" ");
    strcat(cmd,"--menu \"Select Type of Search\"");
    sprintf(cmd+strlen(cmd)," %d %d %d", 16+j, 45, 9+j);
  }
  if (-1 != access("ziptuner.url", F_OK)){
      strcat(cmd," 0 \"Resume previous search\"");
  }
  strcat(cmd," 1 \"Search by Tag\"");
  strcat(cmd," 2 \"Search by Country\"");
  strcat(cmd," 3 \"Search by State\"");
  strcat(cmd," 4 \"Search by Language\"");
  strcat(cmd," 5 \"Search by Station Name\"");

  strcat(cmd," 6 \"List Countries\"");
  strcat(cmd," 7 \"List Languages\"");
  strcat(cmd," 8 \"List Tags (there are many)\"");
  if (play)
    strcat(cmd," 9 \"List Saved Stations\"");

  strcat(cmd, " 2>/tmp/ziptuner.tmp");

  //printf("cmd = %s\n", cmd);
  //exit(0);

  choice = system ( cmd ) ;
  //if (stop && (choice == 0x300)) {
  if (stop && (choice == 0x200)) {
    system ( stop ) ;
    //printf("\n\n%s\n",stop);
    exit(0);
  }

  buff[0] = 0;
  if (fp = fopen("/tmp/ziptuner.tmp", "r"))
  {
    while (fgets(buff, 255, fp) != NULL)
      {}
    fclose(fp);
    //printf("\n\n%s\n",buff);
  }
  if (1 != sscanf(buff, "%d", &i))
    exit(0); // /tmp/ziptuner.tmp is empty, so they hit cancel.  Pack up and go home.

  // Try to reuse prev search if selected option 0.
  buff[0] = 0;
  if (i == 0) {
    if (fp = fopen("ziptuner.url", "r")) {
      while (fgets(buff, 255, fp) != NULL)
	{}
      fclose(fp);
      if (strlen(buff)) {
	char *p = strrchr(buff, '/');
	if (p) strcpy(srch_str, ++p);
	else strcpy(srch_str, "previous search");
	strcpy(srch_url, buff);
	// If reusing search, attempt to reuse last item selected as well.
	if (fp = fopen("ziptuner.item", "r")) {
	  if (1 == fscanf(fp, "%d", &i))
	    previtem = i;
	  fclose(fp);
	}
      }
    }
  }
  else if ((i >= 6) && (i <= 8))  {
    if (i == 6) {
	strcpy(srch_str, "Countries");
	strcpy(buff,"http://www.radio-browser.info/webservice/json/countries");
	strcat(srch_url, "bycountry/");
	// about 144 name value stationcount (name always seems same as value)
    }
    else if (i == 7) {
	strcpy(srch_str, "Languages");
	strcpy(buff,"http://www.radio-browser.info/webservice/json/languages");
	strcat(srch_url, "bylanguage/");
	// about 160 name value stationcount (name always seems same as value)
    }
    else if (i == 8) {
	strcpy(srch_str, "Tags");
	strcpy(buff,"http://www.radio-browser.info/webservice/json/tags");
	strcat(srch_url, "bytag/");
	// about 3000 name value stationcount (name always seems same as value)
	// but about 1800 are bogus (skip items with stationcount < 2)
	//
	// Need to run another dialog in get_srch_str_from_list() to pick a name.
    }
    if (!get_srch_str_from_list(buff))
      goto retry;

    if (-1 != access("ziptuner.item", F_OK))
      unlink("ziptuner.item");
  }
  else if (i == 9) {
    // Find all .m3u and .pls files in save dirs (including .)
    get_favs();
    strcpy(buff, "file://.");
    strcpy(srch_str,"");
    if (-1 != access("ziptuner.item", F_OK))
      unlink("ziptuner.item");
    goto retry;
  }
  else if (-1 != access("ziptuner.item", F_OK))
    unlink("ziptuner.item");

  /************************************************/
  // If we get to here, we are gonna run a requested search.
  // (break off to new fn, for readability?
  /************************************************/
  
  if (!strlen(buff)) {
  switch (i) {
  case 2:
    sprintf(cmd, "dialog --title \"Internet Radio Search by Country\" --clear --inputbox ");
    strcat(srch_url, "bycountry/");
    break;
  case 3:
    sprintf(cmd, "dialog --title \"Internet Radio Search by State\" --clear --inputbox ");
    strcat(srch_url, "bystate/");
    break;
  case 4:
    sprintf(cmd, "dialog --title \"Internet Radio Search by Language\" --clear --inputbox ");
    strcat(srch_url, "bylanguage/");
    break;
  case 5:
    sprintf(cmd, "dialog --title \"Internet Radio Search by Station Name\" --clear --inputbox ");
    strcat(srch_url, "byname/");
    break;
  case 1:
  default:
    sprintf(cmd, "dialog --title \"Internet Radio Search by Tag\" --clear --inputbox ");
    strcat(srch_url, "bytag/");
    break;
  }
  sprintf(cmd+strlen(cmd),"\"Search for:\" %d %d 2> /tmp/ziptuner.tmp", height-3, width-6);

  system ( cmd ) ;

  buff[0] = 0;
  if (fp = fopen("/tmp/ziptuner.tmp", "r"))
  {
    while (fgets(buff, 255, fp) != NULL)
      {}
    fclose(fp);
  }
  //printf("\n\n%s\n",buff);
  //remove("/tmp/ziptuner.tmp");
  strcpy(srch_str, buff);
  //printf("tags = <%s>\n", srch_str);
  if (strlen(srch_str)) {
    if (s = strpbrk(srch_str, "\r\n"))
      *s = 0;
    strcat(srch_url, srch_str);
#if 0 /* NOT the best place to save search url, dont know if its good yet. */
    // Save the station search url for re-use.
    if (fp = fopen("ziptuner.url", "w")) {
      fprintf(fp, "%s", srch_url);
      fclose(fp);
    }
#endif
  }
  } // (!strlen(buff)

  if (strlen(srch_str)) {
    // gen_tp();  
    get_int_ip();  // Works on zipit, but not laptop, so just set connection=1.
    int_connection=1; 
    //signal (SIGALRM, catch_alarm);
    if (int_connection) {
      if (!get_url(srch_url)) {
	// I'm still not sure this really works, but give it a shot for now.
	cmd = cmd_out;
	//sprintf(srch_str, "");
	goto retry;
      }
#if 1 /* This is the place to save search url, but is srch_url still good? */
      // Save the station search url for re-use.
      else if (fp = fopen("ziptuner.url", "w")) {
	fprintf(fp, "%s", srch_url);
	fclose(fp);
      }
#endif
    }
  }
  else {
    // /tmp/ziptuner.tmp is empty, so they hit cancel.  Pack up and go home.
    goto retry;
  }
  //printf("W,H = (%d, %d)\n",width,height);
  return 0;	          
}
