/*
    TGE-FS: TGE Filesystem
    Copyright (C) 2007       Masahiro Kasahara <masahiro@kasahara.ws>

    This work is derived from fusexmp.c in FUSE distribution,
    which is developed by Miklos Szeredi <miklos@szeredi.hu>,
    and is distributed under the GNU GPL.
*/

#if HAVE_CONFIG
 #include "config.h"
#endif

#define FUSE_USE_VERSION 26
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <utime.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <sys/fsuid.h>
#include <limits.h>
#include <vector>
#include <string>
#include <map>
#include <set>
#include "sha2.h"
#include "pmutex.h"
#include <iostream>
#include "libtgelock.h"
#include "tge_fcopy.h"
#include "tge_log.h"
#include "tge_compctl.h"
#include "tge_cache.h"
#include "tge_appconfig.h"

using namespace std;

static const char* versionString = "1.10";
static char my_home[PATH_MAX + PATH_MAX];
static char canonicalMountPoint[PATH_MAX + PATH_MAX];
static int  canonicalMountPoint_length = 0;

static CompressionControl compressionControl;

#define FH_SPECIAL_FILE ((uint64_t)-1)

class SETFSID {
  static Mutex setFSID_mutex;
  bool locked;
  bool uid_save;
  bool gid_save;
  enum {
    ROOT_USER = 0
  };
public:
  SETFSID() {
    if(getuid() == ROOT_USER) {
      struct fuse_context *fc = fuse_get_context();
      setFSID_mutex.lock();
      locked = setFSID_mutex.islocked();
      if(locked) {
	uid_save = setfsuid(fc->uid);
	gid_save = setfsgid(fc->gid);
      } else {
        uid_save = getuid();
        gid_save = getgid();
      }
    } else {
      locked = false;
      uid_save = getuid();
      gid_save = getgid();
    }
  }
  ~SETFSID() {
    if(getuid() == ROOT_USER) {
      if(locked) {
	setfsuid(uid_save);
	setfsgid(gid_save);
	setFSID_mutex.unlock();
      }
    }
  }
};

Mutex SETFSID::setFSID_mutex;

class LocalFile {
public:
  string realFileName;
  string cachedFileName;
  bool   isDirty;
  bool   isCached;
  bool   isOriginalFileCompressed;
  LocalFile() {
    isDirty  = false;
    isCached = false;
    isOriginalFileCompressed = false;
  }
  LocalFile(const string& realFileName, const string& cachedFileName, const bool isCached)
    : realFileName(realFileName), isCached(isCached), isOriginalFileCompressed(false) {
    isDirty  = false;
  }
  LocalFile(const string& realFileName, const string& cachedFileName, const bool isCached, const bool isOriginalFileCompressed)
    : realFileName(realFileName), isCached(isCached), isOriginalFileCompressed(isOriginalFileCompressed) {
    isDirty  = false;
  }
};

class CachedLocalFiles {
  void createCacheDir();

  set<string>              lockedLocalFiles;
  Mutex                    lockedLocalFiles_mutex;
  ConditionVariable        lockedLocalFiles_cond;
public:
  class LocalCacheFileLock;
private:
  friend class LocalCacheFileLock;
  inline void lockLCF()             { lockedLocalFiles_mutex.lock();                      }
  inline void unlockLCF()           { lockedLocalFiles_mutex.unlock();                    }
  inline void waitLCFStatChange()   { lockedLocalFiles_cond.wait(lockedLocalFiles_mutex); }
  inline void notifyLCFStatChange() { lockedLocalFiles_cond.signalAll();                  }
  inline bool alreadyExistLCFLock(const string& filename) {
    return 0 < lockedLocalFiles.count(filename);
  }
  inline void insertLCFLock(const string& filename) {
    lockedLocalFiles.insert(filename);
  }
  inline void removeLCFLock(const string& filename) {
    lockedLocalFiles.erase(filename);
    lockedLocalFiles_cond.signalAll();
  }
public:
  class LocalCacheFileLock {
    CachedLocalFiles& clf;
    string localCacheFileName;
    bool   isLocked;
  public:
    LocalCacheFileLock(CachedLocalFiles& clf, const char *filename) : clf(clf) {
      localCacheFileName = filename;
      clf.lockLCF();
      while(clf.alreadyExistLCFLock(localCacheFileName)) clf.waitLCFStatChange();
      clf.insertLCFLock(localCacheFileName);
      isLocked = true;
      clf.unlockLCF();
    }
    void unlock() {
      if(!isLocked) return;
      clf.lockLCF();
      isLocked = false;
      clf.removeLCFLock(localCacheFileName);
      clf.unlockLCF();
    }
    ~LocalCacheFileLock() { unlock(); }
  };

private:
  LocalFile dummy;
  map<uint64_t, LocalFile>     localFH2LocalFile;
  Mutex                        localFH2LocalFile_mutex; 
  map<std::string, LocalFile*> localFileName2LocalFile;
public:
  class LFLock;
private:
  friend class LFLock;
  void lockLF() {    localFH2LocalFile_mutex.lock();   }
  void unlockLF() {  localFH2LocalFile_mutex.unlock(); }
public:
  class LFLock : public Cache_LockedFileChecker {
    CachedLocalFiles& clf;
    bool isLocked;
  public:
    inline LFLock(CachedLocalFiles& clf) : clf(clf) { clf.lockLF(); isLocked = true; }
    inline void unlock() { if(isLocked){ clf.unlockLF(); isLocked = false; } }
    void setDirtyFlag(const uint64_t fh, const bool flag) {
      map<uint64_t, LocalFile>::iterator it = clf.localFH2LocalFile.find(fh);
      if(it == clf.localFH2LocalFile.end())
	return;
      it->second.isDirty = flag;
    }
    LocalFile& getLF(const uint64_t fh) {
      map<uint64_t, LocalFile>::iterator it = clf.localFH2LocalFile.find(fh);
      return it != clf.localFH2LocalFile.end() ? it->second : clf.dummy;
    }
    void removeLF(const uint64_t fh) {
      map<uint64_t, LocalFile>::iterator it = clf.localFH2LocalFile.find(fh);
      if(it == clf.localFH2LocalFile.end())
	return;
      const LocalFile& lh = it->second;
      clf.localFileName2LocalFile.erase(lh.cachedFileName);
      clf.localFH2LocalFile.erase(it);
    }
    virtual bool isLockedFile(const std::string& filename) const {
      return 0 < clf.localFileName2LocalFile.count(filename);
    }
    void createLF(const uint64_t fh, const LocalFile& lf) {
      LocalFile& p = clf.localFH2LocalFile[fh] = lf;
      clf.localFileName2LocalFile[lf.cachedFileName] = &p;
    }
    virtual ~LFLock() {
      unlock();
    }
  };

