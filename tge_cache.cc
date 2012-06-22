#if HAVE_CONFIG
 #include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <vector>
#include <string.h>
#include <fstream>
#include <algorithm>
#include "tge_log.h"
#include "tge_cache.h"

using namespace std;

long long CacheGarbageCollection::GC_period_in_KBytes = 1024 * 1024ll * 3;      // 3Gbytes
int       CacheGarbageCollection::GC_period_in_nFiles = 1000;                   // 1000files
double    CacheGarbageCollection::GC_apply_hardLimit_ratio_of_HDD_usage = 0.70; // 70%
int       CacheGarbageCollection::GC_delete_cache_if_this_number_of_days_passed = 14; // 2 weeks;
int       CacheGarbageCollection::AUTO = -1;

static inline bool IsKanji(char c) { return false; }

void CSVParse(const string& str, vector<string>& Retval) {
  Retval.clear();
  string curstr;
  int State = 0;
  for(unsigned int i = 0; i < str.size(); i++) {
    switch(State){
    case 0:
      if(str[i] == '"'){
        State = 1;
      } else {
        if(str[i] == ','){
          Retval.push_back(curstr); curstr.erase();
        } else {
          curstr += str[i];
          if(IsKanji(str[i])){
            curstr += str[++i];
          }
        }
      }
      break;
    case 1:
      if(str[i] == '"'){
        State = 2;
      } else {
        curstr += str[i];
        if(IsKanji(str[i])){
          curstr += str[++i];
        }
      }
      break;
    case 2:
      if(str[i] == '"'){
        curstr += '"';
        State = 1;
      } else {
        if(str[i] == ','){
          Retval.push_back(curstr); curstr.erase();
        } else {
          curstr += str[i];
          if(IsKanji(str[i])){
            curstr += str[++i];
          }
        }
      }
      break;
    }
  }
  Retval.push_back(curstr);
}

string convertToCSV(const vector<string>& Data) {
  string RetVal;
  for(unsigned int i = 0; i < Data.size(); i++){
    const string& s = Data[i];
    if(i != 0)
      RetVal += ",";
    if(s.find_first_of(",\"") == string::npos){
      RetVal += s;
    } else {
      RetVal += '"';
      unsigned int j;
      for(j = 0; j < s.length(); j++){
        if(s[j] == '"'){
          RetVal += "\"\"";
        } else {
          if(IsKanji(s[j])){
            RetVal += s[j++];
          }
          RetVal += s[j];
        }
      }
      RetVal += '"';
    }
  }
  return RetVal;
}

CacheGarbageCollection::CacheGarbageCollection()
{
  initialized = false;
  resetCounter();
}

void CacheGarbageCollection::resetCounter()
{
  GC_counter_in_KBytes = 0ll;
  GC_counter_in_nFiles = 0;
}

void CacheGarbageCollection::accessedFile(const long long fileSize, const Cache_LockedFileChecker& clfc)
{
  if(!initialized) {
    logprintf(0, LOG_ERROR, "Garbage collection maneger is called without initialization. (AF)\n");
    return;
  }
  if(fileSize < 0)
    return;
  GC_counter_in_KBytes = (fileSize + 4095ll) / 1024;
  GC_counter_in_nFiles++;
  if(GC_period_in_nFiles <= GC_counter_in_nFiles || GC_period_in_KBytes <= GC_counter_in_KBytes) {
    collect(clfc);
    resetCounter();
  }
}

void CacheGarbageCollection::appendLocalFileCollection(const std::string& localCacheFileName, const std::string& originalFullPathName)
{
  Mutex::scoped_lock lock(localCacheCollectionFile_mutex);
  localCacheFileName2originalFullPathName[localCacheFileName] = originalFullPathName;
  ofstream ost(localCacheCollectionFile.c_str(), ios::app);
  if(!ost) {
    logprintf(0, LOG_ERROR, "Could not append to local cache collection file.\n", localCacheCollectionFile.c_str());
    return;
  }
  vector<string> csv;
  csv.resize(2);
  csv[0] = localCacheFileName;
  csv[1] = originalFullPathName;
  ost << convertToCSV(csv) << "\n";
  localCacheCollection_SolidText_isDirty = true;
}

void CacheGarbageCollection::removeLocalFileCollection(const std::string& localCacheFileName)
{
  Mutex::scoped_lock lock(localCacheCollectionFile_mutex);
  map<string, string>::iterator it = localCacheFileName2originalFullPathName.find(localCacheFileName);
  if(it != localCacheFileName2originalFullPathName.end()) {
    localCacheFileName2originalFullPathName.erase(it);
    localCacheCollection_SolidText_isDirty = true;
  }
}

