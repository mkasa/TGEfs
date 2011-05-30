#ifndef _LIBTGELOCK_HEADER
#define _LIBTGELOCK_HEADER

// NOTE: You need to put socket.h, ppthread.(h|cc) and pmutex.h on the same project
#include <string>
#include "socket.h"
#include "pmutex.h"
#include "ppthread.h"

// IMPORTANT NOTE: This class is not reentrant, that is, you must not make simultaneous calls from multiple threads.
class TGELock : PThread {
  std::string serverHostName;
  int         portNumber;

  Mutex                  objectState_mutex;
  bool                   weHaveTheLock;
  bool                   isThreadRunning;
  ConditionVariable      isThreadRunning_cond;
  bool                   hasEncounteredAnyError;
  std::string            errorMessageIfAny;
  gimite::socket_stream* mySocketStream;

  void run() {
    while(true) {
      {
	timeval tv;
	tv.tv_sec  = UPDATE_INTERVAL_IN_SECONDS;
	tv.tv_usec = 0;
	select(0, NULL, NULL, NULL, &tv);
      }
      {
	Mutex::scoped_lock lock(objectState_mutex);
	if(!weHaveTheLock || mySocketStream == NULL || mySocketStream->fail()) {
	  weHaveTheLock   = false;
	  isThreadRunning = false;
	  isThreadRunning_cond.signal();
	  break;
	}
	*mySocketStream << "L " << int(EXPIRE_INTERVAL_IN_SECONDS) << std::endl;
      }
      {
	std::string line;
	const int result = mySocketStream->recv_line_with_timeout(line, WAIT_DURATION_IN_SECONDS);
	const bool errorOccurred = result < 0;
	if(errorOccurred) {
	  Mutex::scoped_lock lock(objectState_mutex);
	  weHaveTheLock   = false;
	  isThreadRunning = false;
	  isThreadRunning_cond.signal();
	  break;
	}
	// line == "OK"
      }
    }
  }
  enum {
    WAIT_DURATION_IN_SECONDS   = 1,
    UPDATE_INTERVAL_IN_SECONDS = 5,
    EXPIRE_INTERVAL_IN_SECONDS = 10,
    NUMBER_OF_RETRIES          = 100
  };
  void init() {
    weHaveTheLock        = false;
    isThreadRunning      = false;
  }

 public:
  TGELock()
    : hasEncounteredAnyError(false), mySocketStream(NULL) { init(); }
  TGELock(const char* serverHostName, const int portNumber)
    : serverHostName(serverHostName), portNumber(portNumber), hasEncounteredAnyError(false), mySocketStream(NULL) { init(); }
  TGELock(const std::string& serverHostName, const int portNumber)
    : serverHostName(serverHostName), portNumber(portNumber), hasEncounteredAnyError(false), mySocketStream(NULL) { init(); }
  ~TGELock() {
  }
  std::string emptyString;
  bool isFailed() const { return hasEncounteredAnyError; }
  const std::string& getErrorMessage() const {
    if(hasEncounteredAnyError)
      return errorMessageIfAny;
    return emptyString;
  }
  void setTGELockdServer(const char* serverHostName, const int portNumber) {
    this->serverHostName = serverHostName;
    this->portNumber     = portNumber;
  }
  bool lock() {
    mySocketStream = new gimite::socket_stream(serverHostName.c_str(), portNumber);
    if(!*mySocketStream) {
      delete mySocketStream;
      mySocketStream         = NULL;
      hasEncounteredAnyError = true;
      errorMessageIfAny      = "Could not connect to the server";
      return false;
    }
    *mySocketStream << "L " << int(EXPIRE_INTERVAL_IN_SECONDS) << std::endl;
    // std::cerr << "New socket instance" << std::endl;
    for(int tryCount = 0; tryCount < NUMBER_OF_RETRIES; tryCount++) {
      // std::cerr << "Try count = " << tryCount << std::endl;
      std::string line;
      const int receivedSize = mySocketStream->recv_line_with_timeout(line, UPDATE_INTERVAL_IN_SECONDS * 1000000);
      const bool errorOccurred = receivedSize < 0;
      if(errorOccurred) {
	hasEncounteredAnyError = true;
	errorMessageIfAny      = "::recv error";
	return false;
      }
      if(!line.empty()){
	if(line == "OK") {
	  std::cerr << "Try to acquire lock" << std::endl;
	  Mutex::scoped_lock lock(objectState_mutex);
	  while(isThreadRunning) {
	    std::cerr << "Thread blocking" << std::endl;
	    weHaveTheLock = false; // force unlock.
	    isThreadRunning_cond.wait(objectState_mutex); // wait existing thread to terminate
	  }
	  // std::cerr << "Invoke new thread" << std::endl;
	  weHaveTheLock   = true;
	  isThreadRunning = true;
	  start(true);
	  return true;
	} else {
	  // std::cerr << "Error1" << std::endl;
	  hasEncounteredAnyError = true;
	  errorMessageIfAny      = "Unknown server response : " + line;
	  return false;
	}
      } else {
	// std::cerr << "LINE: '" << line << "'" << std::endl;
      }
    }
    // std::cerr << "Error2" << std::endl;
    hasEncounteredAnyError = true;
    errorMessageIfAny      = "Server timeout";
    return false;
  }
  void unlock() {
    {
      Mutex::scoped_lock lock(objectState_mutex);
      if(weHaveTheLock) {
	std::cerr << "Release lock" << std::endl;
	weHaveTheLock = false;
	if(mySocketStream != NULL) {
	  *mySocketStream << "U\nQUIT" << std::endl;
	  std::string line;
	  const int receivedSize = mySocketStream->recv_line_with_timeout(line, UPDATE_INTERVAL_IN_SECONDS * 1000000);
	  // line == "OK" (shoule be), but we would not check because we cannot do anything even if it did not hold.
      mySocketStream->close();
	}
      }
      delete mySocketStream;
      mySocketStream = NULL;
    }
  }
};

#endif // #ifndef _LIBTGELOCK_HEADER