  string createCachedFileName(const char * virtualPath) const
  {
    SHA256 sha;
    vector<unsigned char> digest;
    sha.doAll(reinterpret_cast<const unsigned char *>(virtualPath), strlen(virtualPath), digest);
    char buffer[256];
    char* p = buffer;
    *p++ = '/';
    for(unsigned int i = 0; i < digest.size(); i++) {
      *p++ = "0123456789ABCDEF"[(digest[i] >> 4) & 0xf];
      *p++ = "0123456789ABCDEF"[ digest[i]       & 0xf];
    }
    *p = '\0';
    return string(cacheDirectoryRoot) + buffer;
  }

  void init() {
    createCacheDir();
  }
  CachedLocalFiles() { }
};

static CachedLocalFiles       cachedLocalFiles;
static CacheGarbageCollection cacheGarbageCollection;

//----------------------------------------------------------------------
static inline bool isRecursiveFilePath(const char *path)
{
  logprintf(4, LOG_DEBUG, "recursive path check %s\n", path);
  if(strncmp(path, canonicalMountPoint, canonicalMountPoint_length - 1) == 0) {
    logprintf(4, LOG_DEBUG, "still remains possibility\n");
    if(path[canonicalMountPoint_length] =='/') {
      logprintf(4, LOG_DEBUG, "recursive\n");
      return true;
    }
    if(path[canonicalMountPoint_length] =='\0') {
      logprintf(4, LOG_DEBUG, "recursive\n");
      return true;
    }
  }
  return false;
}

static inline bool isSpecialPath(const char *path)
{
  if(strncmp(path, "/proc", 5) == 0) {
    const char *suffix = path + 5;
    if(*suffix == '\0' || *suffix == '/')
      return true;
  }
  return false;
}

static inline const char* getSpecialPath(const char *path)
{
  if(isSpecialPath(path)) {
    return path + 5;
  } else {
    return path;
  }
}

static long long getFileSize(const char *path)
{
  struct stat s;
  const int result = stat(path, &s);
  if(result != 0) {
    return -1;
  }
  return s.st_size;
}

static string createCachedFileName(const char *path)
{
  const string firstCandidateForCachedLocalFileName = cachedLocalFiles.createCachedFileName(path);
  if(cacheGarbageCollection.canUseLocalCacheNameForLocalFileCollection(firstCandidateForCachedLocalFileName, path))
    return firstCandidateForCachedLocalFileName;
  return "";
}

//----------------------------------------------------------------------
static string createFSAttr()
{
  string retval;
  {
    char buffer[256];
    sprintf(buffer, "version=%s\n", versionString);
    retval += buffer;
    sprintf(buffer, "loglevel=%d\n", getloglevel());
    retval += buffer;
  }
  return retval;
}

static int tgefs_getattr(const char *path, struct stat *stbuf)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  logprintf(3, LOG_DEBUG, "getattr for file %s\n", path);
  if(isSpecialPath(path)) {
    const char* spath = getSpecialPath(path);
    logprintf(2, LOG_DEBUG, "getattr for special file %s (sname=%s)\n", path, spath);
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode    = 0666 | S_IFREG;
    stbuf->st_nlink   = 1;
    struct fuse_context *fc = fuse_get_context();
    stbuf->st_uid     = fc->uid;
    stbuf->st_gid     = fc->gid;
    const time_t currentTime = time(NULL);
    stbuf->st_rdev    = 0;
    stbuf->st_blksize = 32768;
    stbuf->st_blocks  = 1;
    stbuf->st_atime   = currentTime;
    stbuf->st_mtime   = currentTime;
    stbuf->st_ctime   = currentTime;
    if(strcmp(spath, "/tgefs") == 0) {
      string s = createFSAttr();
      stbuf->st_size    = s.size();
      return 0;
    }
    if(strcmp(spath, "/tgefslog") == 0) {
      int sz = 0;
      for(int t = getloglevel(); 0 < t; t /= 10)
	sz++;
      if(sz == 0)
	sz++;
      stbuf->st_size    = sz + 1;
      return 0;
    }
    if(strcmp(spath, "/tgefscache") == 0) {
      stbuf->st_size    = cacheGarbageCollection.getSolidTextSize();
      return 0;
    }
    if(strcmp(spath, "") != 0) {
      return -ENOENT; // file not found
    }
  }
  {
    SETFSID setfsid;
    const string ccfn = createCachedFileName(path);
    if(!ccfn.empty()) {
      CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
      const int res = lstat(path, stbuf);
      if (res == -1) return -errno;
    } else {
      const int res = lstat(path, stbuf);
      if (res == -1) return -errno;
    }
  }
  
  const bool isRegularFile = (stbuf->st_mode & S_IFMT) == S_IFREG;
  if(isRegularFile) {
    logprintf(3, LOG_DEBUG, "Regularfile, will check if the file is compressed\n");
    char compressionType;
    long long fileSize;
    bool isCompressedFile;
    {
      SETFSID setfsid;
      isCompressedFile = is_lzo_compressed_file(path, &compressionType, &fileSize);
    }
    if(isCompressedFile) {
      logprintf(3, LOG_DEBUG, "The file is compressed. The file size is modified to %lld\n", fileSize);
      stbuf->st_size = fileSize;
    }
  }
  return 0;
}

