#if HAVE_CONFIG
 #include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "lzocomp.h"

using namespace std;

// Compressed file format (total 16bytes):
//
// Signature             7 bytes : L Z O \FC \AC \BA \71
// Compression algorithm 1 byte  : 1 (for LZO)
// File size             8 bytss :

char LZO_signature[] = "LZO\xfc\xac\xba\x71";
const char COMPRESSION_TYPE_LZO = 1;

bool copyFile(const char *srcPath, const char *destPath, int mode)
{
  const int srcfd = open(srcPath, O_RDONLY | O_LARGEFILE);
  if(srcfd == -1) return false;
  const int destfd = open(destPath, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW | O_LARGEFILE, mode);
  if(destfd == -1) {
    close(srcfd);
    return false;
  }
  {
    const int bufferSize = 16 * 1024 * 1024; // 16MegaBytes
    char* buffer = new char[bufferSize];
    unsigned int readBytes;
    while((readBytes = read(srcfd, buffer, bufferSize)) > 0) {
      write(destfd, buffer, readBytes);
    }
    delete[] buffer;
  }
  close(srcfd);
  close(destfd);
  return true;
}

bool copyFileWithCompression(const char *srcPath, const char *destPath, int mode)
{
  const int srcfd = open(srcPath, O_RDONLY | O_LARGEFILE);
  if(srcfd == -1) return false;
  struct stat st;
  {
    const int result = fstat(srcfd, &st);
    if(result == -1) return false;
    if(st.st_mode & S_IFMT != S_IFREG) false; // not a regular file. non-regular file may not have a file size.
  }
  const int destfd = open(destPath, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW | O_LARGEFILE, mode);
  if(destfd == -1) {
    close(srcfd);
    return false;
  }
  {
    char buffer[16];
    const int LZO_signature_length = strlen(LZO_signature);
    memcpy(buffer    , LZO_signature        , LZO_signature_length);
    memcpy(buffer + 7, &COMPRESSION_TYPE_LZO, sizeof(COMPRESSION_TYPE_LZO));
    const unsigned long long fileSize = st.st_size;
    memcpy(buffer + 8, &fileSize            , sizeof(fileSize));
    write(destfd, buffer, 16);
  }
  LZO lzoObject;
  lzoObject.compress(srcfd, destfd);
  close(srcfd);
  close(destfd);
  return true;
}

bool copyFileWithDecompression(const char *srcPath, const char *destPath, int mode, bool* srcFileWasCompressed)
{
  if(srcFileWasCompressed != NULL)
    *srcFileWasCompressed = false;
  int srcfd = open(srcPath, O_RDONLY | O_LARGEFILE);
  if(srcfd == -1) return false;
  int destfd = open(destPath, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW | O_LARGEFILE, mode);
  if(destfd == -1) {
    close(srcfd);
    return false;
  }
  {
    unsigned lzbuffer[128];
    const int LZO_signature_length = strlen(LZO_signature);
    const int headerSize = LZO_signature_length + sizeof(char) + sizeof(long long);
    int minusOffsetBytes = read(srcfd, lzbuffer, headerSize);
    const bool decompressionCopy = minusOffsetBytes == headerSize && memcmp(LZO_signature, lzbuffer, LZO_signature_length) == 0;
    if(decompressionCopy) {
      if(srcFileWasCompressed != NULL)
	*srcFileWasCompressed = true;
      const char compressionType = lzbuffer[LZO_signature_length];
      const long long fileSize   = *((long long *)(lzbuffer + LZO_signature_length + sizeof(char)));
      // above two variables are obtained but not used yet.
      LZO lzoObject;
      lzoObject.decompress(srcfd, destfd);
    } else {
      write(destfd, lzbuffer, minusOffsetBytes);

      const int bufferSize = 16 * 1024 * 1024; // 16MegaBytes
      char* buffer = new char[bufferSize];
      int readBytes;
      while((readBytes = read(srcfd, buffer, bufferSize - minusOffsetBytes)) > 0) {
	write(destfd, buffer, readBytes);
	minusOffsetBytes = 0;
      }
      delete[] buffer;
    }
  }
  close(srcfd);
  close(destfd);
  return true;
}

bool is_lzo_compressed_file(const char* infilename, char* compression_type, long long* file_size)
{
  const int fd = open(infilename, O_RDONLY | O_LARGEFILE);
  if(fd == -1) {
    // fprintf(stderr, "Cannot open %s\n", infilename);
    return false;
  }
  unsigned char lzbuffer[128];
  const int LZO_signature_length = strlen(LZO_signature);
  const int headerLength = LZO_signature_length + sizeof(char) + sizeof(long long);
  if(headerLength != 16)
    return false;
  const int readBytes = read(fd, lzbuffer, headerLength);
  close(fd);
  if(readBytes == -1) {
    // fprintf(stderr, "Read from %s failed\n", infilename);
    return false;
  }
  if(readBytes < headerLength) {
    return false;
  }
  const bool isCompressedFile = memcmp(LZO_signature, lzbuffer, LZO_signature_length) == 0;
  const char compressionType = lzbuffer[LZO_signature_length];
  const long long fileSize   = *((long long *)(lzbuffer + LZO_signature_length + sizeof(char)));
  if(isCompressedFile) {
    if(compression_type != NULL)
      *compression_type = compressionType;
    if(file_size != NULL)
      *file_size = fileSize;
  }
  return isCompressedFile;
}

