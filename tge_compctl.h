#ifndef _HEADER_TGE_COMPCTL
#define  _HEADER_TGE_COMPCTL

#include <vector>
#include <string>

class CompressionControl {
  std::vector<std::string> compressingOrders;

  int  predicate(const std::string& expr, int cursor, const char* path);
  
 public:
  enum CompressionType {
    Uncompressed = 0,
    LZOx1        = 1
  };
  void init(const char* homedir);
  CompressionType getCompressionType(const char* path);
};

#endif // #ifndef _HEADER_TGE_COMPCTL