static int tgefs_access(const char *path, int mask)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    logprintf(2, LOG_DEBUG, "access for special file %s\n", path);
    const char* spath = getSpecialPath(path);
    if(strcmp(spath, "/tgefs") == 0) {
      return 0;
    }
    if(strcmp(spath, "/tgefslog") == 0) {
      return 0;
    }
    if(strcmp(spath, "/tgefscache") == 0) {
      return 0;
    }
    if(strcmp(spath, "") != 0) {
      return -ENOENT; // file not found
    }
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    const int res = access(path, mask);
    if (res == -1) return -errno;
  } else {
    const int res = access(path, mask);
    if (res == -1) return -errno;
  }
  return 0;
}

static int tge_strncpy(const char* src, char* dest, size_t size)
{
  char* p = dest;
  while(*src && 0 < size--) {
    *p++ = *src++;
  }
  return p - dest;
}

static int tgefs_readlink(const char *path, char *buf, size_t size)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    const char* spath = getSpecialPath(path);
    if(strcmp(spath, "/tgefs") == 0) {
      return tge_strncpy("/proc/tgefs", buf, size);
    }
    if(strcmp(spath, "/tgefslog") == 0) {
      return tge_strncpy("/proc/tgefslog", buf, size);
    }
    if(strcmp(spath, "/tgefscache") == 0) {
      return tge_strncpy("/proc/tgefscache", buf, size);
    }
    if(strcmp(spath, "") != 0) {
      return -ENOENT; // file not found
    }
  }
  SETFSID setfsid;
  const int res = readlink(path, buf, size - 1);
  if (res == -1) return -errno;
  buf[res] = '\0';
  return 0;
}

static int tgefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    const char* spath = getSpecialPath(path);
    if(strcmp(spath, "") != 0) {
      return -ENOENT; // file not found
    }
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_mode = 0777;
    if (filler(buf, "tgefs", &st, 0)) return 0;
    if (filler(buf, "tgefslog", &st, 0)) return 0;
    if (filler(buf, "tgefscache", &st, 0)) return 0;
    return 0;
  }
  SETFSID setfsid;
  DIR *dp = opendir(path);
  if (dp == NULL) return -errno;

  struct dirent *de;
  while ((de = readdir(dp)) != NULL) {
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_ino  = de->d_ino;
    st.st_mode = de->d_type << 12;
    if (filler(buf, de->d_name, &st, 0)) break;
  }

  closedir(dp);
  return 0;
}

