#ifndef _HEADER_LZOCOMP
#define _HEADER_LZOCOMP

#include "minilzo.h"

class LZO
{
  static const unsigned long lzo_inblock_length;
  static const unsigned long lzo_outblock_length;
  static const unsigned long lzo_filebuffer_length;

  unsigned char *work;
  unsigned char *in;
  unsigned char *out;
  unsigned char *fbuffer;
  lzo_uint      fbuffer_tail;
  lzo_uint      fbuffer_head;

  inline void init_fbuffer() {
    fbuffer_tail = 0;
    fbuffer_head = 0;
  }
  inline bool flush_fbuffer(const int fhd) {
    const int result = write(fhd, fbuffer, fbuffer_tail);
    if(result == -1)
      return false;
    init_fbuffer();
    return true;
  }
  inline bool is_available_fbuffer(lzo_uint nBytes) {
    return fbuffer_head + nBytes <= fbuffer_tail;
  }
  inline unsigned char *ensure_fbuffer(const int fhd, lzo_uint ensureBytes) {
    if(!is_available_fbuffer(ensureBytes)) {
      memmove(fbuffer, fbuffer + fbuffer_head, fbuffer_tail - fbuffer_head);
      fbuffer_tail -= fbuffer_head;
      fbuffer_head = 0;
      const lzo_uint readBytes = read(fhd, fbuffer + fbuffer_tail, lzo_filebuffer_length - fbuffer_tail);
      if(readBytes == (lzo_uint)-1)
	return NULL; // read error
      fbuffer_tail += readBytes;
    }
    if(!is_available_fbuffer(ensureBytes))
      return NULL;
    return fbuffer + fbuffer_head;
  }
  inline void pop_fbuffer(lzo_uint nBytes) {
    fbuffer_head += nBytes;
  }
  inline bool put_fbuffer(const int fhd, const unsigned char* data, lzo_uint length) {
    if(fbuffer_tail + length >= lzo_filebuffer_length) {
      if(!flush_fbuffer(fhd))
	return false;
    }
    memcpy(fbuffer + fbuffer_tail, data, length);
    fbuffer_tail += length;
    return true;
  }

public:
  LZO();
  ~LZO();
  int max_inblock_size()  const { return lzo_inblock_length; }
  int max_outblock_size() const { return lzo_outblock_length + sizeof(int); }
  bool compress(const int infd, const int outfd);
  bool compress(const unsigned char* in, lzo_uint in_len, unsigned char* out, lzo_uint& out_len);
  bool decompress(const int infd, const int outfd);
  bool decompress(const unsigned char* in, lzo_uint in_len, unsigned char* out, lzo_uint& out_len);
};

#endif // #ifndef _HEADER_LZOCOMP
