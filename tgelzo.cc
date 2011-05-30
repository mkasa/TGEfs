/*
    LZO: LZO compression/decompression utility for TGE-FS
    Copyright (C) 2007       Masahiro Kasahara <masahiro@kasahara.ws>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#if HAVE_CONFIG
 #include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <string.h>
#include <string>
#include <fcntl.h>
#include "lzocomp.h"
#include "tge_fcopy.h"

using namespace std;

const char COMPRESSION_TYPE_LZO = 1;
LZO lzoObject;

void printusage_tgelzo()
{
  fprintf(stderr, "TGE-LZO file compression utility.\n"
                  "usage: tgelzo [e|d|c] <file> [options ...]\n"
                  "  e ... encode\n"
                  "  d ... decode\n"
                  "  c ... decode and cat (output to stdout)\n");
}

void printusage_lcat()
{
  fprintf(stderr, "Decompress TGE-LZO file and outputs it to stdout\n"
                  "usage: lcat <file(s)>\n");
}

void printusage_unlzo()
{
  fprintf(stderr, "Decompress TGE-LZO file like gunzip.\n"
                  "usage: unlzo <file(s)>\n");
}

void printusage_lzo()
{
  fprintf(stderr, "Compress to TGE-LZO file like gzip.\n"
                  "usage: lzo <file(s)>\n");
}

void lzo_encode(const char* infilename)
{
  if(access(infilename, F_OK) != 0) {
    fprintf(stderr, "%s is not found.\n", infilename);
    exit(1);
  }
  if(is_lzo_compressed_file(infilename)) {
    fprintf(stderr, "%s is already compressed by TGEFS-LZO\n", infilename);
    exit(1);
  }
  struct stat infile_stat;
  if(stat(infilename, &infile_stat) == -1) {
    fprintf(stderr, "Cannot stat %s.\n", infilename);
    exit(1);
  }
  string tmpfilename = infilename;
  tmpfilename += ".temp";
  const int ofd = open(tmpfilename.c_str(), O_CREAT | O_EXCL | O_LARGEFILE | O_WRONLY, infile_stat.st_uid == getuid() ? infile_stat.st_mode & 0777 : 0644);
  if(ofd == -1) {
    fprintf(stderr, "Cannot open %s for temporary object.\nMaybe it already exists, or the directory is not writable.\n", tmpfilename.c_str());
    exit(1);
  }
  const int ifd = open(infilename, O_RDONLY | O_LARGEFILE);
  if(ifd == -1) {
    fprintf(stderr, "Cannot open %s\n", infilename);
    close(ofd);
    exit(1);
  }
  struct stat st;
  {
    const int result = fstat(ifd, &st);
    if(result == -1) {
      close(ofd);
      fprintf(stderr, "Cannot stat %s\n", infilename);
      exit(1);
    }
    if(st.st_mode & S_IFMT != S_IFREG) {
      close(ofd);
      fprintf(stderr, "%s is not a regular file\n", infilename);
      exit(1);
    }
  }
  {
    char buffer[16];
    const int LZO_signature_length = strlen(LZO_signature);
    memcpy(buffer    , LZO_signature        , LZO_signature_length);
    memcpy(buffer + 7, &COMPRESSION_TYPE_LZO, sizeof(COMPRESSION_TYPE_LZO));
    const unsigned long long fileSize = st.st_size;
    memcpy(buffer + 8, &fileSize            , sizeof(fileSize));
	printf("LZOsig=%d\n", LZO_signature_length);
	for(int i = 0; i < 16; i++) {
      printf(" %02X", buffer[i]);
	}
    printf("\n");
    write(ofd, buffer, 16);
  }
  const bool compressionSuceeded = lzoObject.compress(ifd, ofd);
  close(ofd);
  close(ifd);
  if(compressionSuceeded) {
    if(unlink(infilename) == -1) {
      fprintf(stderr, "Cannot unlink %s. Temporary file is left.\n", infilename);
      exit(1);
    } else {
      if(rename(tmpfilename.c_str(), infilename) == -1) {
	fprintf(stderr, "Cannot rename temporary file %s to %s.\nThis must rarely occur.", tmpfilename.c_str(), infilename);
	exit(1);
      }
    }
  } else {
    fprintf(stderr, "Error occurred during compression\n");
    unlink(tmpfilename.c_str());
    exit(1);
  }
}

void lzo_decode(const char* infilename)
{
  if(access(infilename, F_OK) != 0) {
    fprintf(stderr, "%s is not found.\n", infilename);
    exit(1);
  }
  if(!is_lzo_compressed_file(infilename)) {
    fprintf(stderr, "%s is not compressed by TGEFS-LZO\n", infilename);
    exit(1);
  }
  struct stat infile_stat;
  if(stat(infilename, &infile_stat) == -1) {
    fprintf(stderr, "Cannot stat %s.\n", infilename);
    exit(1);
  }
  string tmpfilename = infilename;
  tmpfilename += ".temp";
  const int ofd = open(tmpfilename.c_str(), O_CREAT | O_EXCL | O_LARGEFILE | O_WRONLY, infile_stat.st_uid == getuid() ? infile_stat.st_mode & 0777 : 0644);
  if(ofd == -1) {
    fprintf(stderr, "Cannot open %s for temporary object.\nMaybe it already exists, or the directory is not writable.\n", tmpfilename.c_str());
    exit(1);
  }
  const int ifd = open(infilename, O_RDONLY | O_LARGEFILE);
  if(ifd == -1) {
    fprintf(stderr, "Cannot open %s\n", infilename);
    close(ofd);
    exit(1);
  }
  {
    unsigned lzbuffer[128];
    const int LZO_signature_length = strlen(LZO_signature);
    const int headerSize           = LZO_signature_length + sizeof(char) + sizeof(long long);
    const int readBytes            = read(ifd, lzbuffer, headerSize);
    const bool decompressionCopy   = readBytes == LZO_signature_length && memcmp(LZO_signature, lzbuffer, LZO_signature_length) == 0;
    if(readBytes < headerSize) {
      fprintf(stderr, "read error\n");
      exit(1);
    }
  }
  const bool decompressionSuceeded = lzoObject.decompress(ifd, ofd);
  close(ofd);
  close(ifd);
  if(decompressionSuceeded) {
    if(unlink(infilename) == -1) {
      fprintf(stderr, "Cannot unlink %s. Temporary file is left.\n", infilename);
      exit(1);
    } else {
      if(rename(tmpfilename.c_str(), infilename) == -1) {
	fprintf(stderr, "Cannot rename temporary file %s to %s.\nThis must rarely occur.", tmpfilename.c_str(), infilename);
	exit(1);
      }
    }
  } else {
    fprintf(stderr, "Error occurred during decompression\n");
    unlink(tmpfilename.c_str());
    exit(1);
  }
}

void lzo_cat(const char* infilename)
{
  if(access(infilename, F_OK) != 0) {
    fprintf(stderr, "%s is not found.\n", infilename);
    exit(1);
  }
  const bool isDecompressionCopy = is_lzo_compressed_file(infilename);
  const int ifd = open(infilename, O_RDONLY | O_LARGEFILE);
  if(ifd == -1) {
    fprintf(stderr, "Cannot open %s\n", infilename);
    exit(1);
  }
  const int stdoutfd = 1;
  if(isDecompressionCopy) {
	const int LZO_header_size = 16;
    char buffer[LZO_header_size];
    const int readBytes = read(ifd, buffer, sizeof(buffer));
    if(readBytes < sizeof(buffer)) {
      fprintf(stderr, "read error\n");
      return;
    }
    lzoObject.decompress(ifd, stdoutfd);
  } else {
    const int bufferSize = 1024 * 1024; // 1megaBytes
    char* buffer = new char[bufferSize];
    int readBytes;
    while((readBytes = read(ifd, buffer, bufferSize)) > 0) {
      write(stdoutfd, buffer, readBytes);
    }
    delete[] buffer;
  }
}

bool isProgram(const char* argv0, const char* program_name)
{
  const char* a_cursor = argv0;
  for(const char* p = argv0; *p;) {
    if(*p++ == '/')
      a_cursor = p;
  }
  if(strcmp(a_cursor, program_name) == 0)
    return true;
  return false;
}

int main(int argc, char** argv)
{
  const char* program_name = argv[0];
  // printf("PROG '%s'\n", program_name);
  if(isProgram(program_name, "lcat")) {
    // PROGRAM = lcat
    if(argc < 2) {
      printusage_lcat();
      return 1;
    }
	for(int i = 1; i < argc; i++) {
      const char* filename = argv[i];
      lzo_cat(filename);
    }
    return 0;
  }
  if(isProgram(program_name, "unlzo")) {
    // PROGRAM = unlzo
    if(argc < 2) {
      printusage_unlzo();
      return 1;
    }
	for(int i = 1; i < argc; i++) {
      const char* filename = argv[i];
      lzo_decode(filename);
    }
    return 0;
  }
  if(isProgram(program_name, "lzo")) {
    // PROGRAM = lzo
    if(argc < 2) {
      printusage_lzo();
      return 1;
    }
	for(int i = 1; i < argc; i++) {
      const char* filename = argv[i];
      lzo_encode(filename);
    }
    return 0;
  }
  // PROGRAM = tgelzo
  if(argc < 3) {
    printusage_tgelzo();
    return 1;
  }
  const char* type = argv[1];
  const char* filename = argv[2];
  if(strcmp(type, "e") == 0) {
    lzo_encode(filename);
  } else if(strcmp(type, "d") == 0) {
    lzo_decode(filename);
  } else if(strcmp(type, "c") == 0) {
    lzo_cat(filename);
  } else {
    printusage_tgelzo();
    return 1;
  }
  return 0;
}
