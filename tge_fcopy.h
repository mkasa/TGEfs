#ifndef _HEADER_TGE_FCOPY
#define _HEADER_TGE_FCOPY

bool copyFile(const char *srcPath, const char *destPath, int mode);
bool copyFileWithCompression(const char *srcPath, const char *destPath, int mode);
bool copyFileWithDecompression(const char *srcPath, const char *destPath, int mode, bool* srcFileWasCompressed = NULL);
bool is_lzo_compressed_file(const char* infilename, char* compression_type = NULL, long long* file_size = NULL);

extern char LZO_signature[];

#endif // $ifndef _HEADER_TGE_FCOPY