static int tgefs_mknod(const char *path, mode_t mode, dev_t rdev)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    return -EPERM;
  }
  const string ccfn = createCachedFileName(path);
  int res;
  SETFSID setfsid;
  /* On Linux this could just be 'mknod(path, mode, rdev)' but this
     is more portable. However, other part of the software is not
     written as portable. */
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    if (S_ISREG(mode)) {
      res = open(path, O_CREAT | O_EXCL | O_WRONLY | O_LARGEFILE, mode);
      if (res >= 0) res = close(res);
    } else if (S_ISFIFO(mode)) {
      res = mkfifo(path, mode);
    } else {
      res = mknod(path, mode, rdev);
    }
  } else {
    if (S_ISREG(mode)) {
      res = open(path, O_CREAT | O_EXCL | O_WRONLY | O_LARGEFILE, mode);
      if (res >= 0) res = close(res);
    } else if (S_ISFIFO(mode)) {
      res = mkfifo(path, mode);
    } else {
      res = mknod(path, mode, rdev);
    }
  }
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_mkdir(const char *path, mode_t mode)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    return -EPERM;
  }
  SETFSID setfsid;
  const int res = mkdir(path, mode);
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_unlink(const char *path)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    return -EPERM;
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  int res;
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    res = unlink(path);
  } else {
    res = unlink(path);
  }
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_rmdir(const char *path)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    return -EPERM;
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  int res;
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    res = rmdir(path);
  } else {
    res = rmdir(path);
  }
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_symlink(const char *from, const char *to)
{
  if(isRecursiveFilePath(from))
    return -ENOENT;
  if(isRecursiveFilePath(to))
    return -EPERM;
  if(isSpecialPath(from)) {
    return -EPERM;
  }
  SETFSID setfsid;
  int res;
  const string ccfn1 = createCachedFileName(from);
  const string ccfn2 = createCachedFileName(to);
  if(!ccfn1.empty() && !ccfn2.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock1(cachedLocalFiles, ccfn1.c_str());
    CachedLocalFiles::LocalCacheFileLock lcflock2(cachedLocalFiles, ccfn2.c_str());
    res = symlink(from, to);
  } else {
    res = symlink(from, to);
  }
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_rename(const char *from, const char *to)
{
  if(isRecursiveFilePath(from))
    return -ENOENT;
  if(isRecursiveFilePath(to))
    return -EPERM;
  if(isSpecialPath(from)) {
    return -EPERM;
  }
  logprintf(2, LOG_DEBUG, "rename for file %s to %s\n", from, to);
  SETFSID setfsid;
  int res;
  const string ccfn1 = createCachedFileName(from);
  const string ccfn2 = createCachedFileName(to);
  if(!ccfn1.empty() && !ccfn2.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock1(cachedLocalFiles, ccfn1.c_str());
    CachedLocalFiles::LocalCacheFileLock lcflock2(cachedLocalFiles, ccfn2.c_str());
    res = rename(from, to);
  } else {
    res = rename(from, to);
  }
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_link(const char *from, const char *to)
{
  if(isRecursiveFilePath(from))
    return -ENOENT;
  if(isRecursiveFilePath(to))
    return -EPERM;
  if(isSpecialPath(from)) {
    return -EPERM;
  }
  SETFSID setfsid;
  int res;
  const string ccfn1 = createCachedFileName(from);
  const string ccfn2 = createCachedFileName(to);
  if(!ccfn1.empty() && !ccfn2.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock1(cachedLocalFiles, ccfn1.c_str());
    CachedLocalFiles::LocalCacheFileLock lcflock2(cachedLocalFiles, ccfn2.c_str());
    res = link(from, to);
  } else {
    res = link(from, to);
  }
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_chmod(const char *path, mode_t mode)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    return -EPERM;
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  int res;
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    res = chmod(path, mode);
  } else {
    res = chmod(path, mode);
  }
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_chown(const char *path, uid_t uid, gid_t gid)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    return -EPERM;
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    const int res = lchown(path, uid, gid);
    if (res == -1) return -errno;
  } else {
    const int res = lchown(path, uid, gid);
    if (res == -1) return -errno;
  }
  return 0;
}

static int tgefs_truncate(const char *path, off_t size)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    const char* spath = getSpecialPath(path);
    logprintf(2, LOG_DEBUG, "truncate for special file %s (sname=%s)\n", path, spath);
    if(strcmp(spath, "/tgefslog") == 0) {
      if(size == 0)
	return 0;
    }
    if(strcmp(spath, "/tgefscache") == 0) {
      if(size == 0)
	return 0;
    }
    return -EPERM;
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  int res;
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    res = truncate(path, size);
  } else {
    res = truncate(path, size);
  }
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_utimens(const char *path, const struct timespec ts[2])
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    return -EPERM;
  }
  logprintf(2, LOG_DEBUG, "utimens for file %s\n", path);
  SETFSID setfsid;
  struct timeval tv[2];
  tv[0].tv_sec  = ts[0].tv_sec;
  tv[0].tv_usec = ts[0].tv_nsec / 1000;
  tv[1].tv_sec  = ts[1].tv_sec;
  tv[1].tv_usec = ts[1].tv_nsec / 1000;

  const string ccfn = createCachedFileName(path);
  int res;
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    res = utimes(path, tv);
  } else {
    res = utimes(path, tv);
  }
  if (res == -1) return -errno;
  return 0;
}

//------------------------------------------------------------------------------
static bool touchByAnotherFilesDate(const char* fileToTouch, const char* referenceFile)
{
  struct stat refstat;
  const int statResult = stat(referenceFile, &refstat);
  if(statResult == -1)
    return false;
  struct utimbuf times;
  times.actime   = refstat.st_atime;
  times.modtime  = refstat.st_mtime;
  const int utimeResult = utime(fileToTouch, &times);
  if(utimeResult == -1)
    return false;
  return true;
}

static int getMyFilePermission(const struct stat &statBuffer)
{
  struct fuse_context *fc = fuse_get_context();
  if(fc->uid == 0)
    return 0600;
  if(statBuffer.st_uid == fc->uid) {
    return (statBuffer.st_mode & 0700) >> 6; // owner
  }
  if(statBuffer.st_gid == fc->gid) {
    return (statBuffer.st_mode & 0070) >> 3; // group
  }
  return   (statBuffer.st_mode & 0007);      // other
}

