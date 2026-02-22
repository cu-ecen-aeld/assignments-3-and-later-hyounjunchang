/*
Referenced from
- https://beej.us/guide/bgnet/source/examples/server.c
Modified by Hyounjun Chang for ECEN5713
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include <syslog.h>
#include <fcntl.h>
#include <stdbool.h> 
#include <pthread.h>

#include <time.h>
#include <stdint.h>
#include <sys/queue.h>
#include "server.h"

#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 10     // how many pending connections queue holds

// File transfer
#define FILENAME "/var/tmp/aesdsocketdata"
#define BUFSIZE 1024

// Clock
#define TIME_STR_SIZE 100

// Global Variable
bool CLOSE_SERVER = false;

// structs with mutex
pthread_mutex_t *fileMutex;
pthread_mutex_t *listMutex;
FILE* fptr;
int sockfd;

// Initialize list
SLIST_HEAD(slisthead, entry);
struct slisthead head;                  // Singly linked list

void timer_handler(int sig, siginfo_t *si, void *uc);
void signal_handler (int signo);
void *get_in_addr(struct sockaddr *sa);
int printCurrentTime(void);
int writeTimeEvery10Sec(void* thread_param);
void* acceptRequests(void* thread_param);
void* handleRequests(void* thread_param);

// handler for SIGINT and SIGTERM, exiting if signal
void signal_handler (int signo)
{   
    // SIGPIPE and SIGABRT added for valgrind
    if (signo == SIGINT || signo == SIGTERM ){
        CLOSE_SERVER = true;
    }
    return;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int printCurrentTime(void){
    // Time: from https://man7.org/linux/man-pages/man3/strftime.3.html
    char timestamp_str[11] = "timestamp:";
    char timestr[TIME_STR_SIZE+1];
    time_t t;
    struct tm *time_tmp;
    int rc;

    t = time(NULL);
    time_tmp = localtime(&t);
    if (time_tmp == NULL) {
        syslog(LOG_ERR, "Error getting local time, Error: %s", strerror(errno));
    }
    size_t timestr_size = strftime(timestr, sizeof(timestr), "%a, %d %b %Y %T %z", time_tmp);
    if (timestr_size == 0) {
        syslog(LOG_ERR, "Error with strftime(), Error: %s", strerror(errno));
    }
    timestr[timestr_size] = '\n'; // append with newline

    // write time to file
    rc = pthread_mutex_lock(fileMutex);
    if (rc != 0){
        syslog(LOG_ERR,"pthread_mutex_lock() failed, Error: %s", strerror(errno));
    }
    // write "timestamp:"
    size_t nmem_written = fwrite(timestamp_str, strlen(timestamp_str), 1, fptr);
    if (nmem_written == 0){
        syslog(LOG_ERR, "Error with fwrite(), Error: %s", strerror(errno));
        rc = pthread_mutex_unlock(fileMutex);
    }
    // write time to file
    nmem_written = fwrite(timestr, timestr_size+1, 1, fptr);
    if (nmem_written == 0){
        syslog(LOG_ERR, "Error with fwrite(), Error: %s", strerror(errno));
        rc = pthread_mutex_unlock(fileMutex);
    }
    rc = pthread_mutex_unlock(fileMutex);
    if (rc != 0){
        syslog(LOG_ERR,"pthread_mutex_unlock() failed, Error: %s", strerror(errno));
    }
    return 0;
}

void* writeTimeEvery10sec(void* thread_param){
    struct timespec start, current, target;
    clock_gettime(CLOCK_MONOTONIC, &start);
    target.tv_sec = start.tv_sec + 10;
    target.tv_nsec = start.tv_nsec; 

    while (1){
        // return immdiately
        if (CLOSE_SERVER){
            break;
        }       
        clock_gettime(CLOCK_MONOTONIC, &current);     
        if ((current.tv_sec > target.tv_sec) ||
            (current.tv_sec == target.tv_sec && current.tv_nsec > target.tv_nsec)){
            target.tv_sec = current.tv_sec + 10; 
            int rc = printCurrentTime();
            if (rc != 0){
                syslog(LOG_ERR,"printCurrentTime() failed, Error: %s", strerror(errno));
            }
        }
    }   
    return thread_param;
}

void* acceptRequests(void* thread_param){
    struct sockaddr_storage their_addr; // connector's address info
	socklen_t sin_size;
    int rc, new_conn_fd;
    char client_str[INET6_ADDRSTRLEN];

    while (1){
        sin_size = sizeof their_addr;
        new_conn_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_conn_fd == -1) {
            if (ECONNABORTED){
                syslog(LOG_DEBUG, "Connection to socket aborted, no longer accepting requests...");
            }
            else{
                syslog(LOG_ERR, "Failed during accept(), Error: %s", strerror(errno));
            }
            break;
        }
        int status;
        status = fcntl(new_conn_fd, F_SETFL, O_NONBLOCK);
        if (status == -1){
            syslog(LOG_ERR, "Failed during fnctl(), Error: %s", strerror(errno));
            close(new_conn_fd);
            break;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), client_str, sizeof client_str);
        syslog(LOG_DEBUG, "Accepted connection from %s\n", client_str);

        //Create a new child thread too handle connection
        struct entry *n1 = (struct entry*) malloc(sizeof(struct entry));
        if (!n1){
            syslog(LOG_ERR, "malloc() failed creating new thread arguments, Error: %s", strerror(errno));
            CLOSE_SERVER = true;
        }
        
        // setup parameters so it can be freed
        n1->conn_fd = new_conn_fd;
        n1->connection_closed = false;
        strcpy(n1->client_addr_str, client_str);

        SLIST_INSERT_HEAD(&head, n1, entries);

        rc = pthread_create(&(n1->thread), NULL, handleRequests, (void*)n1);
        if (rc != 0){
            syslog(LOG_ERR, "pthread_create() failed creating new thread, Error: %s", strerror(errno));
            CLOSE_SERVER = true;
        }
    }
    return thread_param;
}

// handle_requests
void* handleRequests(void* thread_param){
    struct entry* thread_func_args = (struct entry*) thread_param;
    int conn_fd = thread_func_args->conn_fd;

    // initialize buffers in stack
    char recv_buf[BUFSIZE+1];
    char send_buf[BUFSIZE+1];
    bool last_char_is_newline = false;
    int rc;

    pthread_t tid = pthread_self(); 
    while(1){
        // connection operations first
        ssize_t bytes_read_socket = recv(conn_fd, recv_buf, BUFSIZE, MSG_DONTWAIT);
        if (bytes_read_socket < 0){
            if (errno != EAGAIN){
                syslog(LOG_ERR, "Errors recv(), tid: %ld, Error: %s", tid, strerror(errno));
                thread_func_args->connection_closed = true;
            }
        }
        else if (bytes_read_socket == 0){
            // if recv returns zero, that means the connection has been closed:
            thread_func_args->connection_closed = true;    
        }
        else{
            syslog(LOG_DEBUG, "%ld bytes received from %s", bytes_read_socket, thread_func_args->client_addr_str);

            // Update Linked List of Threads
            rc = pthread_mutex_lock(fileMutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_lock() failed, Error: %s", strerror(errno));
            } 

            // write to file
            size_t nmem_written = fwrite(recv_buf, bytes_read_socket, 1, fptr);
            if (nmem_written == 0){
                syslog(LOG_ERR, "Error with fwrite(), tid: %ld, Error: %s", tid, strerror(errno));
            }

            for (int i = 0; i < bytes_read_socket; i++){
                if (recv_buf[i] == '\n'){
                    last_char_is_newline = true;
                    
                    // set fileptr to beginning
                    rc = fseek(fptr, 0, SEEK_SET);
                    if (rc == -1){
                        syslog(LOG_ERR, "Error with fseek(), tid: %ld, Error: %s", tid, strerror(errno));
                    }

                    // send file
                    size_t bytes_to_send = fread(send_buf, 1, 1, fptr);

                    while (bytes_to_send > 0){
                        // Blocking write
                        ssize_t bytes_sent_socket = send(conn_fd, send_buf, bytes_to_send, 0);
                        if (bytes_sent_socket != bytes_to_send){
                            syslog(LOG_ERR, "Error with send(), tid: %ld, Error: %s", tid, strerror(errno));
                        }
                        bytes_to_send = fread(send_buf, 1, 1, fptr);
                    }
                    
                    syslog(LOG_DEBUG, "File sent to %s\n", thread_func_args->client_addr_str);
                    
                    // reset file pointer
                    rc = fseek(fptr, 0, SEEK_END);
                    if (rc == -1){
                        syslog(LOG_ERR, "Error with fseek(), tid: %ld, Error: %s", tid, strerror(errno));
                    }             
                }
                else{
                    last_char_is_newline = false;
                }
            }
            
            // Release Mutex to File only if last character is \n (end of packet)
            if (last_char_is_newline){
                rc = pthread_mutex_unlock(fileMutex);
                if (rc != 0){
                    syslog(LOG_ERR,"pthread_mutex_unlock() failed, Error: %s", strerror(errno));
                } 
            }

        }
        if (CLOSE_SERVER || thread_func_args->connection_closed){
            close(conn_fd);
            syslog(LOG_DEBUG, "Close connection with fd: %d", conn_fd);
            break;
        }
    }
    return thread_param; 
}


int main(int argc, char **argv){
    // Set up mutex
    pthread_mutex_t fMutex = PTHREAD_MUTEX_INITIALIZER;
    fileMutex = &fMutex;

    // setup list
    SLIST_INIT(&head);

    // Set up signal handler
    if (signal (SIGINT, signal_handler) == SIG_ERR) {
        fprintf (stderr, "Cannot handle SIGINT!\n");
        exit(-1);
    }
    if (signal (SIGTERM, signal_handler) == SIG_ERR) {
        fprintf (stderr, "Cannot handle SIGTERM!\n");
        exit(-1);
    }

    bool RUN_AS_DAEMON = false;
    if (argc >= 2){
        if (strcmp(argv[1],"-d") == 0){
            RUN_AS_DAEMON = true;
        }
    }

    // Open syslog
    if (RUN_AS_DAEMON){
        if (daemon(0, 0) == -1) {
            // Log error if daemonization fails
            perror("daemon() fails");
            exit(-1);
        }
        // log as daemon
        openlog("aesdsocket", LOG_CONS | LOG_PID, LOG_DAEMON);
        syslog(LOG_DEBUG, "Starting server as daemon...");
    }
    else{
        // write to console if no logger, Include PID, no delay, error to console
        openlog("aesdsocket", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER);
        syslog(LOG_DEBUG, "Starting server...");
    }
    
    fptr = fopen(FILENAME, "w+"); // create file, overwrite if exists, with read and write permission

    // Check if the file was opened successfully
    if (fptr == NULL) {
        syslog(LOG_ERR, "Error opening /var/tmp/aesdsocketdata, Error: %s", strerror(errno));
        closelog();
        exit(-1);
    }

	// listen on sock_fd, new connection on conn_fd
	struct addrinfo hints, *servinfo, *p;
    int rc;
	int yes=1;

	memset(&hints, 0, sizeof hints); // clear memory
	hints.ai_family = AF_INET; // IPv4
	hints.ai_socktype = SOCK_STREAM; // TCP
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rc = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s\n", gai_strerror(rc));
        closelog();
	    exit(-1);
	}

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            syslog(LOG_ERR, "setsockopt() failed, Error: %s", strerror(errno));
            closelog();
            exit(-1);
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        // bind successful
        break;
    }
	
	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
        syslog(LOG_ERR, "Failed to bind(), Error: %s", strerror(errno));
		closelog();
		exit(-1);
	}
	if (listen(sockfd, BACKLOG) == -1) {
		syslog(LOG_ERR, "Failed during listen(), Error: %s", strerror(errno));
        close(sockfd);
		closelog();
		exit(-1);
	}
    printf("server: waiting for connections...\n");
    
    // a new thread for 10 second timer
    pthread_t timer_thread;
    rc = pthread_create(&timer_thread, NULL, writeTimeEvery10sec, NULL);
    if (rc != 0){
        syslog(LOG_ERR, "pthread_create() failed creating timer thread, Error: %s", strerror(errno));
        close(sockfd);
        closelog();
        exit(-1);
    }
   
    pthread_t accept_thread; 

    //Create a new child thread to handle connections
    rc = pthread_create(&accept_thread, NULL, acceptRequests, NULL);
    if (rc != 0){
        syslog(LOG_ERR, "pthread_create() failed creating new thread, Error: %s", strerror(errno));
        close(sockfd);
        closelog();
        exit(-1);
    }

    struct entry *np;
	while(!CLOSE_SERVER) {  // main loop
        SLIST_FOREACH(np, &head, entries){
            if (np->connection_closed){
                // wait for handleRequests thread to finish
                pthread_join(np->thread, NULL);
                SLIST_REMOVE(&head, np, entry, entries);/* Deletion */
                free(np);
            }
        }
        
    }

    syslog(LOG_DEBUG, "Caught signal, exiting");
    
    // close timer thread
    pthread_join(timer_thread, NULL);
    // Close all connections
    SLIST_FOREACH(np, &head, entries){
        // wait for handleRequests thread to finish
        close(np->conn_fd);
        pthread_join(np->thread, NULL);
        SLIST_REMOVE(&head, np, entry, entries);/* Deletion */
        free(np);
    }

    // Stop reads/writes
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    pthread_join(accept_thread, NULL);
    
    // close file
    fclose(fptr);
    remove(FILENAME);
    syslog(LOG_DEBUG, "Shutting Down Server...");
    closelog();
    exit(0);
}