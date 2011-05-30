#ifndef _HEADER_PMUTEX
#define _HEADER_PMUTEX

// add -pthread to g++
#include <pthread.h>
#include <time.h>

class ConditionVariable;

class Mutex {
  pthread_mutex_t mutex;
  bool            locked;
 public:
  Mutex() {
    pthread_mutex_init(&mutex, NULL);
    locked = false;
  }
  ~Mutex() {
    pthread_mutex_destroy(&mutex);
  }
  inline void lock() {
    locked = (pthread_mutex_lock(&mutex) == 0);
  }
  inline bool trylock() {
    locked = (pthread_mutex_trylock(&mutex) == 0);
    return locked;
  }
  inline void unlock() {
    pthread_mutex_unlock(&mutex);
    locked = false;
  }
  inline bool islocked() const { return locked; }

  class scoped_lock {
    Mutex& m;
   public:
    inline scoped_lock(Mutex& m) : m(m) {
      m.lock();
    }
    inline void lock() {
      m.lock();
    }
    inline void unlock() {
      m.unlock();
    }
    inline bool islocked() const { return m.islocked(); }
    inline ~scoped_lock() {
      m.unlock();
    }
  };
  friend class ConditionVariable;
};

class ConditionVariable {
  pthread_cond_t cond;
 public:
  ConditionVariable() {
    pthread_cond_init(&cond, NULL);
  }
  ~ConditionVariable() {
    pthread_cond_destroy(&cond);
  }
  inline void wait(Mutex& m) {
    pthread_cond_wait(&cond, &m.mutex);
  }
  inline bool timedWait(Mutex& m, time_t sec, long nanosec = 0l) {
    struct timespec abstime;
    clock_gettime(CLOCK_REALTIME, &abstime);
    abstime.tv_sec  += sec;
    abstime.tv_nsec += nanosec;
    const int result = pthread_cond_timedwait(&cond, &m.mutex, &abstime);
    if(result == 0)
      return true;
    return false;
  }
  inline void signal() {
    pthread_cond_signal(&cond);
  }
  inline void signalAll() {
    pthread_cond_broadcast(&cond);
  }
};

#endif //ifndef _HEADER_PMUTEX

