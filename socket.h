//---------------------------------------------------------------------------
/*
                 gimite/socket Ver.1.5  by Gimite Ichikawa
  
  This is a socket library which supports TCP/IP and UDP/IP.
  It supports Windows (using WinSock2) and UNIX (using POSIX Socket).
  
  See http://gimite.net/gimite/cppmess/socket.html (Japanese) for more
  information.
  
  This software is public domain.
  
  Gimite Ichikawa http://gimite.net/
  
*/
//---------------------------------------------------------------------------
#ifndef GIMITE_socketH
#define GIMITE_socketH
//---------------------------------------------------------------------------
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  define GIMITE_WIN32
#endif
//---------------------------------------------------------------------------
#include <iostream>
#include <ios>//streamsize
#include <streambuf>//basic_streambuf
#include <string>//char_traits, basic_string
#include <vector>//vector
#ifdef GIMITE_WIN32
#  include <WinSock2.h>
#  pragma comment(lib,"ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#endif
//---------------------------------------------------------------------------
namespace gimite{
//---------------------------------------------------------------------------
#ifdef GIMITE_WIN32
  typedef SOCKET socket_t;
  typedef int sock_result_t;
  typedef int sock_len_t;
  typedef unsigned long sock_uint32_t;
#else
  typedef int socket_t;
  typedef ssize_t sock_result_t;
  typedef socklen_t sock_len_t;
  typedef u_int32_t sock_uint32_t;
#endif
//---------------------------------------------------------------------------
//�\�P�b�g�V�X�e����������
inline bool startup_socket(){
#ifdef GIMITE_WIN32
  WSADATA wsaData;
  return WSAStartup(0x0002, &wsaData)==0;
#else
  return true;
#endif
}

//---------------------------------------------------------------------------
//�\�P�b�g�V�X�e������n��
inline void cleanup_socket(){
#ifdef GIMITE_WIN32
  WSACleanup();
#endif
}

//---------------------------------------------------------------------------
//IP�A�h���X
class ip_address
{
public:
  explicit ip_address(sock_uint32_t addr= INADDR_NONE){
#ifdef GIMITE_WIN32
    addr_.S_un.S_addr= addr;
#else
    addr_.s_addr= addr;
#endif
  }
  explicit ip_address(const in_addr& addr): addr_(addr){}
  ip_address(const std::string& host){
    solve_address(host.c_str());
  }
  ip_address(const char* host){
    solve_address(host);
  }
  ip_address& operator=(const std::string& host){
    solve_address(host.c_str());
    return *this;
  }
  ip_address& operator=(const char* host){
    solve_address(host);
    return *this;
  }
  //�A�h���X��\��in_addr�\����
  in_addr addr()const{
    return addr_;
  }
  //�A�h���X��\��32�r�b�g����
  sock_uint32_t as_int()const{
#ifdef GIMITE_WIN32
    return addr_.S_un.S_addr;
#else
    return addr_.s_addr;
#endif
  }

private:
  in_addr addr_;
  
  //�z�X�g��orIP�̕�����\������IP�A�h���X�𓾂�
  void solve_address(const char* host){
    sock_uint32_t uaddr= ::inet_addr(host);
    if (uaddr==INADDR_NONE){
      hostent* ent= ::gethostbyname(host);
      if (ent && ent->h_addr_list[0])
        uaddr= *(sock_uint32_t*)ent->h_addr_list[0];
    }
#ifdef GIMITE_WIN32
    addr_.S_un.S_addr= uaddr;
#else
    addr_.s_addr= uaddr;
#endif
  }
  
  //��r�A���l����B
  friend bool operator<(const ip_address& lhs, const ip_address& rhs){
		return lhs.as_int()<rhs.as_int();
	}
	friend bool operator>(const ip_address& lhs, const ip_address& rhs){ return rhs<lhs; }
	friend bool operator<=(const ip_address& lhs, const ip_address& rhs){ return !(lhs>rhs); }
	friend bool operator>=(const ip_address& lhs, const ip_address& rhs){ return !(lhs<rhs); }
	friend bool operator==(const ip_address& lhs, const ip_address& rhs){ return lhs>=rhs && lhs<=rhs; }
	friend bool operator!=(const ip_address& lhs, const ip_address& rhs){ return !(lhs==rhs); }
  
