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
#include <netdb.h>
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
First get a list to choose from.  (old API)
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

// Some compile options to consider.
//#define OLD_API
//#define DEBUG

#ifndef OLD_API
char srv[512] = "https://fr1.api.radio-browser.info"; // Default server
char hbuf[NI_MAXHOST] = "all.api.radio-browser.info"; // Random server selector.
#endif

/*
New api (maybe http://api instead of https://fr1.api works for http ??)
https://fr1.api.radio-browser.info/json/stats
https://fr1.api.radio-browser.info/json/tags
https://fr1.api.radio-browser.info/json/countries
https://fr1.api.radio-browser.info/json/stations/byuuid/{searchterm}
https://fr1.api.radio-browser.info/json/stations/byname/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bynameexact/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bycodec/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bycodecexact/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bycountry/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bycountryexact/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bycountrycodeexact/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bystate/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bystateexact/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bylanguage/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bylanguageexact/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bytag/{searchterm}
https://fr1.api.radio-browser.info/json/stations/bytagexact/{searchterm}

https://fr1.api.radio-browser.info/json/tags
https://fr1.api.radio-browser.info/json/countries
https://fr1.api.radio-browser.info/json/states/USA/

http://fr1.api.radio-browser.info/m3u/url/stationuuid
http://fr1.api.radio-browser.info/pls/url/stationuuid

NOTE: curl on IZ2S has problems with the certs for https.
      cmdline can curl can disable this with -k
      In C it's curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
      Can I just install newer certs on iz2s?  Where are they stored?
      grep for /usr shows /usr/local-openssl/ssl/certs or ../usr/local-openssl/ssl/cert.pem
      I put the certs on IZ2S as /usr/local-openssl/ssl/cert.pem  (did NOT work) 
      But, curl --cacert /usr/local-openssl/ssl/cert.pem DID work (-k also worked).
      So this should work:  curl_easy_setopt(curl, CURLOPT_CAPATH, capath);
      Add cmdline ziptuner opts for special IZ2S workarounds?  Or #ifdef IZ2S ?
      Maybe -k (ignore cert), and -c certfile, then convert to curl_easy_setopt()
*/

/*
NOTE: Gotta read https://api.radio-browser.info/
             and https://fr1.api.radio-browser.info/
      Use uuid fields instead of id field (stationuuid, checkuuid, clickuuid)
        sample json: "stationuuid":"960e57c5-0601-11e8-ae97-52543be04c81"

      Consider countrycode instead of country (../bycountrycodeexect/US)
      2 letter codes:  https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2

      May need to do nslookup on all.api.radio-browser.info
      and pick a random Address: line instead of api.radio...

      New api seems to leave out webservice and/or webserviec/v2
      Gotta test some of this with curl on the cmdline.

      Need to use "click counter" api in playit?
         http://fr1.api.radio-browser.info/m3u/url/stationuuid
      That may also get me a useable .m3u file.
      Or do I just make it myself?
      see what url_resolved gets me.  May be better than the url.
      debug this with DEBUG code (save stationuuid and url_resolved too)

      Need user-agent setting "-A ziptuner/0.2" for curl requests.

      Looks like secure https only for new api searches (301 error for http)
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
int favnum = 0;
int nowplaying = -1;
char splashtext[64];
int resize = 1;
// 24 lines with 5x10 font on 320x240 pixel display (zipit)
#define SPLASH_MINH 24

#ifdef IZ2S
int skipcert = 1;
#else
int skipcert = 0;
#endif
  
/************************************************/
void quit(int q)
{
  printf("\e[H\e[J");
  printf("quit(%d)\n",q);
  exit(0);
}

/************************************************/
int dialog(char *cmd)
{
  int code = system(cmd);
  // If "error" is code -1, does it sign extend to 0xffffff00 ?
  // Puppy linux dialog gives 0x100 (not 0x02) on ctrl-C, which breaks this.
  // Also what about ctrl-Z.  That stops the job.  Is fg resume ok?
  if (((code & 0xff00) == 0xff00) || (code == 0x02)) // ESC or ctrl-C
    quit(0); 
  return code;
}

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