bool CacheGarbageCollection::canUseLocalCacheNameForLocalFileCollection(const std::string& localCacheFileName, const std::string& originalFullPathName)
{
  Mutex::scoped_lock lock(localCacheCollectionFile_mutex);
  map<string, string>::iterator it = localCacheFileName2originalFullPathName.find(localCacheFileName);
  if(it == localCacheFileName2originalFullPathName.end()) {
    return true;
  }
  return it->second == originalFullPathName;
}

void CacheGarbageCollection::updateSolidText_internal_shouldBeCalledWithMutexLocked()
{
  if(localCacheCollection_SolidText_isDirty) {
    vector<char>   solidText;
    vector<string> csv;
    csv.resize(2);
    for(map<string, string>::const_iterator cit = localCacheFileName2originalFullPathName.begin(); cit != localCacheFileName2originalFullPathName.end(); ++cit) {
      csv[0] = cit->first;
      csv[1] = cit->second;
      const string& line = convertToCSV(csv);
      solidText.insert(solidText.end(), line.begin(), line.end());
      solidText.push_back('\n');
    }
    localCacheCollection_SolidText.assign(solidText.begin(), solidText.end());
    localCacheCollection_SolidText_isDirty = false;
  }
}

std::string CacheGarbageCollection::getSolidText(std::string::size_type start, std::string::size_type end)
{
  return localCacheCollection_SolidText.substr(start, end);
}

std::string::size_type CacheGarbageCollection::getSolidTextSize()
{
  return localCacheCollection_SolidText.size();
}

void CacheGarbageCollection::updateSolidText()
{
  Mutex::scoped_lock lock(localCacheCollectionFile_mutex);
  updateSolidText_internal_shouldBeCalledWithMutexLocked();
}

void CacheGarbageCollection::saveLocalFileCollection()
{
  Mutex::scoped_lock lock(localCacheCollectionFile_mutex);
  const string localCacheCollectionFile_New = localCacheCollectionFile + ".swp";
  {
    ofstream ost(localCacheCollectionFile_New.c_str());
    if(!ost) {
      logprintf(0, LOG_ERROR, "Could not save local cache collection file to temporary file '%s'.\n", localCacheCollectionFile_New.c_str());
      return;
    }
    vector<string> csv;
    csv.resize(2);
    for(map<string, string>::const_iterator cit = localCacheFileName2originalFullPathName.begin(); cit != localCacheFileName2originalFullPathName.end(); ++cit) {
      csv[0] = cit->first;
      csv[1] = cit->second;
      ost << convertToCSV(csv) << "\n";
    }
  }
  const int result = rename(localCacheCollectionFile_New.c_str(), localCacheCollectionFile.c_str());
  if(result != 0) {
    logprintf(0, LOG_ERROR, "Could not rename the temporary file '%s' to '%s'.\n", localCacheCollectionFile_New.c_str(), localCacheCollectionFile.c_str());
    return;
  }
}

void CacheGarbageCollection::initLocalFileCollection()
{
  int numberOfDeletedEntries = 0;
  {
    Mutex::scoped_lock lock(localCacheCollectionFile_mutex);

    localCacheFileName2originalFullPathName.clear();
    ifstream ist(localCacheCollectionFile.c_str());
    if(!ist) {
      const bool fileWasNotFound = access(localCacheCollectionFile.c_str(), F_OK) != 0;
      if(fileWasNotFound) {
	lock.unlock(); // avoid recursive lock.
	logprintf(0, LOG_INFO, "Not found local cache collection file '%s'. Create an empty one.\n", localCacheCollectionFile.c_str());
	saveLocalFileCollection();
      } else {
	logprintf(0, LOG_ERROR, "Could not open local cache collection file '%s'. Use an empty list.\n", localCacheCollectionFile.c_str());
      }
      return;
    }
    string line;
    vector<string> cvs;
    while(getline(ist, line)) {
      if(line.empty())
	continue;
      CSVParse(line, cvs);
      if(cvs.size() >= 2) {
	const string& localCacheFileName = cvs[0];
	const string& originalFullPathName = cvs[1];
	const bool localFileFound = access(localCacheFileName.c_str(), F_OK) == 0;
	if(localFileFound) {
	  if(localCacheFileName2originalFullPathName.count(localCacheFileName))
	    numberOfDeletedEntries++;
	  localCacheFileName2originalFullPathName[localCacheFileName] = originalFullPathName;
	} else {
	  numberOfDeletedEntries++;
	}
      }
    }
  }
  if(0 < numberOfDeletedEntries) {
    logprintf(0, LOG_INFO, "%d files are removed from the local cache list.\n", numberOfDeletedEntries );
    saveLocalFileCollection();
  }
}

