#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

// Optional: use these functions to add debug or error prints to your application
//#define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    
    time_t wait_sec, wait_nsec;
    
    // Wait before obtaining mutex
    wait_sec = thread_func_args->obtain_ms / 1000;
    wait_nsec = (thread_func_args->obtain_ms % 1000) * 1000000;

    // from Linux System Programming pg 383
    struct timespec req, rem;

    req.tv_sec = wait_sec;  
    req.tv_nsec = wait_nsec;
    struct timespec *a, *b; 
    
    a = &req;
    b = &rem;
    
    while (nanosleep (a, b) && errno == EINTR) {
        struct timespec *tmp = a;
        a = b;
        b = tmp;   
    }

    int rc;
    rc = pthread_mutex_lock(thread_func_args->mutex);
    if (rc != 0){
        ERROR_LOG("pthread_mutex_lock() failed");
        return thread_param;
    } 

    DEBUG_LOG("Mutex Locked");

    // Wait before releasing mutex
    wait_sec = thread_func_args->release_ms / 1000;
    wait_nsec = (thread_func_args->release_ms % 1000) * 1000000;

    // from Linux System Programming pg 383
    req.tv_sec = wait_sec;
    req.tv_nsec = wait_nsec;
    a = &req;
    b = &rem;

    while (nanosleep (a, b) && errno == EINTR) {
        struct timespec *tmp = a;
        a = b;
        b = tmp;   
    }
    
    // Set success
    thread_func_args->thread_complete_success = true;

    rc = pthread_mutex_unlock(thread_func_args->mutex);
    if (rc != 0){
        ERROR_LOG("pthread_mutex_unlock() failed");
        return thread_param;
    }

    DEBUG_LOG("Mutex Unlocked");
    
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    // Check inputs first
    if (wait_to_obtain_ms < 0 || wait_to_release_ms < 0){
        ERROR_LOG("Negative wait time");
        return false;
    }

    struct thread_data* thread_setup = (struct thread_data*) malloc(sizeof(struct thread_data));

    if (!thread_setup){
        ERROR_LOG("malloc() failed for thread_data");
        return false;
    }

    thread_setup->thread_complete_success = false;
    thread_setup->obtain_ms = wait_to_obtain_ms;
    thread_setup->release_ms = wait_to_release_ms;
    thread_setup->mutex = mutex;

    // setup mutex
    pthread_mutex_init(mutex, NULL);

    int rc;

    // create a thread and call threadfunc()
    rc = pthread_create(thread, NULL, threadfunc, (void *)thread_setup);

    if (rc != 0){
        ERROR_LOG("pthread_create() failed");
        return false;
    }
    DEBUG_LOG("pthread_create() called");

    // wait for threadfunc() to finish
    rc = pthread_join(*thread, NULL);
    if (rc != 0){
        ERROR_LOG("pthread_join() failed");
    }
    DEBUG_LOG("pthread_join() called");

    bool success = thread_setup->thread_complete_success;
    free (thread_setup);

    return success;
}

