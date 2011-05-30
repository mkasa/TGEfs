#ifndef _HEADER_TGE_CACHE
#define _HEADER_TGE_CACHE

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <map>
#include "pmutex.h"

class Cache_LockedFileChecker {
 public:
  Cache_LockedFileChecker() {}
  virtual bool isLockedFile(const std::string& fname) const = 0;
  virtual ~Cache_LockedFileChecker() {}
};

class CacheGarbageCollection {
  bool        initialized;
  int         softLimitInKBytes;
  int         hardLimitInKBytes;
  std::string cacheRootDirectory;

  Mutex       garbageCollection_mutex;

  static long long GC_period_in_KBytes;
  static int       GC_period_in_nFiles;
  static double    GC_apply_hardLimit_ratio_of_HDD_usage;
  static int       GC_delete_cache_if_this_number_of_days_passed;

  long long GC_counter_in_KBytes;
  int       GC_counter_in_nFiles;
  
  void   resetCounter();
  std::string fullPath(const std::string& path);
  bool statCacheRootDir(struct statfs* sfs);

  std::string localCacheCollectionFile;
  Mutex       localCacheCollectionFile_mutex;
  std::map<std::string, std::string> localCacheFileName2originalFullPathName;
  
  std::string localCacheCollection_SolidText;
  bool        localCacheCollection_SolidText_isDirty;
  void                   updateSolidText_internal_shouldBeCalledWithMutexLocked();
public:
  std::string            getSolidText(std::string::size_type start, std::string::size_type end);
  std::string::size_type getSolidTextSize();
  void                   updateSolidText();

private:
  void initLocalFileCollection();
  void saveLocalFileCollection();
public:

  void appendLocalFileCollection(const std::string& localCacheFileName, const std::string& originalFullPathName);
  void removeLocalFileCollection(const std::string& localCacheFileName);
  bool canUseLocalCacheNameForLocalFileCollection(const std::string& localCacheFileName, const std::string& originalFullPathName);

 public:
  static int       AUTO;

  CacheGarbageCollection();
  void init(const char* cacheRootDirectory,
	    const int softLimitInKBytes,
	    const int hardLimitInKBytes);
  void collect(const Cache_LockedFileChecker& clfc);
  void accessedFile(const long long fileSize, const Cache_LockedFileChecker& clfc);
};

#endif // #ifndef _HEADER_TGE_CACHE