static bool copyFileIfUpdatedOrFirstTime(const char *srcPath, const char *destPath, bool *isSourceFileCompressed)
{
  if(isSourceFileCompressed != NULL)
    *isSourceFileCompressed = false;
  const bool foundSourceFile      = access(srcPath , F_OK) == 0;
  if(!foundSourceFile) return false;
  const bool foundDestinationFile = access(destPath, F_OK) == 0;

  struct stat srcStatBuf, destStatBuf;
  const int srcStatResult = stat(srcPath, &srcStatBuf);
  if(srcStatResult != 0)
    return false; // stat failed. maybe it can't be copied either.
  if(foundDestinationFile) {
    const int destStatResult = lstat(destPath, &destStatBuf); // the destination may not be a symbolic link.
    if(destStatResult == 0) {
      // stat succeeded
      const bool isSymLink            = (destStatBuf.st_mode & S_IFLNK) == S_IFLNK;
      if(isSymLink) {
	logprintf(2, LOG_DEBUG, "Dest file %s is a symlink. Will remove it.\n", destPath);
	if(unlink(destPath) == -1) {
	  logprintf(0, LOG_ERROR, "Dest file %s could not be removed.\n", destPath);
	  return false;
	}
	logprintf(2, LOG_DEBUG, "Successfully removed.\n");
      } else {
	// const bool areTheSizesSame      = srcStatBuf.st_size  == destStatBuf.st_size;
	const bool isTheLocalCacheNewer = srcStatBuf.st_mtime <= destStatBuf.st_mtime;
	if(/*areTheSizesSame && (NOTE: file size may not necessarily be same particular if the original file is compressed)*/ isTheLocalCacheNewer) {
	  // no need to copy
	  const int srcPermission  = getMyFilePermission(srcStatBuf);
	  const int destPermission = getMyFilePermission(destStatBuf);
	  if(srcPermission != destPermission) {
	    const int desiredPermission = srcPermission << 6;
	    logprintf(2, LOG_DEBUG, "Chmod %s from %o to %o\n", destPath, destStatBuf.st_mode & 07777, desiredPermission);
	    const int result = chmod(destPath, desiredPermission);
	    if(result != 0) {
	      logprintf(0, LOG_ERROR, "Failed to chmod %s from %o to %o\n", destPath, destStatBuf.st_mode & 07777, desiredPermission);
	      return false;
	    }
	  }
	  if(isSourceFileCompressed != NULL) {
	    *isSourceFileCompressed = is_lzo_compressed_file(srcPath);
	    logprintf(2, LOG_DEBUG, "Original file is %s\n", *isSourceFileCompressed ? "compressed" : "uncompressed");
	  }
	  return true;
	}
      }
    }
  }
  logprintf(2, LOG_DEBUG, "Copy %s to %s\n", srcPath, destPath);
  const int desiredPermission = getMyFilePermission(srcStatBuf) << 6;
  const bool useTGELock       = minimumFileSizeToEnableLock <= srcStatBuf.st_size;
  {
    TGELock lock(tgeLockdServer, tgeLockdPort);
    if(useTGELock) {
      lock.lock();
      if(lock.isFailed()) {
	logprintf(0, LOG_ERROR, "Lock error : %s\n", lock.getErrorMessage().c_str());
      } else {
	logprintf(3, LOG_INFO,  "Locked tgelockd\n");
      }
    }
    if(!copyFileWithDecompression(srcPath, destPath, desiredPermission, isSourceFileCompressed)) {
      if(useTGELock) {
	lock.unlock();
      }
      logprintf(0, LOG_ERROR, "Copy from remote '%s' to local '%s' failed.\n", srcPath, destPath);
      return false;
    }
    if(useTGELock) {
      lock.unlock();
    }
  }
  {
    CachedLocalFiles::LFLock lock(cachedLocalFiles);
    cacheGarbageCollection.accessedFile(getFileSize(destPath), lock);
  }
  const bool touchSucceeded = touchByAnotherFilesDate(destPath, srcPath);
  if(!touchSucceeded) {
    logprintf(0, LOG_ERROR, "Touch failed on processing local cache '%s' for '%s'. This may result in severe degrade in cache performance.", destPath, srcPath);
  }
  return true;
}

static int tgefs_open(const char *path, struct fuse_file_info *fi)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    const char* spath = getSpecialPath(path);
    logprintf(2, LOG_DEBUG, "Open special file %s [%s]\n", path, spath);
    if(strcmp(spath, "/tgefs") == 0) {
      fi->fh = FH_SPECIAL_FILE;
      return 0;
    }
    if(strcmp(spath, "/tgefslog") == 0) {
      fi->fh = FH_SPECIAL_FILE;
      return 0;
    }
    if(strcmp(spath, "/tgefscache") == 0) {
      fi->fh = FH_SPECIAL_FILE;
      return 0;
    }
    if(strcmp(spath, "") != -0) {
      return -ENOENT;
    }
  }
  const string ccfn = createCachedFileName(path);
  logprintf(2, LOG_DEBUG, "Open %s [%s]\n", path, ccfn.c_str());
  {
    SETFSID setfsid;
    const string ccfn = createCachedFileName(path);
    int res;
    if(!ccfn.empty()) {
      CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
      res = open(path, fi->flags);
    } else {
      res = open(path, fi->flags);
    }
    if (res == -1) return -errno;
    close(res);
  }
  if(ccfn.empty()) {
    logprintf(0, LOG_ERROR, "Hash conflicted. Fall back to direct access for '%s'\n", path);
    // fall back to direct access, though, hash confliction would occur at fairly low rate.
    int res;
    {
      SETFSID setfsid;
      res = open(path, fi->flags);
      if (res == -1) return -errno;
      fi->fh = res;
      CachedLocalFiles::LFLock lock(cachedLocalFiles);
      lock.createLF(fi->fh, LocalFile(path, ccfn, false));
    }
    logprintf(1, LOG_WARNING, "Use original file, fh = %d\n", res);
  } else {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    bool isOriginalFileCompressed = false;
    const bool succeeded = copyFileIfUpdatedOrFirstTime(path, ccfn.c_str(), &isOriginalFileCompressed);
    if(!succeeded) {
      logprintf(0, LOG_ERROR, "Copy failed. Fall back to direct access for '%s'\n", path);
      int res;
      { // fall back to direct access
	SETFSID setfsid;
	res = open(path, fi->flags);
	if (res == -1) return -errno;
	fi->fh = res;
	CachedLocalFiles::LFLock lock(cachedLocalFiles);
	lock.createLF(fi->fh, LocalFile(path, ccfn, false));
      }
      logprintf(1, LOG_WARNING, "Use original file, fh = %d\n", res);
    } else {
      logprintf(2, LOG_DEBUG, "Cache access.\n");
      int res;
      { // cache access
	res = open(ccfn.c_str(), fi->flags);
	if (res == -1) return -errno;
	fi->fh = res;
	CachedLocalFiles::LFLock lock(cachedLocalFiles);
	lock.createLF(fi->fh, LocalFile(ccfn, ccfn, true, isOriginalFileCompressed));
      }
      cacheGarbageCollection.appendLocalFileCollection(ccfn, path);
      logprintf(2, LOG_DEBUG, "Use cached file, fh = %d\n", res);
    }
  }
  return 0;
}

