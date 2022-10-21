/* threads.h: C++ wrapper for threads. */

#ifndef THREADS_H
#define THREADS_H

extern "C" {
#include <pthread.h>
}

class Mutex {
friend class Condition;

  private:
    pthread_mutex_t mutex;

  public:
    Mutex() { pthread_mutex_init(&mutex, NULL); }
    ~Mutex() { pthread_mutex_destroy(&mutex); }
    void lock() { pthread_mutex_lock(&mutex); }
    void unlock() { pthread_mutex_unlock(&mutex); }
    void try_lock() { pthread_mutex_trylock(&mutex); }
};

class Condition {

  private:
    pthread_cond_t cond;

  public:
    Condition() { pthread_cond_init(&cond, NULL); }
    ~Condition() { pthread_cond_destroy(&cond); }
    void wait(Mutex *mutex) { pthread_cond_wait(&cond, &mutex->mutex); }
    void signal() { pthread_cond_signal(&cond); }
    void broadcast() { pthread_cond_broadcast(&cond); }
};

inline void start_thread(void * (*start_routine)(void *), void *arg) {
    pthread_t thread;

    pthread_create(&thread, NULL, start_routine, arg);
};

#endif