  //stream�o�́B
  friend std::ostream& operator<<(std::ostream& os, const ip_address& addr){
		sock_uint32_t uaddr= addr.as_int();
		for (int i= 0; i<4; ++i){
			if (i>0) os << ".";
			os << (int)(unsigned char)(uaddr >> i*8);
		}
		return os;
	}
  
};

//---------------------------------------------------------------------------
//IP�A�h���X+�|�[�g�ԍ�
struct socket_address
{
  explicit socket_address(const ip_address& i= ip_address(), int p= 0)
    : ip(i), port(p){}
  
  //IP�A�h���X
  ip_address ip;
  //�|�[�g�ԍ�
  int port;
  
  //��r�A���l����B
  friend bool operator<(const socket_address& lhs, const socket_address& rhs){
		if (lhs.ip<rhs.ip) return true;
		if (lhs.ip>rhs.ip) return false;
		return lhs.port<rhs.port;
	}
	friend bool operator>(const socket_address& lhs, const socket_address& rhs){ return rhs<lhs; }
	friend bool operator<=(const socket_address& lhs, const socket_address& rhs){ return !(lhs>rhs); }
	friend bool operator>=(const socket_address& lhs, const socket_address& rhs){ return !(lhs<rhs); }
	friend bool operator==(const socket_address& lhs, const socket_address& rhs){
		return lhs>=rhs && lhs<=rhs;
	}
	friend bool operator!=(const socket_address& lhs, const socket_address& rhs){ return !(lhs==rhs); }
  
