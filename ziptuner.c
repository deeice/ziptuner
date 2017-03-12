#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
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

char TAGS_URL[512] = 
  "http://www.radio-browser.info/webservice/json/stations/bytag/";

int  destnum = 1;
char *destfile = ".";
char **dest = &destfile;
char tags[512] = "";

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

FILE *fd;
char buff[256];

char *play = NULL; // mpg123tty4
int choice = 0;
	
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
int get_url(char *url) {
  int retval = 1;
  CURL *curl_handle;
  CURLcode res;
  char *s, *playlist;
  struct MemoryStruct chunk;
  chunk.memory = (char *)malloc(1); /* will be grown as needed by WriteMemoryCallback() */ 
  chunk.size = 0; /* no data at this point */
  //printf("\nURL = %s\n\n", url);
  curl_global_init(CURL_GLOBAL_ALL);
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_URL,url);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
  res = curl_easy_perform(curl_handle);
  cmd = NULL; // Do not free cmd when done unless we malloc for station pick dialog.
  if(res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",curl_easy_strerror(res));
    retval = 0;
  }
  else if (chunk.size <= 0) {
    retval = 0;
  }
  else {
    cJSON *json = cJSON_Parse(chunk.memory); 
    //printf("%s\n",chunk.memory);
    if (!json) {
     printf("Error before: [%s]\n",cJSON_GetErrorPtr());
    } else {
      int i = 0;
      int n = cJSON_GetArraySize(json);
      //printf("found %d tags\n",n);

      cmd = malloc(chunk.size + 128); // Add extra space for "dialog..."
      sprintf(cmd, "dialog --clear --title \"Pick a station\" ");
      if (play) {
	  sprintf(cmd+strlen(cmd),"--ok-label \"Save\" ");
	  sprintf(cmd+strlen(cmd),"--extra-button --extra-label \"Play\" ");
      }
      sprintf(cmd+strlen(cmd),"--menu \"%d Stations matching <%s>\"", n, tags);
      sprintf(cmd+strlen(cmd)," %d %d %d ", height-3, width-6, height-9);

      for (i=0; i<n; i++){
	cJSON *item = cJSON_GetArrayItem(json, i);
	char *id = cJSON_GetObjectItem(item,"id")->valuestring;
	char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	char *item_url = cJSON_GetObjectItem(item,"url")->valuestring;
	//printf("% 3d %s\n",i,name);
	strcat(cmd," ");
	sprintf(cmd+strlen(cmd),"%d",i+1);
	//strcat(cmd,"\"");
	strcat(cmd," \"");
	for (s = strpbrk(name, "\""); s; s = strpbrk(s, "\""))
	  *s = '-'; // Quotes inside strings confuse Dialog.
	strcat(cmd,name);
	strcat(cmd,"\"");
      }
      strcat(cmd, " 2>/tmp/ziptuner.tmp");
#if 0      
      if ((fd = fopen("response.json", "w"))){
	fprintf(fd,"found %d tags\n",n);
	fprintf(fd, chunk.memory); 
	fprintf(fd,"\n\n");
	fprintf(fd, cmd); 
	fprintf(fd,"\n");
	fclose(fd);
      }
#endif
      if (n <= 0) {
	retval = 0;
      }
      else {
	//printf("\n%s\n", cmd);
	choice = system ( cmd ) ;
	//printf("dialog => %d, 0x%08x\n",choice,choice);
	// Seems to return dialog return value shifted left 8 | signal id in the low 7 bits
	// And bit 7 tells if there was a coredump.
	// Or -1 (32 bits) if system failed.
	// So really anything but 0=ok or 0x300=play means cancel/die (maybe 0x200=help?)
	//exit(0);
	if (!(fd = fopen("/tmp/ziptuner.tmp", "r")))
	{
	  //remove("/tmp/ziptuner.tmp");
	  //return NULL;
	}
	buff[0] = 0;
	while (fgets(buff, 255, fd) != NULL)
	  {}
	fclose(fd);
	//printf("\n\n%s\n",buff);
	//remove("/tmp/ziptuner.tmp");

	if (choice == 0x300) {
	  if (1 == sscanf(buff, "%d", &i)){
	    cJSON *item = cJSON_GetArrayItem(json, i-1);
	    char *item_url = cJSON_GetObjectItem(item,"url")->valuestring;
	    if (!(strstr(item_url,".m3u") || strstr(item_url,".pls"))) {
	      char *p = strstr(play,"-@");
	      //printf ("Skipping playlist arg in <%s>\n", play);
	      if (p)    // If its a stream and not a playlist then...
		*p = 0; // remove any mpg123 arg that says its a playlist.
	    }
	    //printf ("\n%s \"%s\"\n", play, item_url);
            cmd = malloc(strlen(play) + strlen(item_url) + 128); 
            sprintf(cmd, "\n%s \"%s\" &\n", play, item_url);
            system ( cmd ) ;
	  }
	  else {
	    //printf ("\nSkipping %s\n", play);
	  }
	  // exit (0);
	}
	else

	if (1 == sscanf(buff, "%d", &i)){
	  cJSON *item = cJSON_GetArrayItem(json, i-1);
	  char *id = cJSON_GetObjectItem(item,"id")->valuestring;
	  char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	  char *item_url = cJSON_GetObjectItem(item,"url")->valuestring;
	  //sprintf(url, "http://www.radio-browser.info/webservice/v2/pls/url/%s",id);
	  sprintf(url, "http://www.radio-browser.info/webservice/v2/m3u/url/%s",id);
#if 0	  
	  /* DEBUG stuff.  Delete when fully functional */
	  printf("%d: %s\n",i,url);
	  if ((fd = fopen("id.url", "w"))){
	    fprintf(fd, url); 
	    fclose(fd);
	  }
#endif	  
	  /* Start over */
	  curl_easy_cleanup(curl_handle);     /* cleanup curl stuff */ 
	  free(chunk.memory);
	  chunk.memory = (char *)malloc(1);
	  chunk.size = 0;

	  curl_global_init(CURL_GLOBAL_ALL);
	  curl_handle = curl_easy_init();
	  curl_easy_setopt(curl_handle, CURLOPT_URL,url);
	  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
	  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
	  res = curl_easy_perform(curl_handle);
	  if(res != CURLE_OK) {
	    fprintf(stderr, "curl_easy_perform() failed: %s\n",curl_easy_strerror(res));
	  }
	  else {
	    // Verify we got a playlist.  If not, try the url from the big list.
	    playlist = chunk.memory;
	    //if (strstr(playlist, "did not find station with matching id")) {
	    if (strstr(playlist, "did not find station")) {
	      playlist = NULL;
	      if(!strstr(item_url,".pls") && !strstr(item_url,".m3u")) {
		//printf("\nDid NOT find station.  Using item_url.\n");
		//sprintf(url,"[playlist]\nFile1=%s\n",item_url);
		sprintf(url,"%s\n",item_url); // Just a link should work for m3u file...
		playlist = url;
	      }
	      else {
		//printf("\nDid NOT find station.  Fetching item_url.\n");
		if (strstr(item_url,".pls")) 
		  sprintf(ext, ".pls"); // item_url has .pls extension, so output should too.

		/* Start over */
		curl_easy_cleanup(curl_handle);     /* cleanup curl stuff */ 
		free(chunk.memory);
		chunk.memory = (char *)malloc(1);
		chunk.size = 0;
		
		curl_global_init(CURL_GLOBAL_ALL);
		curl_handle = curl_easy_init();
		curl_easy_setopt(curl_handle, CURLOPT_URL,item_url);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
		curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
		res = curl_easy_perform(curl_handle);
		if(res != CURLE_OK) {
		  fprintf(stderr, "curl_easy_perform() failed: %s\n",curl_easy_strerror(res));
		}
		else {
		  //printf("%d; %s\n",chunk.size,chunk.memory);
		  playlist = chunk.memory;
		}
	      }
	    }
	    if (playlist){ // Fix the filename and then save the playlist (if we got one).

	      for (s = strpbrk(name, "\""); s; s = strpbrk(s, "\""))
		*s = '-'; // Quotes inside strings make for ugly filenames.
	      for (s = strpbrk(name, " "); s; s = strpbrk(s, " "))
		*s = '_'; // Remove spaces from filenames.
              for (s=strpbrk(name,"'`;()&|\\/"); s; s=strpbrk(s,"'`;()&|\\/"))
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
		  sprintf(buff, "%s/%s%s",destfile, name, ext);
		  if (fd = fopen(buff, "w")){
		    fprintf(fd, playlist); 
		    fclose(fd);
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
		  if (fd = fopen(destfile, "a")){
		    //printf("Writing %s\n",p);
		    fprintf(fd, p); 
		    fclose(fd);
		  }
		}
	      }
	    }	
	  }
	}
      }
    }
  }
  if (cmd)
    free(cmd);
  if (0 == retval) {
    //printf("messagebox Got nothing\n");
    cmd = cmd_out;
    sprintf(cmd, "dialog --clear --title \"Sorry.\" --msgbox \"None Found.\"");
    sprintf(cmd+strlen(cmd)," %d %d", 6, 20);
    system ( cmd ) ;
  }
  curl_easy_cleanup(curl_handle);     /* cleanup curl stuff */ 
  free(chunk.memory);
  chunk.size = 0;
  /* we're done with libcurl, so clean it up */ 
  curl_global_cleanup();
}

