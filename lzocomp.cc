#if HAVE_CONFIG
 #include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "lzocomp.h"

using namespace std;

const unsigned long LZO::lzo_filebuffer_length = 16 * 1024 * 1024ul; // buffer length
const unsigned long LZO::lzo_inblock_length    = 128 * 1024ul;
const unsigned long LZO::lzo_outblock_length   = lzo_inblock_length + lzo_inblock_length / 16 + 64 + 3;

bool LZO::compress(const int infd, const int outfd)
{
  init_fbuffer();
  unsigned int readBytes;
  while((readBytes = read(infd, in, lzo_inblock_length)) > 0) {
    lzo_uint out_len;
    lzo1x_1_compress(in, readBytes, out, &out_len, work);
    if(out_len >= readBytes) {
      // incompressible
      const int size = -readBytes;
      if(!put_fbuffer(outfd, reinterpret_cast<const unsigned char*>(&size), sizeof(int)))
	return false;
      if(!put_fbuffer(outfd, in, readBytes))
	return false;
      // printf("UB %d %d\n", size, readBytes);
    } else {
      // compressed
      const int size = out_len;
      if(!put_fbuffer(outfd, reinterpret_cast<const unsigned char*>(&size), sizeof(int)))
	return false;
      if(!put_fbuffer(outfd, out, out_len))
	return false;
      // printf("CB %d %d [%02X %02X %02X\n", size, readBytes, out[0], out[1], out[2]);
    }
  }
  if(!flush_fbuffer(outfd))
    return false;
  return true;
}

bool LZO::compress(const unsigned char* in, lzo_uint in_len, unsigned char* out, lzo_uint& out_len)
{
  out_len = max_outblock_size();
  const int result = lzo1x_1_compress(in, in_len, out + sizeof(int), &out_len, work);
  assert(result == LZO_E_OK);
  if(out_len >= in_len) {
    // incompressible
    *reinterpret_cast<int*>(out) = -in_len;
    memcpy(out + sizeof(int), in, in_len);
  } else {
    // compressed
    *reinterpret_cast<int*>(out) = out_len;
    out_len += sizeof(int);
  }
  return true;
}

bool LZO::decompress(const int infd, const int outfd)
{
  init_fbuffer();
  while(true) {
    int size;
    {
      unsigned char* sizep = ensure_fbuffer(infd, sizeof(int));
      if(sizep == NULL)
	break;
      size = *reinterpret_cast<int*>(sizep);
      pop_fbuffer(sizeof(int));
    }
    // printf("SIZE %d\n", size);
    if(size <= 0) {
      // incompressible data
      unsigned char* data = ensure_fbuffer(infd, -size);
      if(data == NULL)
	return false;
      // printf("D\n");
      if(write(outfd, data, -size) == -1)
	return false;
      // printf("E\n");
      pop_fbuffer(-size);
      // printf("UB %d %d\n", -size, -size);
    } else {
      // compressed
      unsigned char* data = ensure_fbuffer(infd, size);
      if(data == NULL)
	return false;
      // printf("D\n");
      lzo_uint out_len = max_outblock_size();
      const int result = lzo1x_decompress_safe(data, size, out, &out_len, NULL);
      // printf("E = %d\n", result);
      if(result == LZO_E_OK) {
	if(write(outfd, out, out_len) == -1)
	  return false;
      } else {
	static char mark[4] = {0xde, 0xad, 0xbe, 0xef};
	// put thousands of deadbeef
	for(int i = 0; i < 1000; i++) {
	  write(outfd, mark, 4);
	}
	return false;
      }
      pop_fbuffer(size);
      // printf("CB %d %d [%02X %02X %02X\n", size, (int)out_len, data[0], data[1], data[2]);
    }
  }
  return true;
}

bool LZO::decompress(const unsigned char* in, lzo_uint in_len, unsigned char* out, lzo_uint& out_len)
{
  const int compressedSize = *reinterpret_cast<int*>(out);
  if(compressedSize <= 0) {
    // raw block
    const int rawBlockSize = -compressedSize;
    out_len = rawBlockSize;
    memcpy(out, in + sizeof(int), out_len);
  } else {
    const int result = lzo1x_decompress(in + sizeof(int), compressedSize, out, &out_len, NULL);
    assert(result == LZO_E_OK);
  }
  return true;
}

LZO::LZO() {
  if(lzo_init() != LZO_E_OK) {
    fprintf(stderr, "LZO init error\n");
    exit(1);
  }
  work    = new unsigned char[LZO1X_1_MEM_COMPRESS];
  in      = new unsigned char[lzo_inblock_length];
  out     = new unsigned char[lzo_outblock_length];
  fbuffer = new unsigned char[lzo_filebuffer_length];
}

LZO::~LZO() {
  delete[] in;
  delete[] out;
  delete[] work;
  delete[] fbuffer;
}