  //stream�o�́B
  friend std::ostream& operator<<(std::ostream& os, const socket_address& saddr){
		os << saddr.ip << ":" << saddr.port;
		return os;
	}
	
};

//---------------------------------------------------------------------------
//�X�g���[���\�P�b�g�pstreambuf
class socket_streambuf: public std::streambuf
{
  typedef std::streambuf::int_type int_type;
  typedef std::streambuf::traits_type traits_type;
  
public:
  explicit socket_streambuf(socket_t sock= -1, std::size_t buf_size= 1024)
      : sock_(sock), buffer_(buf_size) {}
  socket_streambuf(const ip_address& host, int port, 
        std::size_t buf_size= 1024)
      : sock_(-1), buffer_(buf_size){
    open(host, port);
  }
  virtual ~socket_streambuf(){
    close();
  }
  //�ڑ�
  bool open(const ip_address& host, int port){
    if (is_open()) close();
    if (host.as_int()==INADDR_NONE) return false;
    sock_= ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_==-1) return false;
    sockaddr_in addr;
    addr.sin_family= AF_INET;
    addr.sin_port= htons((unsigned short)port);
    addr.sin_addr= host.addr();
    if (::connect(sock_, (const sockaddr*)&addr, sizeof(sockaddr_in))==-1)
      close();
    //FD_ZERO(&write_timeout_fds);
    //FD_ZERO(&except_timeout_fds);
    FD_ZERO(&read_timeout_fds);
    return is_open();
  }
  //�ؒf
  void close(){
    if (sock_==-1) return;
#ifdef GIMITE_WIN32
    ::closesocket(sock_);
#else
    ::close(sock_);
#endif
    sock_= -1;
  }
  //�\�P�b�g�n���h��������ɉ��
  socket_t release(){
    socket_t sock= sock_;
    sock_= -1;
    return sock;
  }
  //���M
  int send(const void* buffer, int size){
    return ::send(sock_, (const char*)buffer, size, 0);
  }
  //��M
  int recv(void* buffer, int size){
    return ::recv(sock_, (char*)buffer, size, 0);
  }
  //�^�C���A�E�g�t����M (added by mkasa)
  int recv_line_with_timeout(void* buffer, const int size, const int time_out_in_microsecond, const char eol_delimit = '\n') {
    int readBytes = 0;
    int microsecondsToWait = time_out_in_microsecond;
    struct timeval timeout;
    timeout.tv_sec  = microsecondsToWait / 1000000;
    timeout.tv_usec = microsecondsToWait % 1000000;
    while(true) {
      {
	if(is_open()) {
	  FD_SET(sock_, &read_timeout_fds);
	}
	// std::cerr << "SELECT START sock = " << sock_ << std::endl;
	const int result = ::select(sock_ + 1, &read_timeout_fds, NULL /*&write_timeout_fds*/, NULL /*&except_timeout_fds*/, &timeout);
	// std::cerr << "SELECT END" << std::endl;
	const bool errorOccurred  = result == -1;
	if(errorOccurred) return -1;
	const bool timeout = result == 0;
	if(timeout) return readBytes;
      }
      {
	const int result = ::recv(sock_, (char*)buffer + readBytes, size - readBytes, 0);
	const bool errorOccurred  = result == -1;
	if(errorOccurred) return -1;
	const int n = result;
	// std::cerr << n << " bytes read" << std::endl;
	const char* p = (const char*)buffer + readBytes;
	int i;
	for(i = 0; i < n; i++)
	  if(p[i] == eol_delimit) break;
	const bool EOLFound = i < n;
	readBytes += n;
	if(EOLFound) {
	  // std::cerr << "Found EOL" << std::endl;
	  return readBytes;
	}
	const bool bufferFull = readBytes >= size;
	if(bufferFull)
	  return readBytes;
      }
    }
  }
  //����ɐڑ�����Ă邩
  bool is_open()const{ return sock_!=-1; }
  //�\�P�b�g�n���h��
  socket_t socket()const{ return sock_; }

protected:
  //1�������M
  virtual int_type overflow(int_type c= std::char_traits<char>::eof()){
    if (!traits_type::eq_int_type(c, traits_type::eof())){
      char cc= char(c);
      if (traits_type::eq_int_type(xsputn(&cc, 1), 0))
        return traits_type::eof();
    }
    return 'a';
  }
  //n�������M
  virtual std::streamsize xsputn(const char_type* s, std::streamsize n){
    if (!is_open()) return 0;
    sock_result_t res= ::send(sock_, s, int(n), 0);
    return res==-1? 0 : res;
  }
  //��M�o�b�t�@����ɂȂ���
  virtual int_type underflow(){
    if (!is_open()) return traits_type::eof();
    if (!gptr() || gptr()>=egptr()){
      if (gptr() && gptr()>eback())
        buffer_[0]= *(gptr()-1);
      else
        buffer_[0]= '\0';
      // std::cerr << "Underflow" << std::endl;
      sock_result_t size= ::recv(sock_, &buffer_[1], int(buffer_.size()-1), 0);
      if (size<=0)
        return traits_type::eof();
      else
        setg(&buffer_[0], &buffer_[1], &buffer_[size+1]);
    }
    return traits_type::to_int_type(*gptr());
  }

private:
  socket_t sock_;
  std::vector<char> buffer_;

  fd_set read_timeout_fds;
  // fd_set write_timeout_fds;
  // fd_set except_timeout_fds;
};

//---------------------------------------------------------------------------
//�X�g���[���\�P�b�g�pstream
class socket_stream: public std::iostream
{
public:
  //�����ɂ͐ڑ����Ȃ��R���X�g���N�^
  socket_stream(): std::iostream(0) {
    rdbuf(&sbuf_);
    setstate(std::ios::badbit);
	ensureBuffer();
  }
  //�����ɐڑ�����R���X�g���N�^
  socket_stream(const ip_address& host, int port)
      : std::iostream(0), sbuf_(host, port){
    rdbuf(&sbuf_);
    if (!sbuf_.is_open()) setstate(std::ios::badbit);
	ensureBuffer();
  }
  //�����̃\�P�b�g���w�肷��R���X�g���N�^
  explicit socket_stream(socket_t sock)
      : std::iostream(0), sbuf_(sock){
    rdbuf(&sbuf_);
    if (!sbuf_.is_open()) setstate(std::ios::badbit);
  }
  enum {
    INITIAL_BUFFER_SIZE = 4096
  };
  //�f�X�g���N�^
  virtual ~socket_stream(){ rdbuf(0); }