/************************************************/
int main(int argc, char **argv){
  char *s;
  FILE *fd;
  int i;

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
    switch(c)
    {
    case 'p':
      if (argc > 1){
	play = *++argv;
	argc--;
      }
      break;
    case 'h':
    case '?':
      printf("Usage:  ziptuner [-p playcommand] [destination] ...\n"
	     "\n"
	     "Internet radio playlist fetcher.\n"
	     "\n"
	     "  Use -p to specify a command for the play button.\n"
	     "  Multiple destinations allowed.  Files or folders..\n"
	     "\n"
	     "eg:"
	     "  ziptuner -p mpg123 ~/my/playlist/folder\n"
	     );
      exit(0);
    default:
      // ignore it
      break;
    }
  }
  if (argc > 0) {
    destfile = argv[0];
    dest = argv;
    destnum = argc;
  }
  //printf("\nplay = <%s>\ndest = <%s>\nnum =%d [%d]\n",play,*dest,destnum, argc);

 retry:
  sprintf(cmd, "dialog --clear --title \"Zippy Internet Radio Tuner\" --menu ");
  strcat(cmd,"\"Select Type of Search\"");
  sprintf(cmd+strlen(cmd)," %d %d %d", height-3, width-6, height-9);
  if (-1 != access("ziptuner.url", F_OK)){
      strcat(cmd," 0 \"Resume previous search\"");
  }
  strcat(cmd," 1 \"Search by Tag\"");
  strcat(cmd," 2 \"Search by Country\"");
  strcat(cmd," 3 \"Search by State\"");
  strcat(cmd," 4 \"Search by Language\"");
  strcat(cmd," 5 \"Search by Station Name\"");
  strcat(cmd, " 2>/tmp/ziptuner.tmp");

  //printf("cmd = %s\n", cmd);
  //exit(0);

  system ( cmd ) ;
  if (!(fd = fopen("/tmp/ziptuner.tmp", "r")))
  {
    //remove("/tmp/ziptuner.tmp");
    //return NULL;
  }
  buff[0] = 0;
  while (fgets(buff, 255, fd) != NULL)
    {}
  fclose(fd);
  //printf("\n\n%s\n",buff);

  if (1 != sscanf(buff, "%d", &i))
    exit(0); // /tmp/ziptuner.tmp is empty, so they hit cancel.  Pack up and go home.

  // Try to reuse prev search if selected option 0.
  buff[0] = 0;
  if (i == 0) {
    if (i == 0) {
      if (fd = fopen("ziptuner.url", "r")) {
        while (fgets(buff, 255, fd) != NULL)
          {}
        fclose(fd);
        if (strlen(buff)) {
            strcpy(tags, buff);
            strcpy(TAGS_URL, buff);
        }
      }
    }
  }
  if (!strlen(buff)) {
  switch (i) {
  case 2:
    sprintf(cmd, "dialog --title \"Internet Radio Search by Country\" --clear --inputbox ");
    sprintf(TAGS_URL, "http://www.radio-browser.info/webservice/json/stations/bycountry/");
    break;
  case 3:
    sprintf(cmd, "dialog --title \"Internet Radio Search by State\" --clear --inputbox ");
    sprintf(TAGS_URL, "http://www.radio-browser.info/webservice/json/stations/bystate/");
    break;
  case 4:
    sprintf(cmd, "dialog --title \"Internet Radio Search by Language\" --clear --inputbox ");
    sprintf(TAGS_URL, "http://www.radio-browser.info/webservice/json/stations/bylanguage/");
    break;
  case 5:
    sprintf(cmd, "dialog --title \"Internet Radio Search by Station Name\" --clear --inputbox ");
    sprintf(TAGS_URL, "http://www.radio-browser.info/webservice/json/stations/byname/");
    break;
  case 1:
  default:
    sprintf(cmd, "dialog --title \"Internet Radio Search by Tag\" --clear --inputbox ");
    break;
  }
  sprintf(cmd+strlen(cmd),"\"Search for:\" %d %d 2> /tmp/ziptuner.tmp", height-3, width-6);

  system ( cmd ) ;

  buff[0] = 0;
  if (fd = fopen("/tmp/ziptuner.tmp", "r"))
  {
    while (fgets(buff, 255, fd) != NULL)
      {}
    fclose(fd);
  }
  //printf("\n\n%s\n",buff);
  //remove("/tmp/ziptuner.tmp");
  strcpy(tags, buff);
  //printf("tags = <%s>\n", tags);
  if (strlen(tags)) {
    if (s = strpbrk(tags, "\r\n"))
      *s = 0;
    strcat(TAGS_URL, tags);
    // Save the station search url for re-use.
    if (fd = fopen("ziptuner.url", "w")) {
      fprintf(fd, "%s", TAGS_URL);
      fclose(fd);
    }
  }
  }

  if (strlen(tags)) {
    // gen_tp();  
    get_int_ip();  // Works on zipit, but not laptop, so just set connection=1.
    int_connection=1; 
    //signal (SIGALRM, catch_alarm);
    if (int_connection) {
      if (!get_url(TAGS_URL)) {
	// I'm still not sure this really works, but give it a shot for now.
	*cmd = cmd_out;
	sprintf(TAGS_URL, "http://www.radio-browser.info/webservice/json/stations/bytag/");
	sprintf(tags, "");
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