static int tgefs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  logprintf(3, LOG_DEBUG, "Read %s size=%ld, offset=%ld, fh=%ld\n", path, size, offset, fi->fh);
  if(fi->fh == FH_SPECIAL_FILE) {
    const char* spath = getSpecialPath(path);
    if(strcmp(spath, "/tgefs") == 0) {
      const string fslog = createFSAttr();
      if(offset < (off_t)fslog.size()) {
	const int actualLength = (off_t)fslog.size() - offset;
	const int readLength = actualLength <= (int)size ? actualLength : size;
	memcpy(buf, fslog.data() + offset, readLength);
	return readLength;
      }
      return 0;
    }
    if(strcmp(spath, "/tgefslog") == 0) {
      char buffer[128];
      sprintf(buffer, "%d\n", getloglevel());
      const int bufsize = strlen(buffer);
      if(offset < (off_t)bufsize) {
	const int actualLength = (off_t)bufsize - offset;
	const int readLength   = actualLength <= (int)size ? actualLength : size;
	memcpy(buf, buffer + offset, readLength);
	return readLength;
      }
      return 0;
    }
    if(strcmp(spath, "/tgefscache") == 0) {
      const string& solidSubText = cacheGarbageCollection.getSolidText(offset, offset + size);
      const int copiedSize = std::min<int>(size, solidSubText.size());
      memcpy(buf, solidSubText.data(), copiedSize);
      return copiedSize;
    }
    return -EBADF;
  }
  int res = pread(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;
  return res;
}

static int tgefs_write(const char *path, const char *buf, size_t size,
                       off_t offset, struct fuse_file_info *fi)
{
  logprintf(3, LOG_DEBUG, "Write %s size=%ld, offset=%ld, fh=%ld\n", path, size, offset, fi->fh);
  if(fi->fh == FH_SPECIAL_FILE) {
    const char* spath = getSpecialPath(path);
    if(strcmp(spath, "/tgefs") == 0) {
      return 0;
    }
    if(strcmp(spath, "/tgefslog") == 0) {
      if(size <= 0)
	return 0;
      if(isdigit(buf[0])) {
	const int level = buf[0] - '0';
	loglevel(level);
	logprintf(0, LOG_INFO, "Log level changed to %d\n", level);
      }
      return size;
    }
    if(strcmp(spath, "/tgefscache") == 0) {
      cacheGarbageCollection.updateSolidText();
      return size;
    }
    return -EBADF;
  }
  int res = pwrite(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;
  {
    CachedLocalFiles::LFLock lock(cachedLocalFiles);
    lock.setDirtyFlag(fi->fh, true);
  }
  return res;
}

static int tgefs_release(const char *path, struct fuse_file_info *fi)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  logprintf(2, LOG_DEBUG, "Release %s fh=%ld\n", path, fi->fh);
  if(fi->fh == FH_SPECIAL_FILE) {
    return 0;
  }
  {
    close(fi->fh);
    LocalFile lf;
    {
      CachedLocalFiles::LFLock lock(cachedLocalFiles);
      lf = lock.getLF(fi->fh);
    }
    if(lf.isDirty) {
      logprintf(2, LOG_DEBUG, "Dirty flag set, need to copy back. (Cached = %d)\n", lf.isCached);
      if(lf.isCached) {
	CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, lf.realFileName.c_str());
	logprintf(2, LOG_DEBUG, "Copy %s to %s\n", lf.realFileName.c_str(), path);
	int mode = 0600;
	bool failedStat = false;
	{
	  struct stat origFileStat;
	  const int statResult = stat(path, &origFileStat);
	  if(statResult == -1) {
	    logprintf(0, LOG_ERROR, "stat failed for the original file '%s', which is going to be replaced by '%s'. Using default permission (0600).\n", path, lf.realFileName.c_str());
	    failedStat = true;
	  } else {
	    mode = origFileStat.st_mode & 0777;
	    logprintf(2, LOG_DEBUG, "Using original file mode(%o)\n", mode);
	  }
	}
	struct stat cacheFileStat;
	{
	  const int statResult = stat(lf.realFileName.c_str(), &cacheFileStat);
	  if(statResult == -1) {
	    logprintf(0, LOG_ERROR, "stat failed for the cached file '%s'.\n", lf.realFileName.c_str());
	    cacheFileStat.st_size = 10 * 1024 * 1024; // 10Mbytes for temporary
	  } else {
	    logprintf(2, LOG_DEBUG, "The cache file size is %ld.\n", cacheFileStat.st_size);
	  }
	}
	bool copySucceeded;
	const CompressionControl::CompressionType ctype = compressionControl.getCompressionType(path);
	{
	  const bool useTGELock = minimumFileSizeToEnableLock <= cacheFileStat.st_size;
	  TGELock tgeLock(tgeLockdServer, tgeLockdPort);
	  if(useTGELock) {
	    tgeLock.lock();
	    if(tgeLock.isFailed()) {
	      logprintf(0, LOG_ERROR, "Lock error : %s\n", tgeLock.getErrorMessage().c_str());
	    } else {
	      logprintf(3, LOG_INFO,  "Locked tgelockd\n");
	    }
	  }
	  switch(ctype){
	  case CompressionControl::Uncompressed:
	    copySucceeded = copyFile(lf.realFileName.c_str(), path, mode);
	    break;
	  case CompressionControl::LZOx1:
	    copySucceeded = copyFileWithCompression(lf.realFileName.c_str(), path, mode);
	    break;
	  default:
	    copySucceeded = false;
	  }
	  if(useTGELock) {
	    tgeLock.unlock();
	  }
	}
	if(!copySucceeded) {
	  logprintf(0, LOG_ERROR, "Write back copy failed. ('%s' -> '%s', mode=%o, ctype=%d)\n", lf.realFileName.c_str(), path, mode, ctype);
	  if(failedStat) {
	    logprintf(0, LOG_ERROR, "stat failed for the original file '%s', which is going to be replaced by '%s'. Using default permission (0600).\n", path, lf.realFileName.c_str());
	  }
	} else {
	  logprintf(2, LOG_DEBUG, "Copy succeeded\n");
	  const bool touchSucceeded = touchByAnotherFilesDate(lf.realFileName.c_str(), path);
	  if(!touchSucceeded) {
	    logprintf(0, LOG_ERROR, "touch failed for write back cache file '%s' for '%s'.\n", lf.realFileName.c_str(), path);
	  }
	  {
	    CachedLocalFiles::LFLock lock(cachedLocalFiles);
	    cacheGarbageCollection.accessedFile(getFileSize(path), lock);
	  }
	}
      }
    }
    {
      CachedLocalFiles::LFLock lock(cachedLocalFiles);
      lock.removeLF(fi->fh);
    }
  }
  return 0;
}

