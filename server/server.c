/*
Referenced from
- https://beej.us/guide/bgnet/source/examples/server.c
Modified by Hyounjun Chang for ECEN5713

For Assignment 8: Claude AI was used to debug
https://claude.ai/chat/63e236bb-6020-40a9-ae6b-258c7edeb090
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include <syslog.h>
#include <fcntl.h>
#include <stdbool.h>

#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 10     // how many pending connections queue holds

#define BUFSIZE 2048
#define TIME_STR_SIZE 100

#define USE_AESD_CHAR_DEVICE 1

#ifdef USE_AESD_CHAR_DEVICE
#define FILENAME "/dev/aesdchar"
#else
#define FILENAME "/var/tmp/aesdsocketdata"
#endif

#include <pthread.h>
#include "server.h"

// for Linked List
struct slisthead head;
SLIST_HEAD(slisthead, entry);

// Global Variable, guarntees atomicity
volatile sig_atomic_t CLOSE_SERVER = 0;
volatile sig_atomic_t SERVER_LISTENING = 0;

pthread_mutex_t *fmutex;
pthread_mutex_t *lmutex;

// for SIGINT termination (since using single-thread)
FILE *wptr;

// handler for SIGINT and SIGTERM, exiting if signal
void signal_handler (int signo)
{
    if (signo == SIGINT || signo == SIGTERM){
        if (SERVER_LISTENING){
            CLOSE_SERVER = 1;
        }
        else{
            syslog(LOG_DEBUG, "Caught signal, exiting");
            #ifndef USE_AESD_CHAR_DEVICE
            if (wptr){
                fclose(wptr);
            }
            remove(FILENAME);
            #endif
            closelog();
            exit(0); 
        }
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

void* writeTimeEvery10sec(void* thread_param){
    struct timespec start, current, target;
    clock_gettime(CLOCK_MONOTONIC, &start);
    target.tv_sec = start.tv_sec + 10;
    target.tv_nsec = start.tv_nsec; 

    while (!CLOSE_SERVER){
        clock_gettime(CLOCK_MONOTONIC, &current);     
        if ((current.tv_sec > target.tv_sec) ||
            (current.tv_sec == target.tv_sec && current.tv_nsec > target.tv_nsec)){
            target.tv_sec = current.tv_sec + 10; 
            
            // Print current time:
            // Time: from https://man7.org/linux/man-pages/man3/strftime.3.html
            char timestr[TIME_STR_SIZE+1];
            time_t t;
            struct tm *time_tmp;
            int rc;

            t = time(NULL);
            time_tmp = localtime(&t);
            if (time_tmp == NULL) {
                syslog(LOG_ERR, "Error getting local time, Error: %s", strerror(errno));
            }
            size_t timestr_size = strftime(timestr, sizeof(timestr), "timestamp: %a, %d %b %Y %T %z", time_tmp);
            if (timestr_size == 0) {
                syslog(LOG_ERR, "Error with strftime(), Error: %s", strerror(errno));
            }
            timestr[timestr_size] = '\n'; // append with newline

            // write time to file
            rc = pthread_mutex_lock(fmutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_lock() failed, Error: %s", strerror(errno));
            }
            fseek(wptr, 0, SEEK_END);
            // write time to file
            size_t nmem_written = fwrite(timestr, timestr_size+1, 1, wptr);
            if (nmem_written == 0){
                syslog(LOG_ERR, "Error with fwrite(), Error: %s", strerror(errno));
            }

            rc = pthread_mutex_unlock(fmutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_unlock() failed, Error: %s", strerror(errno));
            }
        }
        sleep (1); // so it doesn't spin lock
    }   
    return thread_param;
}

void* handleRequests(void* thread_param) {
    struct entry* thread_func_args = (struct entry*) thread_param;

    int conn_fd = thread_func_args->conn_fd;
    struct sockaddr_in client_addr = thread_func_args-> client_addr;

    // buffers
    char recv_buf[BUFSIZE+1];
    char send_buf[BUFSIZE+1];
    char client_ip[INET_ADDRSTRLEN];

    inet_ntop(client_addr.sin_family, get_in_addr((struct sockaddr *)&client_addr), client_ip, sizeof(client_ip));
    syslog(LOG_DEBUG, "Accepted connection from %s\n", client_ip);

    while(!CLOSE_SERVER || !thread_func_args->conn_closed){
        ssize_t bytes_read_socket = recv(conn_fd, recv_buf, BUFSIZE, MSG_DONTWAIT);
        if (bytes_read_socket < 0){
            if (errno != EAGAIN){
                syslog(LOG_ERR, "Errors recv(), Error: %s", strerror(errno));
                thread_func_args->conn_closed = true;
                break;
            }
        }
        else if (bytes_read_socket == 0){
            // if recv returns zero, that means the connection has been closed:
            thread_func_args->conn_closed = true;
            break;
        }
        else{
            syslog(LOG_DEBUG, "%ld bytes received from %s", bytes_read_socket, client_ip);
            // Write to File
            pthread_mutex_lock(fmutex);

            #ifdef USE_AESD_CHAR_DEVICE
            int write_fd = open(FILENAME, O_WRONLY);
            if (write_fd < 0) {
                syslog(LOG_ERR, "Error opening device for write: %s", strerror(errno));
                pthread_mutex_unlock(fmutex);
                continue;
            }

            ssize_t nmem_written = write(write_fd, recv_buf, bytes_read_socket);
            if (nmem_written < 0){
                syslog(LOG_ERR, "Error with fwrite(), Error: %s", strerror(errno));
            }
            
            int local_read_fd = open(FILENAME, O_RDONLY);
            if (local_read_fd < 0) {
                syslog(LOG_ERR, "Error opening device for read: %s", strerror(errno));
                pthread_mutex_unlock(fmutex);
                continue;
            }
            ssize_t bytes_to_send = read(local_read_fd, send_buf, BUFSIZE);


            syslog(LOG_DEBUG, "Bytes to send: %ld", bytes_to_send);
            while (bytes_to_send > 0){
                ssize_t bytes_sent_socket = sendto(conn_fd, send_buf, bytes_to_send, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
                if (bytes_sent_socket != bytes_to_send){
                    syslog(LOG_ERR, "Error with send(), Error: %s", strerror(errno));
                }
                else{
                    syslog(LOG_DEBUG, "Bytes sent: %ld", bytes_to_send);
                }
                bytes_to_send = read(local_read_fd, send_buf, BUFSIZE);
                syslog(LOG_DEBUG, "Bytes to send: %ld", bytes_to_send);
            }
            close(write_fd);
            close(local_read_fd);
            #else
            fseek(wptr, 0, SEEK_END);

            size_t nmem_written = fwrite(recv_buf, bytes_read_socket, 1, wptr);
            if (nmem_written == 0){
                syslog(LOG_ERR, "Error with fwrite(), Error: %s", strerror(errno));
            }
            fseek(wptr, 0, SEEK_SET);

            size_t bytes_to_send = fread(send_buf, 1, BUFSIZE, wptr);
            syslog(LOG_DEBUG, "Bytes to send: %ld", bytes_to_send);
            while (bytes_to_send > 0){
                // Blocking write
                ssize_t bytes_sent_socket = sendto(conn_fd, send_buf, bytes_to_send, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
                if (bytes_sent_socket != bytes_to_send){
                    syslog(LOG_ERR, "Error with send(), Error: %s", strerror(errno));
                }
                else{
                    syslog(LOG_DEBUG, "Bytes sent: %ld", bytes_to_send);
                }
                bytes_to_send = fread(send_buf, 1, BUFSIZE, wptr);
                syslog(LOG_DEBUG, "Bytes to send: %ld", bytes_to_send);
            }
            #endif
            syslog(LOG_DEBUG, "File sent");
            pthread_mutex_unlock(fmutex);
        }
        
    }
    // CLOSE CONNECTION
    close(conn_fd);
    syslog(LOG_DEBUG, "Closed connection from %s\n", client_ip);
    return thread_param;
}

int main(int argc, char **argv){
    int sockfd, conn_fd;

    // Set up signal() --> sigaction()
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    bool RUN_AS_DAEMON = false;
    if (argc >= 2){
        if (strcmp(argv[1],"-d") == 0){
            RUN_AS_DAEMON = true;
        }
    }

    if (RUN_AS_DAEMON){
        int rc = daemon(0,0);// wokring directory to "/", output to "/dev/null"
        if (rc != 0){
            perror("Error creating daemon");
            exit(-1);
        }
        // log as daemon
        openlog("aesdsocket", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON);
        syslog(LOG_DEBUG, "Starting server as daemon...");
    }
    else{
        // write to console if no logger, Include PID, no delay, error to console
        openlog("aesdsocket", LOG_CONS | LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_USER);
        syslog(LOG_DEBUG, "Starting server...");
    }
    
    // opening file
    #ifndef USE_AESD_CHAR_DEVICE 
    wptr = fopen(FILENAME, "w+"); // create file, overwrite if exists, with read and write permission
    #endif
    syslog (LOG_DEBUG, "Opening file: %s", FILENAME);


    // Check if the file was opened successfully
    #ifndef USE_AESD_CHAR_DEVICE
    if (wptr == NULL) {
        syslog(LOG_ERR, "Error opening %s, Error: %s", FILENAME, strerror(errno));
        closelog();
        exit(-1);
    }
    #endif

	// listen on sock_fd, new connection on conn_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_in their_addr; // connector's address info
	socklen_t sin_size;
	int yes=1;
	int rv;

	memset(&hints, 0, sizeof hints); // clear memory
	hints.ai_family = AF_INET; // IPv4
	hints.ai_socktype = SOCK_STREAM; // TCP
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s\n", gai_strerror(rv));
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
		closelog();
		exit(-1);
	}

    pthread_mutex_t fileMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t listMutex = PTHREAD_MUTEX_INITIALIZER;
    fmutex = &fileMutex;
    lmutex = &listMutex;

    SERVER_LISTENING = true;
    printf("server: waiting for connections...\n");

    #ifndef USE_AESD_CHAR_DEVICE
    pthread_t timerthread;
    rv = pthread_create(&timerthread, NULL, writeTimeEvery10sec, NULL);
    if (rv != 0){
        syslog(LOG_ERR, "pthread_create() failed creating new thread, Error: %s", strerror(errno));
        CLOSE_SERVER = true;
    }
    #endif


    while(!CLOSE_SERVER){
        sin_size = sizeof their_addr;
        conn_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (conn_fd == -1) {
            syslog(LOG_ERR, "Failed during accept(), Error: %s", strerror(errno));
            close(sockfd);
            continue;
        }
        int status;
        status = fcntl(conn_fd, F_SETFL, O_NONBLOCK);
        if (status == -1){
            syslog(LOG_ERR, "Failed during fnctl(), Error: %s", strerror(errno));
            break;
        }
    
        // Create Thread
        struct entry* n1 = (struct entry*) malloc(sizeof(struct entry));
        if (!n1){
            syslog(LOG_ERR, "malloc() failed creating new thread arguments, Error: %s", strerror(errno));
            break;
        }

        n1->conn_fd = conn_fd;
        n1->conn_closed = false;
        n1->client_addr = their_addr;

        // Update Linked List
        pthread_mutex_lock(lmutex);
        SLIST_INSERT_HEAD(&head, n1, entries);
        pthread_mutex_unlock(lmutex);      

        rv = pthread_create(&(n1->tid), NULL, handleRequests, (void*)n1);
        if (rv != 0){
            syslog(LOG_ERR, "pthread_create() failed creating new thread, Error: %s", strerror(errno));
            CLOSE_SERVER = true;
        }
    }

    syslog(LOG_DEBUG, "Caught signal, exiting");
    
    #ifndef USE_AESD_CHAR_DEVICE
    pthread_join(timerthread, NULL);
    #endif

    // Remove all completed from list
    // Write to File
    pthread_mutex_lock(lmutex);
    struct entry *np;
    SLIST_FOREACH(np, &head, entries){
        pthread_join(np->tid, NULL); // socket is close by the threadd at the end of function, so only need to join
        SLIST_REMOVE(&head, np, entry, entries);
        // free data
        free(np);
    }
    pthread_mutex_unlock(lmutex);


    // close file
    #ifndef USE_AESD_CHAR_DEVICE
    fclose(wptr);
    remove(FILENAME);
    #endif

    syslog(LOG_DEBUG, "Shutting Down Server...");
    closelog();
    exit(0);

}
