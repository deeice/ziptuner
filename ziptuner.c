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

#include "cJSON.h"

// ------------ Setup --------------------------

#define WIDTH 320
#define HEIGHT 240

#define CMD_OUT_MAX 1024

#if 0
char *TAGS_URL = (char *)
    "http://www.radio-browser.info/webservice/json/tags";
#else
char TAGS_URL[256] = 
  "http://www.radio-browser.info/webservice/json/stations/bytag/";
#endif

char *destdir = ".";
char tags[256] = "";

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
*/

// ---------------------------------------------


struct MemoryStruct {
  char *memory;
  size_t size;
};

#if 0
SDL_Surface* screen = NULL;
int rotateScreen=0;

TTF_Font *font_control = NULL;
TTF_Font *font_text_date = NULL;
TTF_Font *font_text_radios = NULL;
TTF_Font *font_text_status = NULL;
TTF_Font *font_text_weather = NULL;
TTF_Font *font_text_ip = NULL;
#endif

unsigned int lastRadioAlarmTime=0;
unsigned int lastWeatherAlarmTime=0;

struct ifreq ifr_eth;
struct ifreq ifr_wlan;
int int_connection=0;

pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;

int quit=0;
unsigned int radioPlay=0;
char cmd_out[CMD_OUT_MAX];  
char *cmd = cmd_out;

int ip_wlan_x;

FILE *fd;
char buff[255];
	
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
  CURL *curl_handle;
  CURLcode res;
  char *s;
  struct MemoryStruct chunk;
  chunk.memory = (char *)malloc(1); /* will be grown as needed by WriteMemoryCallback() */ 
  chunk.size = 0; /* no data at this point */
  printf("\nURL = %s\n\n", url);
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
    cJSON *json = cJSON_Parse(chunk.memory); 
    //printf("%s\n",chunk.memory);
    if (!json) {
     printf("Error before: [%s]\n",cJSON_GetErrorPtr());
    } else {
      int i = 0;
      cmd = malloc(chunk.size);
      int n = cJSON_GetArraySize(json);
      printf("found %d tags\n",n);

      sprintf(cmd, "dialog  --clear --title \"Pick a station\" --menu ");
      sprintf(cmd+strlen(cmd),"\"%d Stations matching <%s>\"", n, tags);
      strcat(cmd," 21 51 14 ");

      for (i=0; i<n; i++){
	cJSON *item = cJSON_GetArrayItem(json, i);
	char *id = cJSON_GetObjectItem(item,"id")->valuestring;
	char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	char *url = cJSON_GetObjectItem(item,"url")->valuestring;
	printf("% 3d %s\n",i,name);
	strcat(cmd," ");
	sprintf(cmd+strlen(cmd),"%d",i+1);
	//strcat(cmd,"\"");
	strcat(cmd," \"");
	for (s = strpbrk(name, "\""); s; s = strpbrk(s, "\""))
	  *s = '-'; // Quotes inside strings confuse Dialog.
	strcat(cmd,name);
	strcat(cmd,"\"");
      }
      strcat(cmd, " 2>tempfile");
      if ((fd = fopen("response.json", "w"))){
	fprintf(fd,"found %d tags\n",n);
	fprintf(fd, chunk.memory); 
	fprintf(fd,"\n\n");
	fprintf(fd, cmd); 
	fprintf(fd,"\n");
	fclose(fd);
      }
      if (n <= 0) {
	printf("messagebox Got nothing\n");
      }
      else {
	printf("\n%s\n", cmd);
	system ( cmd ) ;
	if (!(fd = fopen("tempfile", "r")))
	{
	  //remove("tempfile");
	  //return NULL;
	}
	buff[0] = 0;
	while (fgets(buff, 255, fd) != NULL)
	  {}
	fclose(fd);
	printf("\n\n%s\n",buff);
	//remove("tempfile");

	if (1 == sscanf(buff, "%d", &i)){
	  cJSON *item = cJSON_GetArrayItem(json, i-1);
	  char *id = cJSON_GetObjectItem(item,"id")->valuestring;
	  char *name = cJSON_GetObjectItem(item,"name")->valuestring;
	  sprintf(url, "http://www.radio-browser.info/webservice/v2/pls/url/%s",id);
	  
	  printf("%d: %s\n",i,url);
	  
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
	    for (s = strpbrk(name, "\""); s; s = strpbrk(s, "\""))
	      *s = '-'; // Quotes inside strings make for ugly filenames.
	    for (s = strpbrk(name, " "); s; s = strpbrk(s, " "))
	      *s = '_'; // Remove spaces from filenames.
	    sprintf(buff, "%s/%s.pls",destdir, name);
	    if ((fd = fopen(buff, "w"))){
	      fprintf(fd, chunk.memory); 
	      fclose(fd);
	    }
	    printf("%d; %s\n",chunk.size,chunk.memory);
	  }	
	}
      }
    }
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

