#include <pthread.h>
#include "ppthread.h"

using namespace std;

bool PThread::start(const bool detached)
{
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE);
  const int result = pthread_create(&threadID, NULL, &PThread::invoker, reinterpret_cast<void *>(this));
  pthread_attr_destroy(&attr);
  const bool hasSuceeded = result == 0;
  return hasSuceeded;
}

void* PThread::invoker(void* thisPointer)
{
  PThread* thisInstance = reinterpret_cast<PThread*>(thisPointer);
  thisInstance->prerun();
  pthread_exit(NULL);
  return NULL;
}

void PThread::prerun()
{
  needToDeleteMySelf = false;
  run();
  if(needToDeleteMySelf) {
    delete this;
  }
}

bool PThread::join()
{
  void* returnValue;
  const int result = pthread_join(getThreadID(), &returnValue);
  const bool hasSuceeded = result == 0;
  return hasSuceeded;
}

PThread::~PThread()
{
}