#ifndef OLD_API
/************************************************/
void get_int_ip() // Select random radio-browser server (recommended by API) 
{
  struct addrinfo* addr;
  //char* hostaddr;
  struct sockaddr_in* saddr;
  socklen_t len = sizeof(struct sockaddr_in);
  int j,i = -1;
  
  int_connection=0;
  
  if (0 != getaddrinfo(hbuf, NULL, NULL, &addr)) {
    fprintf(stderr, "Error resolving hostname %s\n", hbuf);
    exit(1);
  }
  // Use current time as seed for rand.  Keep addr with biggest rand.
  srand(time(0)); 
  for (; addr; addr = addr->ai_next)
  {
    if (addr->ai_family==AF_INET && addr->ai_socktype==SOCK_STREAM) // TCP
    {
      int_connection++;
      saddr = (struct sockaddr_in*)addr->ai_addr;
      //hostaddr = inet_ntoa(saddr->sin_addr);
      //printf("IP is %s\n", hostaddr);
      if (getnameinfo((struct sockaddr *) saddr, len, hbuf, sizeof(hbuf), 
		      NULL, 0, NI_NAMEREQD))
	continue;
      if (i < (j = rand())){ 
	i = j;
	sprintf(srv, "https://%s", hbuf);
	//printf("Address is %s\n", hbuf);
      }
    }
  }
}

#else
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
#endif

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
/* Get terminal size for better dialog size estimates. */
void term_resize(void)
{
  struct winsize ws;

  if (!resize) return;
  
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

  resize = 0;
}

/************************************************/
// Make a default backsplash title, with extra room for "Now playing NNN"
/************************************************/
char *splash(int minh) {
  if (height < minh)
    strcpy(splashtext," ");
  else if ((minh < 0) || (nowplaying < 0)) //"--backtitle \"ziptuner           \" ");
    strcpy(splashtext,"--backtitle \"ziptuner\" "); 
  else                                     //"--backtitle \"Now playing %6d\" ",nowplaying);
    sprintf(splashtext,"--backtitle \"Now playing %d\" ",nowplaying); 

  return splashtext;
}

/************************************************/
void gotnone(void) {
  //printf("messagebox Got nothing\n");
  cmd = cmd_out;
  sprintf(cmd, "dialog --clear --title \"Sorry.\" ");
  strcat(cmd,splash(-1)); // strcat(cmd,"--backtitle \"ziptuner\" ");
  strcat(cmd,"--msgbox \"None Found.\"");
#if 0
  sprintf(cmd+strlen(cmd)," %d %d", 6, 20);
#else
  sprintf(cmd+strlen(cmd)," %d %d", 8, 20);
#endif
  dialog ( cmd ) ;
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

#ifdef DEBUG
    FILE *fp;
    // This is a good place to save search url, since we got a json list.
    // Save the station search url for re-use.
    if (fp = fopen("ziptuner.play", "w")) {
      fprintf(fp, "%s\n", playcmd);
      fclose(fp);
    }
#endif
  
}

// /************************************************/
// char *nextline(char *s)
// {
//   s += strcspn(s,"\r\n"); // Skip to end of srch_url
//   s += strspn(s,"\r\n");  // Skip past any CR LF chars.
//   return s;
// }

void add_fav_to_file(char *destfile, char *url, char* name);

