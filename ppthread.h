#ifndef _PPTHREAD_HEADER_
#define _PPTHREAD_HEADER_

// add -pthread to gcc

#include <pthread.h>

class PThread {
  virtual void run() = 0;

  bool needToDeleteMySelf;
  void prerun();
  static void* invoker(void* thisPointer);
  pthread_t threadID;
 protected:
  pthread_t getThreadID() const { return threadID; }
  void deleteMySelfAtThreadExit() { needToDeleteMySelf = true; }
 public:
  bool start(const bool detached = false);
  bool join();
  virtual ~PThread();
};

#endif // #ifndef _PPTHREAD_HEADER_