//------------------------------------------------------------------------------
static int tgefs_statfs(const char *path, struct statvfs *stbuf)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    const char* spath = getSpecialPath(path);
    if(strcmp(spath, "/tgefs") == 0) {
      path = "/proc";
    } else if(strcmp(spath, "/tgefslog") == 0) {
      path = "/proc";
    } else if(strcmp(spath, "/tgefscache") == 0) {
      path = "/proc";
    } else if(strcmp(spath, "") != -0) {
      return -ENOENT;
    }
  }
  SETFSID setfsid;
  int res = statvfs(path, stbuf);
  if (res == -1) return -errno;
  return 0;
}

static int tgefs_fsync(const char *path, int isdatasync,
                       struct fuse_file_info *fi)
{
  /* Just a stub.  This method is optional and can safely be left
     unimplemented */
  return 0;
}

/* xattr operations are optional and can safely be left unimplemented */
static int tgefs_setxattr(const char *path, const char *name, const char *value,
                          size_t size, int flags)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    return ENOTSUP;
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    const int res = lsetxattr(path, name, value, size, flags);
    if (res == -1) return -errno;
  } else {
    const int res = lsetxattr(path, name, value, size, flags);
    if (res == -1) return -errno;
  }
  return 0;
}

static int tgefs_getxattr(const char *path, const char *name, char *value,
                    size_t size)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    const char* spath = getSpecialPath(path);
    if(strcmp(spath, "/tgefs") == 0) {
      return -ENOTSUP;
    } else if(strcmp(spath, "/tgefslog") == 0) {
      return -ENOTSUP;
    } else if(strcmp(spath, "/tgefscache") == 0) {
      return -ENOTSUP;
    } else if(strcmp(spath, "") != -0) {
      return -ENOENT;
    }
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  int res;
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    res = lgetxattr(path, name, value, size);
  } else {
    res = lgetxattr(path, name, value, size);
  }
  if (res == -1) return -errno;
  return res;
}

static int tgefs_listxattr(const char *path, char *list, size_t size)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    const char* spath = getSpecialPath(path);
    if(strcmp(spath, "/tgefs") == 0) {
      return -ENOTSUP;
    } else if(strcmp(spath, "/tgefslog") == 0) {
      return -ENOTSUP;
    } else if(strcmp(spath, "/tgefscache") == 0) {
      return -ENOTSUP;
    } else if(strcmp(spath, "") != -0) {
      return -ENOENT;
    }
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  int res;
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    res = llistxattr(path, list, size);
  } else {
    res = llistxattr(path, list, size);
  }
  if (res == -1) return -errno;
  return res;
}

static int tgefs_removexattr(const char *path, const char *name)
{
  if(isRecursiveFilePath(path))
    return -ENOENT;
  if(isSpecialPath(path)) {
    const char* spath = getSpecialPath(path);
    if(strcmp(spath, "/tgefs") == 0) {
      return -ENOTSUP;
    } else if(strcmp(spath, "/tgefslog") == 0) {
      return -ENOTSUP;
    } else if(strcmp(spath, "/tgefscache") == 0) {
      return -ENOTSUP;
    } else if(strcmp(spath, "") != -0) {
      return -ENOENT;
    }
  }
  SETFSID setfsid;
  const string ccfn = createCachedFileName(path);
  int res;
  if(!ccfn.empty()) {
    CachedLocalFiles::LocalCacheFileLock lcflock(cachedLocalFiles, ccfn.c_str());
    res = lremovexattr(path, name);
  } else {
    res = lremovexattr(path, name);
  }
  if (res == -1) return -errno;
  return 0;
}

