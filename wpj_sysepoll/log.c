/* --------------------------------------------------------------------------
 *
 *  WP
 *
 * --------------------------------------------------------------------------*/


#include "wpj.h"

FILE * logfile = NULL;
char log_name[200];
char time_stamp[200];
struct tm yesterday;
unsigned long last_time;
pthread_mutex_t logsem;

void init_log(char * log_file){
  strcpy(log_name,log_file);
  logfile = NULL;
  last_time = 0;
  yesterday.tm_mday = -1;
  pthread_mutex_init(&logsem,NULL);
}

void stop_log(){
  if (logfile){
      fclose(logfile);
      logfile = NULL;
    }
}

void logger(char *type, char *host, char *message){
  char date[50];
  char buf[200];
  struct tm *today;
  FILE * f;

  unsigned long ltime;

  if(type == NULL || message == NULL){
    //XXX
    //fprintf(stderr, "Unrecoverable: logger function called with illegal arguments!\n");
    return;
  }

    time( &ltime );

    /* every second */
    if ((ltime > last_time ) || (logfile == NULL)){

      pthread_mutex_lock(&(logsem));

      last_time = ltime;

      today = localtime( &ltime );

      strftime((char *)time_stamp,128,"%H:%M",today);

      /* if day changed or new raport */
      if ((yesterday.tm_mday != today->tm_mday) || (logfile == NULL)){
	memcpy(&yesterday,today,sizeof(struct tm));
	strftime((char *)date,128,"%Y_%m_%d",today);
	sprintf(buf,"%s_%s.log",log_name,date);

	f = logfile;

	logfile = fopen(buf,"at");
	if (f)
	  fclose(f);
      }

      pthread_mutex_unlock(&(logsem));
    }

    if (logfile){
      fprintf(logfile,"%s %s %s\n",time_stamp,host,message);
      fflush(logfile);
    }

    log_debug("%s %s %s",type, host,message);
}

void log_notice(char *host, const char *msgfmt, ...){
    va_list ap;
    char logmsg[512] = "";


    va_start(ap, msgfmt);
    vsnprintf(logmsg, 512, msgfmt, ap);

    logger("notice",host,logmsg);
}

void log_warn(char *host, const char *msgfmt, ...){
    va_list ap;
    char logmsg[512] = "";


    va_start(ap, msgfmt);
    vsnprintf(logmsg, 512, msgfmt, ap);

    logger("warn",host,logmsg);
}

void log_alert(char *host, const char *msgfmt, ...){
    va_list ap;
    char logmsg[512] = "";


    va_start(ap, msgfmt);
    vsnprintf(logmsg, 512, msgfmt, ap);

    logger("alert",host,logmsg);
}
