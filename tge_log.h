#ifndef _HEADER_TGE_LOG
#define _HEADER_TGE_LOG

#define LOG_ERROR     0
#define LOG_WARNING   1
#define LOG_INFO      2
#define LOG_NOTICE    3
#define LOG_DEBUG     4

void initlog(const char *log_dir);
void logprintf(const int level, const int logtype, const char *ptr, ...);
void loglevel(const int showLogEqualOrLessThanThisLevel);
int  getloglevel();

#endif // ifndef _HEADER_TGE_LOG