//--------------------------------------------------------------------------
void CachedLocalFiles::createCacheDir()
{
  fprintf(stderr, "Checking cache directory...\n");
  if(cacheDirectoryRoot[0] != '/') {
    fprintf(stderr, "CacheRoot '%s' must be an absolute path\n", cacheDirectoryRoot);
    exit(1);
  }
  const string dir = cacheDirectoryRoot;
  int ensured_path_end = 0;
  while(ensured_path_end <= (int)dir.size()) {
    if(ensured_path_end == 0 || access(dir.substr(0, ensured_path_end).c_str(), F_OK) == 0) {
    } else {
      if(mkdir(dir.substr(0, ensured_path_end).c_str(), 0755) == 0) {
	fprintf(stderr, "Directory '%s' is created.\n", dir.substr(0, ensured_path_end).c_str());
      } else {
	fprintf(stderr, "CacheRoot '%s' does not exist. Abort.\n", cacheDirectoryRoot);
	exit(1);
      }
    }
    if(ensured_path_end >= (int)dir.size())
      break;
    const string::size_type nextSlashPos = dir.find('/', ensured_path_end + 1);
    if(nextSlashPos == string::npos)
      ensured_path_end = (int)dir.size();
    else
      ensured_path_end = nextSlashPos;
  }
  fprintf(stderr, "Done.\n");
}

static struct fuse_operations tgefs_oper;

int main(int argc, char *argv[])
{
  {
    const char* home_env = getenv("HOME");
    if(home_env == NULL) {
      strcpy(my_home, "/");
    } else {
      strncpy(my_home, home_env, sizeof(my_home) - 1);
      my_home[sizeof(my_home) - 1] = '\0';
    }
    compressionControl.init(my_home);
  }
  if(!load_application_config()) {
    fprintf(stderr, "Failed to configure tgefs. Aborted.\n");
    return 1;
  }
  if(strcmp(cacheDirectoryRoot, "/") == 0) {
    fprintf(stderr, "You must specify 'tgelocaldisk' attribute in the configuration file ~/.tge/.tgerc\n");
    return 1;
  }
  initlog(cacheDirectoryRoot);
  {
    const int INIT_LOG_LEVEL = 0;
    loglevel(INIT_LOG_LEVEL);
  }
  cachedLocalFiles.init();
  cacheGarbageCollection.init(cacheDirectoryRoot, CacheGarbageCollection::AUTO, CacheGarbageCollection::AUTO);
  logprintf(0, LOG_INFO, "Initial garbage colletion\n");
  {
    CachedLocalFiles::LFLock lock(cachedLocalFiles);
    cacheGarbageCollection.collect(lock);
  }
  {
    int idx = 1;
    while(idx < argc && argv[idx][0] == '-')
      idx++;
    if(!(idx < argc)) {
      fprintf(stderr, "No mount points found.\n");
      return 1;
    }
    const char* mountPoint = argv[idx];
    char* rmp = realpath(mountPoint, canonicalMountPoint);
    if(rmp == NULL) {
      fprintf(stderr, "realpath could not be obtained for '%s', errno=%d\n", mountPoint, errno);
      perror(NULL);
      return 1;
    }
    canonicalMountPoint_length = strlen(rmp);
    if(rmp[canonicalMountPoint_length - 1] != '/') {
      rmp[canonicalMountPoint_length++] = '/';
      rmp[canonicalMountPoint_length] = '\0';
    }
    time_t tm = time(NULL);
    const char* timeString = asctime(localtime(&tm));
    const int timeStringLength = strlen(timeString) - 1;
    logprintf(0, LOG_INFO, "tgefs started at %*.*s. mount point = %s\n", timeStringLength, timeStringLength, timeString, rmp);
  }
  umask(0);
  tgefs_oper.getattr	   = tgefs_getattr;
  tgefs_oper.access	   = tgefs_access;
  tgefs_oper.readlink	   = tgefs_readlink;
  tgefs_oper.readdir	   = tgefs_readdir;
  tgefs_oper.mknod	   = tgefs_mknod;
  tgefs_oper.mkdir	   = tgefs_mkdir;
  tgefs_oper.symlink	   = tgefs_symlink;
  tgefs_oper.unlink	   = tgefs_unlink;
  tgefs_oper.rmdir	   = tgefs_rmdir;
  tgefs_oper.rename	   = tgefs_rename;
  tgefs_oper.link	   = tgefs_link;
  tgefs_oper.chmod	   = tgefs_chmod;
  tgefs_oper.chown	   = tgefs_chown;
  tgefs_oper.truncate	   = tgefs_truncate;
  tgefs_oper.utimens	   = tgefs_utimens;
  tgefs_oper.open	   = tgefs_open;
  tgefs_oper.read	   = tgefs_read;
  tgefs_oper.write	   = tgefs_write;
  tgefs_oper.statfs	   = tgefs_statfs;
  tgefs_oper.release	   = tgefs_release;
  tgefs_oper.fsync	   = tgefs_fsync;
  tgefs_oper.setxattr	   = tgefs_setxattr;
  tgefs_oper.getxattr	   = tgefs_getxattr;
  tgefs_oper.listxattr   = tgefs_listxattr;
  tgefs_oper.removexattr = tgefs_removexattr;
  return fuse_main(argc, argv, &tgefs_oper, NULL);
}