/************************************************/
void saveurl(char *filename, char *playlist)
{
  char *s;
  int i;
  FILE *fp;

  strcpy(srch_url, filename); // Save unmangled filename?
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
    
    //*****************************
    // If directory, create a new file in that directory for this playlist.
    if (fileexists && (0 == stat(destfile, &path_stat)) && S_ISDIR(path_stat.st_mode)) { 
      //printf("Found directory\n");
      sprintf(buff, "%s/%s%s",destfile, filename, ext);
      if (fp = fopen(buff, "w")){
	fprintf(fp, "%s", playlist); 
	fclose(fp);
      }
      //printf("Make new file %s\n",buff);
    }
    //*****************************
    else { // destfile is a file.  Append to it in proper format.
      int j;
      //printf("Append to file %s\n",destfile);
      // Just grab the urls.  Fix this to handle multiple urls.
      if ((s = strstr(playlist,"#EXTINF:")) && // its a .mru file.  Get 1st stream.
	  (2 == sscanf(s, " #EXTINF:%[^,],%[^\t\n\r]",srch_str,srch_url))) {
	s += strcspn(s,"\r\n"); // Skip to end of line.
	s += strspn(s,"\r\n");  // Skip past any CR LF chars.
	sscanf(s, " %[^\t\n\r]",pls_url);
      }
      else if ((s = strstr(playlist,"File1=")) && // its a .pls file.  Get 1st stream.
	       (2 == sscanf(s, " File%d=%[^\t\n\r]",&j,pls_url))) {
	s += strcspn(s,"\r\n"); // Skip to end of line.
	s += strspn(s,"\r\n");  // Skip past any CR LF chars.
	sscanf(buff, " Title%d=%[^\t\n\r]",&j,srch_url);
      }
      else { // Just look for an URL
	j = 511;
	if ((s = strstr(playlist,"http://")) ||
	    (s = strstr(playlist,"rtsp://")) ||
	    (s = strstr(playlist,"https://")))
	  j = strcspn(s,"\r\n"); // find end of line.
	else
	  s = playlist;
	strncpy(pls_url,s,j);
	pls_url[j] = 0;
      }
      // Should now have both name and url (name may be NULL for .pls)
      
      if (!fileexists) { // Make a new file if it doesn't exist yet.
	//printf("Create file %s\n",destfile);
	if (fp = fopen(destfile, "w")){
	  if (strstr(destfile,".pls"))
	    fprintf(fp, "[playlist]\n"); 
	  fclose(fp);
	}
      }

      // Only use filename if can't find #EXTINF or Title1=
      add_fav_to_file(destfile, pls_url, srch_url);
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
#ifdef DEBUG
  FILE *fp;
  if (fp = fopen("ziptuner.curl", "w")){
    fprintf(fp, "%s\n", url); 
    fclose(fp);
	  }
#endif
  chunk.memory = (char *)malloc(1); /* will be grown as needed by WriteMemoryCallback() */ 
  chunk.size = 0;
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ziptuner/0.4");
#ifndef OLD_API
  // Tell libcurl to not verify the peer (this works for old puppy linux, and IZ2S)
  // That should be a command line option -k (for all ziptuners, not just IZ2S)
  // (to avoid the cryptonecronom that eventually invalidates everything)
  if (skipcert) curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0);
#ifdef IZ2S /* Tell IZ2S where to find cert.pem (no builtin path, so use debian path) */
  else curl_easy_setopt(curl_handle, CURLOPT_CAINFO, "/usr/local/share/ca_certificates/cert.pem");
#endif      /* Download the curl cert.pem file and put it there.  Works on IZ2S. */

  /* =========== No working default cert location on IZ2S =========== */
  // curl will tell you the default path if you give it a bogus one.  IZ2S says:
  //    curl --cacert bogus https://www.google.com
  //    curl: (77) error setting certificate verify locations:
  //      CAfile: garbled
  //      CApath: none
  //
  // So IZ2S curl was compiled with NO CApath.  And there's no standard between distros.
  // So I like the debian path /usr/local/share/ca_certificates/cacert.pebm for IZ2S.
  // Because /usr/local/share is linked in from the SD card in the IZ2S startup script.
  
  // Provide a default cert path? (could NOT make this work on puppy linux)
  // curl_easy_setopt(curl_handle, CURLOPT_CAPATH, "/usr/local-openssl/ssl"); 
  // curl_easy_setopt(curl_handle, CURLOPT_CAPATH, "/usr/share/curl"); // /curl-ca-bundle.crt");
#endif
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
  nowplaying = -1;
  
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
      FILE *fp;

      // This is a good place to save search url, since we got a json list.
      // Save the station search url for re-use.
      // But incompatible APIs require separate search request files.
#ifndef OLD_API
      // New API uses random servers.  Fixme to NOT save the server name prefix.
      if (fp = fopen("ziptuner.req", "w")) {
	fprintf(fp, "%s\n", srch_url);
	fclose(fp);
      }
#else
      if (fp = fopen("ziptuner.url", "w")) {
	fprintf(fp, "%s\n", srch_url);
	fclose(fp);
      }
#endif

      //printf("found %d tags\n",n); exit(0);
      cmd = malloc(chunk.size + strlen(srch_str) + 512); // extra space for "dialog..."
      
      while (rerun) {
	rerun = 0; // Only rerun if we hit play or stop.
	
	term_resize();
        sprintf(cmd, "dialog --default-item % 10d ", previtem);
        strcat(cmd,splash(SPLASH_MINH));
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
	
	if (height < SPLASH_MINH) // Make room for backtitle with nowplaying
	  sprintf(cmd+strlen(cmd)," %d %d %d ", height-3, width-6, height-9);
	else
	  sprintf(cmd+strlen(cmd)," %d %d %d ", height-5, width-6, height-9);
	
	for (i=0; i<n; i++){
	  cJSON *item = cJSON_GetArrayItem(json, i);
#ifndef OLD_API
	  char *id = cJSON_GetObjectItem(item,"stationuuid")->valuestring;
	  int bitrate = cJSON_GetObjectItem(item,"bitrate")->valueint;
#else
	  char *id = cJSON_GetObjectItem(item,"id")->valuestring;
	  char *bitrate = cJSON_GetObjectItem(item,"bitrate")->valuestring;
#endif
	  char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	  char *item_url = cJSON_GetObjectItem(item,"url")->valuestring;
	  char *codec = cJSON_GetObjectItem(item,"codec")->valuestring;
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
#ifndef OLD_API
	  if (bitrate > 0)
	    sprintf(cmd+strlen(cmd),"% 4s %3d . ",codec,bitrate);
#else
	  if (strcmp(bitrate, "0"))
	    sprintf(cmd+strlen(cmd),"% 4s % 3s . ",codec,bitrate);
#endif
	  else
	    sprintf(cmd+strlen(cmd),"% 4s     . ",codec);
#endif
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

	choice = dialog ( cmd ) ;
	//printf("dialog => %d, 0x%08x\n",choice,choice);
	// Seems to return dialog return value shifted left 8 | signal id in the low 7 bits
	// And bit 7 tells if there was a coredump.
	// Or -1 (32 bits) if system failed.
	// So really anything but 0=ok or 0x300=xtra means cancel/die (maybe 0x200=help?)
	if (stop && (choice == 0x200)) {
	  system ( stop ) ;
	  //printf("\n\n%s\n",stop);exit(0);
	  nowplaying = -1;
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
#ifndef OLD_API
	  char *id = cJSON_GetObjectItem(item,"stationuuid")->valuestring;
#else
	  char *id = cJSON_GetObjectItem(item,"id")->valuestring;
#endif
	  char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	  char *item_url = cJSON_GetObjectItem(item,"url")->valuestring;
	  char *codec = cJSON_GetObjectItem(item,"codec")->valuestring;
	  previtem = i;
	  if (fp = fopen("ziptuner.item", "w")){
	    fprintf(fp, "%d", previtem); 
	    fclose(fp);
	  }
#ifdef DEBUG
	  if (fp = fopen("ziptuner.ntext", "w")){
	    fprintf(fp, "Name==%s\n", name); 
	    fprintf(fp, "I-url=%s\n", item_url); 
	    fprintf(fp, "P-url=%s\n", id); 
	    fclose(fp);
	  }
#endif	  
	  /* IF we hit play, play the playlist in the background and rerun the list. */
          if (play && (choice == 0)) {
	    playit(item_url, codec);
	    nowplaying = i;
	    rerun = 1;
	    continue;
	  }

	  /* Did NOT hit play, so we need to fetch the playlist and save it. */
	  rerun = 1;
#ifndef OLD_API
	  sprintf(pls_url, "%s/m3u/url/%s",srv,id);
#else
	  sprintf(pls_url, "http://www.radio-browser.info/webservice/v2/m3u/url/%s",id);
#endif
	  /* Start over with curl */
	  curl_easy_cleanup(curl_handle);     /* cleanup curl stuff */ 
	  free(chunk.memory);
	  if (CURLE_OK != do_curl(pls_url))
	    continue;

	  //*****************************************
	  // Verify we got a playlist.  If not, try the url from the big list.
	  playlist = chunk.memory;
#ifdef DEBUG
	  if (fp = fopen("ziptuner.ptext", "w")){
	    fprintf(fp, "%s\n", playlist); 
	    fclose(fp);
	  }
#endif
	  if (strstr(playlist, "did not find station") || //"did not find station with matching id"
	      strstr(playlist, "301 Moved Permanently")) { // Yikes, problems with new api???
	    playlist = NULL;
	    if(!strstr(item_url,".pls") && !strstr(item_url,".m3u")) {
	      //printf("\nDid NOT find station.  Using item_url.\n");
	      //sprintf(url,"[playlist]\nFile1=%s\n",item_url);
	      sprintf(pls_url,"%s\n",item_url); // Just a link should work for m3u file...
	      playlist = pls_url;
	    }
	    else { // Item url is a playlist, not a stream.  So fetch the contents.
	      //printf("\nDid NOT find station.  Fetching playlist from item_url.\n");
	      if (strstr(item_url,".pls")) 
		sprintf(ext, ".pls"); // item_url has .pls extension, so output should too.
	      
	      /* Start over */
	      curl_easy_cleanup(curl_handle);     /* cleanup curl stuff */ 
	      free(chunk.memory);
	      if (CURLE_OK != do_curl(item_url))
		continue;
	      //printf("%d; %s\n",chunk.size,chunk.memory);
	      playlist = chunk.memory;
#ifdef DEBUG
	      if (fp = fopen("ziptuner.itext", "w")){
		fprintf(fp, "%s\n", playlist); 
		fclose(fp);
	      }
#endif	  
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

  nowplaying = -1; 
  
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
      cmd = malloc(chunk.size + strlen(srch_str) + 512); // extra space for "dialog..."
      sprintf(cmd, "dialog --clear --title \"Pick from list\" ");
      sprintf(cmd+strlen(cmd),"--menu \"%d %s\"", n, srch_str);
      sprintf(cmd+strlen(cmd)," %d %d %d ", height-3, width-6, height-9);
      for (i=0; i<n; i++){
	cJSON *item = cJSON_GetArrayItem(json, i);
	char *name = cJSON_GetObjectItem(item,"name")->valuestring;
#ifndef OLD_API	
	int k = cJSON_GetObjectItem(item,"stationcount")->valueint;
#else
	char *count = cJSON_GetObjectItem(item,"stationcount")->valuestring;
	int k = atoi(count);
#endif
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
      choice = dialog ( cmd ) ;
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
char *names[256] = {NULL};   // This is the onscreen station names for dialog. 
char *files[256] = {NULL};   // This is the playlist filenames or url lines.
signed char lineN[256] = {0};// This tells playlist line number (-1 if in a dir)

/************************************************/
void clean_favs(void) // Cleanup allocated (strdup) strings
{
  int i;
  
  for (i=0; i<256; i++){
    if (NULL == names[i])
      break;
    free(files[i]);
    free(names[i]);
    lineN[i] = 0;
  }
  files[0] = NULL;
  names[0] = NULL;

  if (cmd)
    free(cmd);
  cmd = cmd_out;
  
  nowplaying = -1; // List is empty, so any index into it is no good.
}

char *tmp_pls = "ziptuner.tmp";

/************************************************/
// Add (append) an item to the end of a .pls or .m3u playlist file.
/************************************************/
void add_fav_to_file(char *destfile, char *url, char* name){
  FILE *fp, *FP;
  int k = 0;  // j is .pls item number to delete.  k is old .pls item nums.
  int pls = 0;
  
  if (NULL == (fp = fopen(destfile, "r"))) return;
  if (NULL == (FP = fopen(tmp_pls, "w"))) return;

  // NOTE:  I've seen streaming .pls files where NumberOfEntries=1 preceeds File1= 
  while (fgets(buff, 255, fp) != NULL)
  {
    if (pls){
      if (strstr(buff, "Version="))
	break;
      if (1 != sscanf(buff, " NumberOfEntries=%d",&k)) // Write NumEntries later...
	fputs(buff, FP);
      if (2 == sscanf(buff, " File%d=%[^\t\n\r]",&k,srch_str))
	pls = 1;
    }
    else {
      fputs(buff, FP);
      pls = (NULL != strstr(buff, "[playlist]")); // Should be 1st line
    }
  }

  if (pls) { // Add our FileN to the playlist, and close it up.
    k = k+1;
    fprintf(FP,"File%d=%s\n",k,url);
    fprintf(FP,"Title%d=%s\n",k,name);
    fprintf(FP,"\n");
    fprintf(FP,"NumberOfEntries=%d\n",k);
    if (strstr(buff, "Version="))
      fprintf(FP, "%s", buff);
  }
  else {
    fprintf(FP,"#EXTINF:1,%s\n",name);
    fprintf(FP,"%s\n",url);
  }
    
  while (fgets(buff, 255, fp) != NULL)
    fputs(buff, FP); 

  fclose(fp);
  fclose(FP);

  // Now replace destfile with the copy.
  unlink(destfile);
  rename(tmp_pls, destfile);
}

/************************************************/
// Delete an item from a .m3u or .pls playlist file
/************************************************/
void del_fav_in_file(int station){
  FILE *fp, *FP;
  int i,j,k = 0;  // j is .pls item number to delete.  k is old .pls item nums.
  int m = 0;      // m is .pls renumber counter (new item numbers)
  int n, pls = 0;
  
  if (NULL == (fp = fopen(destfile, "r"))) return;
  if (NULL == (FP = fopen(tmp_pls, "w"))) return;

  n = lineN[station]; // Get the line number in the file for this station.
  for (i=0; fgets(buff, 255, fp) != NULL; i++)
  {
    if (pls){ // Skip FileN. Update item nums associated with FileM (M != N) 
      if (2 == sscanf(buff, " File%d=%[^\t\n\r]",&k,srch_url)){
	if (i == n) j = k;  // Delete all lines where k = j.
	else m += 1;        // Otherwise increment the .pls renumber counter.
	sprintf(buff,"File%d=%s\n",m,srch_url);
	}
      else if (2 == sscanf(buff, " Title%d=%[^\t\n\r]",&k,srch_url)) 
	sprintf(buff,"Title%d=%s\n",m,srch_url);
      else if (2 == sscanf(buff, " Length%d=%[^\t\n\r]",&k,srch_url))
	sprintf(buff,"Length%d=%s\n",m,srch_url);
      // NOTE: Using m assumes NumberOfEntries comes *after* all File items.
      else if (1 == sscanf(buff, " NumberOfEntries=%d",&k)){
	sprintf(buff,"NumberOfEntries=%d\n",k-1); // Use k-1 instead of m?
	j = -1; // This is not part of FileN, so must not skip fputs().
      }
      else if (1 == sscanf(buff, " %[^\t\n\r]",srch_url)) 
	j = -1; // If not known field (or blank line) then done with FileN.
    
      if (k != j)
	fputs(buff, FP); // Not part of entry to be deleted, so do fputs().
    }
    // If .m3u then delete #EXTINF line and next line with URL
    else if ((i < n) || (i > (n+1))){ 
      fputs(buff, FP);      // copy line to new file, and check if .pls file.
      if (pls = (NULL != strstr(buff, "[playlist]"))) // Should be 1st line 
	j = -1; // Do not skip fputs() until we reach the entry to be deleted.
    }
  }
  fclose(fp);
  fclose(FP);

  // Now replace destfile with the copy.
  unlink(destfile);
  rename(tmp_pls, destfile);
}

/************************************************/
// Open .m3u or .pls file and scan for station urls and titles
/************************************************/
int get_favs_from_file(void)
{
  FILE *fp; 
  char *p,*s;
  int i,j,k;
  
  //printf("Found playlist file\n");
  fp = fopen(destfile, "r");
  if (fp == NULL)
    return 0;
  i = k = 0;
  while (fgets(buff, 255, fp) != NULL)
  {
    // .pls -- 
    if (2 == sscanf(buff, " File%d=%[^\t\n\r]",&j,pls_url)){
      strcpy(srch_url,pls_url);
      if (fgets(buff, 255, fp) != NULL)
	sscanf(buff, " Title%d=%[^\t\n\r]",&j,srch_url);
      files[i] = strdup(pls_url);
      names[i] = strdup(srch_url);
      lineN[i] = k++;               // Save the Line Number in the file.
      if (++i == 255) break;
    }
    // .mru -- After #EXTINF: should be "nnn,StationName" (nnn is play time secs)
    //if (2 == sscanf(buff, " #EXTINF:%d,%[^\t\n\r]",&j,pls_url)){
    if (2 == sscanf(buff, " #EXTINF:%[^,],%[^\t\n\r]",srch_str,pls_url)){
      if (fgets(buff, 255, fp) == NULL)
	break;
      sscanf(buff, " %[^\t\n\r]",srch_url);
      names[i] = strdup(pls_url);
      files[i] = strdup(srch_url);
      lineN[i] = k++;               // Save the Line Number in the file.
      if (++i == 255) break;
    }
    k++;
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
      lineN[i] = -1;          // And remember its in a playlist dir (not file)
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
  int i,j, k, n;
  int item;

  //printf("get favs\n");
  cmd = cmd_out;
  nowplaying = -1;

scanfavs:
  rerun = 1;
  i = 0;
  item = 0;
  
  // Loop through destinations and find playlists
  //  for (j=0; j < destnum; j++) {
  // ALL dest dirs should have the SAME saved playlists, so just use first.
  // Otherwise I would have to check for dups, and that's too much work.
  // Also challenging if one dest is a dir, and one is a playlist file...
  for (n=j=0; j < 1; j++) { 
    struct stat path_stat;
    destfile = dest[j];
    int fileexists = (-1 != access(destfile, F_OK));
    //printf("dest[%d] = %s (exists = %d)\n", j, destfile, fileexists);
    if (!fileexists)
      continue;
    if (0 != stat(destfile, &path_stat))
      continue;
    if (!S_ISDIR(path_stat.st_mode)) 
      n = get_favs_from_file();
    else 
      n = get_favs_from_dir();
  }
      
  if (n == 0) // Give up if no saved stations found.
  {
    gotnone();
    return;
  }

  // Total up length all station names found and allocate dialog cmd string.
  for (k=j=0; j<n; j++)
    k = k + 14 + strlen(names[j]);
  cmd = malloc(k + 512); // extra space for "dialog..."

  //printf("After srch, dest[%d] = <%s>\n", j, destfile);
  
  previtem = 0;  // Do NOT use a previtem from the "search" menus.
  if ((favnum > 0) && (favnum <= n)) {// Now play the station if requested on cmdline.
    previtem = favnum;
    playit(files[previtem-1], NULL); 
    nowplaying = i;
    favnum = -favnum;
  }

  //*****************************
  while (rerun) {
    rerun = 0; // Only rerun if we hit play or stop.
    
    term_resize();
    sprintf(cmd, "dialog --default-item % 10d ", previtem);
    strcat(cmd, splash(SPLASH_MINH));
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
    
    if (height < SPLASH_MINH) // Make room for backtitle with nowplaying
      sprintf(cmd+strlen(cmd)," %d %d %d ", height-3, width-6, height-9);
    else
      sprintf(cmd+strlen(cmd)," %d %d %d ", height-5, width-6, height-9);
    
    // Add all station names found to the dialog list.
    for (j=0; j<n; j++){
      sprintf(cmd+strlen(cmd)," %d \"%s\"",j+1,names[j]);
    }
    strcat(cmd, " 2>/tmp/ziptuner.tmp");

    //printf("\n%s\n", cmd);
    choice = dialog ( cmd ) ;
   
    //printf("choice = %d\n",choice);
    if (stop && (choice == 0x200)) { // 0x200=help button
      system ( stop ) ;
      //printf("\n\n%s\n",stop);exit(0);
      nowplaying = -1;
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

#ifdef DEBUG
    if (fp = fopen("zipplay.tmp", "w")) {
      fprintf(fp,"item = %d\n",i-1);
      fprintf(fp,"Name <%s>\n", names[i-1]);
      fprintf(fp,"Playing <%s>\n", files[i-1]);
      fprintf(fp,"line %d\n", lineN[i-1]);
      fclose(fp);
    }
#endif
    
    if (choice == 0x300) { // Extra button means delete
      if (lineN[i-1] == -1)
	unlink(files[i-1]);
      else{
	del_fav_in_file(i-1);
      }
      clean_favs();
      if (favnum == -i){ // Also delete autoplay favorite if same.
	unlink("ziptuner.fav"); 
	favnum = 0;
      }
      goto scanfavs;
    }
    
    previtem = i;

    /* If we hit play, play the playlist in the background and rerun the list. */
    if (play && (choice == 0)) {
      if (fp = fopen("ziptuner.fav", "w")) { // Remember this as the current fav 
	fprintf(fp,"%d\n",i);                // rename() from /tmp fails on IZ2S
	fclose(fp);
	favnum = -i;
      }
      playit(files[i-1], NULL); // Now play the station,
      nowplaying = i;
      rerun = 1;       // and redisplay the list in case we want to change it.
      continue;
    }
  }

  clean_favs();
}

/************************************************/
static void
sigwinchHandler(int sig)
{
  resize = 1;
}

/************************************************/
int parse_args(int argc, char **argv){
  FILE *fp;
  int i;
  char *s;

  // Ignore broken pipe signals (else ziptuner dies)
  sigaction(SIGPIPE, &(struct sigaction){SIG_IGN}, NULL);

  /* Get terminal size for better dialog size estimates. */
  struct sigaction sa;

  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = sigwinchHandler;
  if (sigaction(SIGWINCH, &sa, NULL) == -1)
    {/* Cannot resize.  Oh well... */}
  term_resize();
    
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
    case 'a':
      if (fp = fopen("ziptuner.fav", "r")) {
	if (1 == fscanf(fp, "%d", &i))
	  favnum = i;
	fclose(fp);
      }
      break;
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
    case 'k':
      skipcert =1;
      break;
    case 'h':
    case '?':
      printf("\n-- ziptuner -- internet radio playlist fetcher.\n"
	     "\nUsage:  \n"
	     "ziptuner [-p command] [-s command] [destination] ...\n"
	     "\n"
	     "  -p sets a command for the play button.\n"
	     "  -s sets a command for the stop button.\n"
	     "  -u convert Latin1 UTF-8 chars to iso-8859-1\n"
	     "  -a auto-resume (playing favorite).\n"
	     "  -k skip ssl CA cert verification.\n"
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

  get_int_ip();  // Works on zipit, but not laptop, so just set connection=1.

  parse_args(argc, argv);

  if (favnum > 0)
    get_favs();
  
  // Main loop of main menu (need to make it a loop instead of a goto)
 retry:
  j=play?1:0; // Add an extra line to menu for favs, if play is available.
#ifndef OLD_API
  sprintf(srch_url, "%s/json/stations/",srv);
#else
  sprintf(srch_url, "http://www.radio-browser.info/webservice/json/stations/");
#endif
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
    splash(-1);
    strcat(cmd,"--menu \"Select Type of Search\"");
    sprintf(cmd+strlen(cmd)," %d %d %d", 16+j, 45, 9+j);
  }
#ifndef OLD_API
  if (-1 != access("ziptuner.req", F_OK)){
      strcat(cmd," 0 \"Resume previous search\"");
#else
  if (-1 != access("ziptuner.url", F_OK)){
      strcat(cmd," 0 \"Resume previous search\"");
#endif
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

  choice = dialog ( cmd ) ;
  //if (stop && (choice == 0x300)) {
  if (stop && (choice == 0x200)) {
    system ( stop ) ;
    //printf("\n\n%s\n",stop);
    //nowplaying = -1;
    quit(1);
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
    quit(1); // /tmp/ziptuner.tmp is empty, so they hit cancel.  Pack up and go home.

  // Try to reuse prev search if selected option 0.
  buff[0] = 0;
  if (i == 0) {
#ifndef OLD_API
    if (fp = fopen("ziptuner.req", "r")) {
      fgets(buff, 255, fp);
      fclose(fp);
#else
    if (fp = fopen("ziptuner.url", "r")) {
      fgets(buff, 255, fp);
      fclose(fp);
#endif
      if (strlen(buff)) {
	char *p = strpbrk(buff,"\n\r");
	if (p) *p = 0;
	p = strrchr(buff, '/');
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
#ifndef OLD_API
	sprintf(buff,"%s/json/countries",srv);
#else
	strcpy(buff,"http://www.radio-browser.info/webservice/json/countries");
#endif
	strcat(srch_url, "bycountry/");
	// about 144 name value stationcount (name always seems same as value)
    }
    else if (i == 7) {
	strcpy(srch_str, "Languages");
#ifndef OLD_API
	sprintf(buff,"%s/json/languages",srv);
#else
	strcpy(buff,"http://www.radio-browser.info/webservice/json/languages");
#endif
	strcat(srch_url, "bylanguage/");
	// about 160 name value stationcount (name always seems same as value)
    }
    else if (i == 8) {
	strcpy(srch_str, "Tags");
#ifndef OLD_API
	sprintf(buff,"%s/json/tags",srv);
#else
	strcpy(buff,"http://www.radio-browser.info/webservice/json/tags");
#endif
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
    // favs use ziptuner.fav, so no need to unlink("ziptuner.item");
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
  }
  } // (!strlen(buff)

  if (strlen(srch_str)) {
    // gen_tp();  
    //get_int_ip();  // Works on zipit, but not laptop, so just set connection=1.
    int_connection=1; 
    //signal (SIGALRM, catch_alarm);
    if (int_connection) {
      if (!get_url(srch_url)) {
	// I'm still not sure this really works, but give it a shot for now.
	cmd = cmd_out;
	//sprintf(srch_str, "");
	goto retry;
      }
    }
  }
  else {
    // /tmp/ziptuner.tmp is empty, so they hit cancel.  Pack up and go home.
    goto retry;
  }
  //printf("W,H = (%d, %d)\n",width,height);
  return 0;	          
}