void CacheGarbageCollection::init(const char* cacheRootDirectory,
				  const int softLimitInKBytes,
				  const int hardLimitInKBytes     )
{
  this->cacheRootDirectory = cacheRootDirectory;
  { // remove trailing '/'
    string& s = this->cacheRootDirectory;
    if(!s.empty() && s[s.size() - 1] == '/') {
      s.resize(s.size() - 1);
    }
  }
  this->localCacheCollectionFile = this->cacheRootDirectory + "/localfiles.csv";

  if(softLimitInKBytes != AUTO) {
    this->softLimitInKBytes   = softLimitInKBytes;
  } else {
    struct statfs sfs;
    if(statCacheRootDir(&sfs)) {
      this->softLimitInKBytes = (long)((long long)sfs.f_bsize * sfs.f_blocks * 0.2 / 1024);
    } else {
      this->softLimitInKBytes = 30 * 1024 * 1024; // 30Gbytes
    }
  }

  if(hardLimitInKBytes != AUTO) {
    this->hardLimitInKBytes  = hardLimitInKBytes;
  } else {
    struct statfs sfs;
    if(statCacheRootDir(&sfs)) {
      this->hardLimitInKBytes = (long)((long long)sfs.f_bsize * sfs.f_blocks * 0.1 / 1024);
    } else {
      this->softLimitInKBytes = 10 * 1024 * 1024; // 10Gbytes
    }
  }

  initLocalFileCollection();
  this->localCacheCollection_SolidText_isDirty = true;
  this->initialized        = true;
}

struct File {
  string name;
  long long   size;
  time_t      lastAccessTime;
  uid_t       owner;

  File() {
    size = -1ll;
  }
  File(const string& name) : name(name) {
    size = -1ll;
  }
};

inline bool sortByAccessTime(File* a, File* b) {
  return a->lastAccessTime < b->lastAccessTime;
}

std::string CacheGarbageCollection::fullPath(const std::string& path)
{
  return cacheRootDirectory + "/" + path;
}

bool CacheGarbageCollection::statCacheRootDir(struct statfs* sfs) {
  const int result = statfs(cacheRootDirectory.c_str(), sfs);
  if(result != 0) {
    logprintf(0, LOG_ERROR, "statfs failed. errno=%d, path=%s\n", errno, cacheRootDirectory.c_str());
    return false;
  }
  return true;
}

