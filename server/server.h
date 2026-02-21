// Reusing some components from threading.h
#include <stdbool.h>
#include <pthread.h>

#include <netinet/in.h>

// Holds thread and its parameters
struct Node_thread{
    pthread_t thread;
    struct thread_handleData* paramData;
    struct Node_thread* next;
};

// Parameters for handleRequests()
struct thread_handleData{
    int conn_fd;
    bool connection_alive;
    char client_addr_str[INET6_ADDRSTRLEN];
};

// Parameters for acceptRequests()
struct thread_acceptData{
    int sockfd;
};

// Structs with mutex for thread-safety
struct linkedList_thread{
    struct Node_thread* head;
    pthread_mutex_t* mutex;
};

struct fileHandler{
    FILE *fptr;
    pthread_mutex_t* mutex;
};