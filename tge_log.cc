#if HAVE_CONFIG
 #include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <sys/file.h>
#include <linux/limits.h>
#include "pmutex.h"
#include "tge_log.h"

static int log_level = 0;
static char log_file_name[PATH_MAX + PATH_MAX];
static Mutex log_mutex;

void initlog(const char *log_dir)
{
  snprintf(log_file_name, sizeof(log_file_name) - 1, "%s/tgefslog", log_dir);
  log_file_name[sizeof(log_file_name) - 1] = '\0';
}

void loglevel(const int showLogEqualOrLessThanThisLevel)
{
  log_level = showLogEqualOrLessThanThisLevel;
}

int getloglevel()
{
  return log_level;
}

void logprintf(const int level, const int logtype, const char *ptr, ...)
{
  if(log_level < level)
    return;
  Mutex::scoped_lock lock(log_mutex);
  FILE* fp = fopen(log_file_name, "a+");
  if(fp == NULL) return;
  const int flockresult = flock(fileno(fp), LOCK_EX); // will be unlocked on close
  if(flockresult == -1) {
    // lock error, but does not report here
  }
  fseek(fp, 0, SEEK_END); // seek to the end

  va_list marker;
  va_start( marker, ptr );
  int size = 256;
  char* buffer = new char[size];
  while(true){
    int nsize = vsnprintf(buffer, size, ptr, marker);
    va_end( marker );
    if(0 <= nsize && nsize < size)
      break;
    delete[] buffer;
    if(nsize < 0){
      size *= 2;
    } else {
      size = nsize + 1;
    }
    buffer = new char[size];
    va_start( marker, ptr );
  }
  {
    char* logtypestr;
    switch(logtype) {
    case LOG_ERROR:
      logtypestr = "ERROR";
      break;
    case LOG_WARNING:
      logtypestr = "WARNING";
      break;
    case LOG_INFO:
      logtypestr = "INFO";
      break;
    case LOG_NOTICE:
      logtypestr = "NOTICE";
      break;
    case LOG_DEBUG:
      logtypestr = "DEBUG";
      break;
    default:
      logtypestr = "UNKNOWN";
    }
    fprintf(fp, "%s: ", logtypestr);
  }
  fprintf(fp, buffer);
  fclose(fp); 
  delete[] buffer;
  va_end( marker );
}