#if 0
  /* Just in case I want more args later... */
  while((--argc>0) && (**++argv=='-'))
  {
    char c;
    int n;
    //printf("processing(%d, %s)\n",argc,*argv); 
    while(c=*++*argv) // Eat a bunch of one char commands after -.
    {
      //printf("cmd %c (%d, %s)\n",c, argc,*argv); 
      switch(c)
      {
      case 'x':
	someboolvar ^= 1;
	break;
      case 'h':
      case '?':
	printf("Usage:  ziptuner -x [outputDir]\n"
	       "\n"
	       "Internet radio playlist fetcher.\n"
	       "\n"
	       "  x = someboolvar.\n"
	       "\n"
	       "eg:"
	       "  ziptuner -x /my/playlist/folder\n"
	       );
	exit(0);
      default:
	// ignore it
	break;
      }
    }
  }
#else
  if (argc > 1)
    destdir = argv[1];
#endif

  sprintf(cmd, "dialog  --clear --title \"Zippy Internet Radio Tuner\" --menu ");
  sprintf(cmd+strlen(cmd),"\"Select Type of Search\"");
  strcat(cmd," 21 51 14");
  strcat(cmd," 1 \"Search by Tag\"");
  strcat(cmd," 2 \"Search by Country\"");
  strcat(cmd," 3 \"Search by State\"");
  strcat(cmd," 4 \"Search by Language\"");
  strcat(cmd," 5 \"Search by Station Name\"");
  strcat(cmd, " 2>tempfile");

  printf("cmd = %s\n", cmd);
  //exit(0);

  system ( cmd ) ;
  if (!(fd = fopen("tempfile", "r")))
  {
    //remove("tempfile");
    //return NULL;
  }
  while (fgets(buff, 255, fd) != NULL)
    {}
  fclose(fd);
  printf("\n\n%s\n",buff);

  //exit(0);
  if (1 != sscanf(buff, "%d", &i))
    i = 1; // Default to search by tag
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
  strcat(cmd, "\"Search for:\" 16 51 2> tempfile");
  system ( cmd ) ;
  if (!(fd = fopen("tempfile", "r")))
  {
    //remove("tempfile");
    //return NULL;
  }
  while (fgets(buff, 255, fd) != NULL)
    {}
  fclose(fd);
  printf("\n\n%s\n",buff);
  //remove("tempfile");
  strcpy(tags, buff);
  printf("tags = <%s>\n");
  if (!strlen(tags))
    exit(0);

  if (s = strpbrk(tags, "\r\n"))
    *s = 0;
  strcat(TAGS_URL, tags);

  // gen_tp();  
  get_int_ip();  // Works on zipit, but not on laptop, so just set connection=1.
  int_connection=1; 

  //signal (SIGALRM, catch_alarm);
 
  if (int_connection) {
    get_url(TAGS_URL); 
  } 

   printf("end\n");        
  return 0;	          
}