void CacheGarbageCollection::collect(const Cache_LockedFileChecker& clfc)
{
  if(!initialized) {
    logprintf(0, LOG_ERROR, "Garbage collection maneger is called without initialization.\n");
    return;
  }
  Mutex::scoped_lock lock(garbageCollection_mutex);
  logprintf(0, LOG_INFO, "Garbage collection started\n");

  // Step 1) List the files in my cache directory.
  DIR* dirp = opendir(cacheRootDirectory.c_str());
  if(dirp == NULL) {
    logprintf(0, LOG_ERROR, "Could not opendir '%s'\n", cacheRootDirectory.c_str());
    return;
  }
  vector<File> files;
  {
    struct dirent oneEntry;
    struct dirent *result;
    int resultStatus;
    while((resultStatus = readdir_r(dirp, &oneEntry, &result)) == 0) {
      if(result == NULL)
	break; // reached the end
      if(strcmp(oneEntry.d_name, "tgefslog") != 0 && oneEntry.d_name[0] != '.') { // exclude log file, '.', '..' and other hidden files
	files.push_back(File(oneEntry.d_name));
      }
    }
    closedir(dirp);
    if(resultStatus != 0) {
      logprintf(0, LOG_ERROR, "readdir failed. (errno=%d)\n", result);
      return;
    }
  }
  // Step 2) Examine the file size for each file which is owned by myself.
  for(unsigned int i = 0; i < files.size(); i++) {
    File& f = files[i];
    struct stat statResult;
    const int result = stat(fullPath(f.name).c_str(), &statResult);
    if(result == -1) {
      logprintf(0, LOG_ERROR, "stat failed during GC. (errno=%d)\n", errno);
      return;
    }
    f.size           = statResult.st_size;
    f.lastAccessTime = std::max<time_t>(statResult.st_atime, statResult.st_mtime);
    f.owner          = statResult.st_uid;
  }
  long long totalSizeUsed = 0;
  int numberOfFiles = 0;
  {
    const uid_t myUID = getuid();
    for(unsigned int i = 0; i < files.size(); i++) {
      const File& f = files[i];
      if(f.owner == myUID) {
	numberOfFiles++;
	totalSizeUsed += f.size;
      }
    }
  }
  {
    const long long totalSizeUsedInKB = (totalSizeUsed + 1023) / 1024;
    const int totalSizeUsedInMB       = totalSizeUsedInKB / 1024;
    if(totalSizeUsedInKB < 1024) {
      logprintf(0, LOG_INFO, "%dMbytes (%d files) are used.\n", totalSizeUsedInMB, numberOfFiles);
    } else {
      logprintf(0, LOG_INFO, "%d.%02dGbytes (%d files) are used.\n", totalSizeUsedInMB / 1000, (totalSizeUsedInMB % 1000) / 10, numberOfFiles);
    }
  }
  // Step 3) Determine which threshold we will use.
  long long garbageSizeToBeCollected = 0ll;
  {
    long long limitSize = -1ll;
    struct statfs sfs;
    if(!statCacheRootDir(&sfs)) {
      logprintf(0, LOG_ERROR, "statfs failed for cachrroot '%s'\n", cacheRootDirectory.c_str());
      return;
    }
    const long long totalCapacity     = sfs.f_bsize * (long long)sfs.f_blocks;
    const long long availableCapacity = sfs.f_bsize * (long long)sfs.f_bavail;
    const long long thresholdCapacity = (long long)(totalCapacity * (1.0 - GC_apply_hardLimit_ratio_of_HDD_usage));
    logprintf(3, LOG_DEBUG, "Total capacity = %lld bytes\n", totalCapacity);
    logprintf(3, LOG_DEBUG, "Avail capacity = %lld bytes\n", availableCapacity);
    logprintf(3, LOG_DEBUG, "Thres capacity = %lld bytes\n", thresholdCapacity);
    if(availableCapacity < thresholdCapacity) {
      limitSize = softLimitInKBytes * 1024ll;
      logprintf(2, LOG_DEBUG, "Use soft limit, %lld bytes\n", limitSize);
    } else {
      limitSize = hardLimitInKBytes * 1024ll;
      logprintf(2, LOG_DEBUG, "Use hard limit, %lld bytes\n", limitSize);
    }
    garbageSizeToBeCollected = max<long long>(0ll, totalSizeUsed - limitSize);
    logprintf(2, LOG_DEBUG, "%lld bytes to be collected\n", garbageSizeToBeCollected);
  }
  // Step 4) Delete too old files, other files are push_back'ed into candidates if they are mine.
  int       numberOfDeletedFiles   = 0;
  long long totalSizeOfDeleteFiles = 0ll;

  vector<File*> candidates;
  {
    const int SECONDS_PER_DAY = 86400;
    const time_t currentDate  = time(NULL);
    const time_t oldDate      = currentDate - SECONDS_PER_DAY * GC_delete_cache_if_this_number_of_days_passed;
    const uid_t  myUID        = getuid();
    for(unsigned int i = 0; i < files.size(); i++) {
      File& file = files[i];
      if(file.owner != myUID)
	continue;
      const string& fullPathName = fullPath(file.name);
      if(clfc.isLockedFile(fullPathName)) {
	logprintf(0, LOG_DEBUG, "%s is opened.\n", file.name.c_str());
	continue;
      }
      if(file.lastAccessTime < oldDate) {
	const int result = unlink(fullPathName.c_str());
	removeLocalFileCollection(fullPathName);
	if(result == 0) {
	  logprintf(3, LOG_DEBUG, "deleted %s because it is too old\n", file.name.c_str());
	  numberOfDeletedFiles++;
	  totalSizeOfDeleteFiles += file.size;
	} else {
	  logprintf(0, LOG_ERROR, "tried to delete '%s' because it's too old, but it failed.\n", file.name.c_str());
	}
      } else {
	candidates.push_back(&file);
      }
    }
  }
  // Step 5) Sort candidates by their last access time
  {
    sort(candidates.begin(), candidates.end(), sortByAccessTime);
    for(unsigned int idx = 0; totalSizeOfDeleteFiles < garbageSizeToBeCollected && idx < candidates.size(); idx++) {
      File& file = *candidates[idx];
      const string& fullPathName = fullPath(file.name);
      const int result = unlink(fullPathName.c_str());
      removeLocalFileCollection(fullPathName);
      if(result == 0) {
	logprintf(3, LOG_DEBUG, "deleted %s because it is old and unused.\n", file.name.c_str());
	numberOfDeletedFiles++;
	totalSizeOfDeleteFiles += file.size;
      } else {
	logprintf(0, LOG_ERROR, "tried to delete '%s' because it's old and unused, but it failed.\n", file.name.c_str());
      }
    }
  }
  // Step 6) Report to the log file
  logprintf(0, LOG_INFO, "Garbage collection finished. %d files are deleted. (%lld bytes in total)\n", numberOfDeletedFiles, totalSizeOfDeleteFiles);
  // Step 7) Reflesh Local File Collection CSV
  saveLocalFileCollection();
}