  void ensureBuffer() {
    buffer_.reserve(INITIAL_BUFFER_SIZE);
    if(buffer_.size() < INITIAL_BUFFER_SIZE)
      buffer_.resize(INITIAL_BUFFER_SIZE);
    num_chars_in_buffer_ = 0;
  }
  //�ڑ�
  void open(const ip_address& host, int port){
    clear();
    sbuf_.open(host, port);
    if (!sbuf_.is_open()) setstate(std::ios::badbit);
	ensureBuffer();
  }
  //�ؒf
  void close(){
    setstate(std::ios::badbit);
    sbuf_.close();
  }
  //�\�P�b�g�n���h��������ɉ��
  socket_t release(){
    setstate(std::ios::badbit);
    return sbuf_.release();
  }
  //���M
  int send(const void* buffer, int size){ return sbuf_.send(buffer, size); }
  //��M
  int recv(void* buffer, int size){ return sbuf_.recv(buffer, size); }
  //�\�P�b�g�n���h��
  socket_t socket()const{ return sbuf_.socket(); }

  int recv_line_with_timeout(std::string& buffer, const int time_out_in_microsecond, const char eol_delimit = '\n') {
    int num_confirmed_chars = 0;
    int num_eol_char = 0; // set to 1 if found. remains zero if end of stream.
    while(true) {
      while(num_confirmed_chars < num_chars_in_buffer_) {
	if(buffer_[num_confirmed_chars++] == eol_delimit) {
	  num_eol_char = 1;
	  goto FOUNDEOL;
	}
      }
      const int remainingBufferCapacity = buffer_.size() - num_chars_in_buffer_;
      if(remainingBufferCapacity <= 0) {
	buffer_.resize(buffer_.size() * 2); continue;
      }
      const int result = sbuf_.recv_line_with_timeout(buffer_.data() + num_chars_in_buffer_, remainingBufferCapacity, time_out_in_microsecond, eol_delimit);
      const bool errorOccurred = result == -1;
      if(errorOccurred) {
	if(0 < num_chars_in_buffer_)
	  break;
	return -1;
      }
      const bool timeOuted = result == 0;
      if(timeOuted)
	return 0;
      const int numCharsRead = result;
      num_chars_in_buffer_ += numCharsRead;
    }
    FOUNDEOL:
    buffer.assign(buffer_.data(), buffer_.data() + num_confirmed_chars - num_eol_char);
    copy(buffer_.begin() + num_confirmed_chars, buffer_.begin() + num_chars_in_buffer_, buffer_.begin());
    num_chars_in_buffer_ -= num_confirmed_chars;
    num_confirmed_chars  = 0;
    return buffer.size();
  }
private:
  socket_streambuf sbuf_;
  std::vector<char> buffer_;
  int               num_chars_in_buffer_;
};

//---------------------------------------------------------------------------
//�o�C���h���Ďg���\�P�b�g
class bound_socket
{
public:
  //�ʏ�̃R���X�g���N�^
  explicit bound_socket(int type)
    : type_(type), sock_(-1), is_bound_(false){}
  //�����̃\�P�b�g�n���h�����g���R���X�g���N�^
  bound_socket(int type, socket_t sock, bool is_bound)
    : type_(type), sock_(sock), is_bound_(is_bound){}
  //�f�X�g���N�^
  virtual ~bound_socket(){
    close();
  }
  //�\�P�b�g���N���[�Y
  void close(){
    is_bound_= false;
    if (sock_==-1) return;
#ifdef GIMITE_WIN32
    ::closesocket(sock_);
#else
    ::close(sock_);
#endif
    sock_= -1;
  }
  //�\�P�b�g�n���h�������
  socket_t release(){
    socket_t sock= sock_;
    sock_= -1;
    is_bound_= false;
    return sock;
  }
  //�����̃\�P�b�g���w��
  void socket(socket_t sock, bool is_bound){
    close();
    sock_= sock;
    is_bound_= is_bound;
  }
  //�\�P�b�g�̃^�C�v
  int type()const{ return type_; }
  //�\�P�b�g�n���h��
  socket_t socket()const{ return sock_; }
  //�������o�C���h����Ă��邩
  operator const void*()const{ return is_bound_? this : NULL; }
  bool operator!()const{ return !is_bound_; }

protected:
  //�o�C���h
  bool pbind(int port){
    if (is_bound_) return false;
    if (sock_==-1){
      sock_= ::socket(AF_INET, type_, 0);
      if (sock_==-1) return false;
    }
    sockaddr_in addr;
    addr.sin_family= AF_INET;
    addr.sin_port= htons(port);
    addr.sin_addr= ip_address(sock_uint32_t(INADDR_ANY)).addr();
      //���Fcygwin gcc 3.3.1�ł�INADDR_ANY�̌^��sock_uint32_t�ł͂Ȃ��̂ŁA
      //    �B�����𖳂������߂̃L���X�g���K�v�B
    if (::bind(sock_, (const sockaddr*)&addr, sizeof(addr))==-1)
      return false;
    is_bound_= true;
    return true;
  }
  
private:
  const int type_;
  socket_t sock_;
  bool is_bound_;
  
};

