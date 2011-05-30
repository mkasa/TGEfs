#ifndef _HEADER_APPCONFIG
#define _HEADER_APPCONFIG

bool load_application_config();
extern char cacheDirectoryRoot[];
extern char tgeLockdServer[];
extern int  tgeLockdPort;
extern int  minimumFileSizeToEnableLock;

#endif // #define _HEADER_APPCONFIG