//---------------------------------------------------------------------------
//�T�[�o�p�X�g���[���\�P�b�g
class server_stream_socket: public bound_socket
{
public:
  //�����ɂ̓o�C���h���Ȃ��R���X�g���N�^
  server_stream_socket()
    : bound_socket(SOCK_STREAM){}
  //�����Ƀo�C���h����R���X�g���N�^
  explicit server_stream_socket(int port, int backlog= 5)
      : bound_socket(SOCK_STREAM){
    bind(port, backlog);  
  }
  //�����̃\�P�b�g�n���h�����g���R���X�g���N�^
  server_stream_socket(socket_t sock, bool is_bound)
    : bound_socket(SOCK_STREAM, sock, is_bound){}
  //�o�C���h
  bool bind(int port, int backlog= 5){
    if (!pbind(port)) return false;
    if (::listen(socket(), backlog)!=-1) return true;
    close();
    return false;
  }
  //�ڑ��v�����󂯓����
  socket_t accept(){
    return ::accept(socket(), 0, 0);
  }
  
};

//---------------------------------------------------------------------------
//�f�[�^�O�����\�P�b�g
class datagram_socket: public bound_socket
{
public:
  //�����ɂ̓o�C���h���Ȃ��R���X�g���N�^
  datagram_socket()
    : bound_socket(SOCK_DGRAM){}
  //�����Ƀo�C���h����R���X�g���N�^
  explicit datagram_socket(int port)
      : bound_socket(SOCK_DGRAM){
    bind(port);
  }
  //�����̃\�P�b�g�n���h�����g���R���X�g���N�^
  datagram_socket(socket_t sock, bool is_bound)
    : bound_socket(SOCK_DGRAM, sock, is_bound){}
  //�o�C���h
  bool bind(int port){
    return pbind(port);
  }
  //�f�[�^����M
  int recvfrom(void* buffer, int size, socket_address* addr= 0,
      int flags= 0){
    sockaddr_in saddr;
    sock_len_t addr_len= sizeof(saddr);
    int result= ::recvfrom(socket(), (char*)buffer, size, flags,
      (sockaddr*)&saddr, &addr_len);
      //���FWinsock��recvfrom�̑�2�p�����[�^�^��char*�Ȃ̂�
      //    �L���X�g���K�v�B
    if (addr){
      addr->ip= ip_address(saddr.sin_addr);
      addr->port= ntohs(saddr.sin_port);
    }
    return result;
  }
  //�f�[�^�𑗐M
  int sendto(const void* buffer, int size, const socket_address* addr,
      int flags= 0){
    sockaddr_in saddr;
    saddr.sin_family= AF_INET;
    saddr.sin_addr= addr->ip.addr();
    saddr.sin_port= htons(addr->port);
    return ::sendto(socket(), (const char*)buffer, size, flags,
      (const sockaddr*)&saddr, sizeof(saddr));
      //���FWinsock��sendto�̑�2�p�����[�^�^��const char*�Ȃ̂�
      //    �L���X�g���K�v�B
  }
  
};

//---------------------------------------------------------------------------
}//namespace gimite
//---------------------------------------------------------------------------
#endif
