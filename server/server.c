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
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include <syslog.h>
#include <fcntl.h>
#include <stdbool.h> 

#include "server.h"
#include <pthread.h>


#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 10     // how many pending connections queue holds
#define FILENAME "/var/tmp/aesdsocketdata"
#define BUFSIZE 1024

// Global Variable
bool CLOSE_SERVER = false;

// structs with mutex
struct linkedList_thread threadList;
struct fileHandler bufferFile;

int sockfd;

void signal_handler (int signo);
void *get_in_addr(struct sockaddr *sa);
void* acceptRequests(void* thread_param);
void* handleRequests(void* thread_param);

// handler for SIGINT and SIGTERM, exiting if signal
void signal_handler (int signo)
{
    if (signo == SIGINT || signo == SIGTERM){
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

void* acceptRequests(void* thread_param){
    struct thread_acceptData* thread_func_args = (struct thread_acceptData*) thread_param;

    int sockfd = thread_func_args->sockfd;

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
        struct thread_handleData* threadData = (struct thread_handleData*) malloc(sizeof(struct thread_handleData));
        struct Node_thread* newNodeThread = (struct Node_thread*) malloc(sizeof(struct Node_thread));
        if (!threadData || !newNodeThread){
            syslog(LOG_ERR, "malloc() failed creating new thread arguments, Error: %s", strerror(errno));
            if(threadData)
                free(threadData);
            if(newNodeThread)
                free(newNodeThread);
        }
        else{
            // setup parameters so it can be freed
            threadData->conn_fd = new_conn_fd;
            threadData->connection_alive = true;
            strcpy(threadData->client_addr_str, client_str);

            newNodeThread->paramData = threadData;
            newNodeThread->next = NULL;

            rc = pthread_create(&(newNodeThread->thread), NULL, handleRequests, (void*)threadData);
            if (rc != 0){
                syslog(LOG_ERR, "pthread_create() failed creating new thread, Error: %s", strerror(errno));
                free(threadData);
                free(newNodeThread);
            }

            // Update Linked List of Threads
            rc = pthread_mutex_lock(threadList.mutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_lock() failed, Error: %s", strerror(errno));
                break;
            } 
            // Add thread to linked list
            if (threadList.head){
                newNodeThread->next = threadList.head;
            }
            threadList.head = newNodeThread;

            rc = pthread_mutex_unlock(threadList.mutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_unlock() failed, Error: %s", strerror(errno));
                break;
            } 
        }
    }
    return thread_param;
}

// handle_requests
void* handleRequests(void* thread_param){
    struct thread_handleData* thread_func_args = (struct thread_handleData*) thread_param;

    int conn_fd = thread_func_args->conn_fd;

    // initialize buffers in stack
    bool CONNECTION_ALIVE = true;
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
                CONNECTION_ALIVE = false;
            }
        }
        else if (bytes_read_socket == 0){
            // if recv returns zero, that means the connection has been closed:
            CONNECTION_ALIVE = false;     
        }
        else{
            syslog(LOG_DEBUG, "%ld bytes received from %s", bytes_read_socket, thread_func_args->client_addr_str);

            // Update Linked List of Threads
            rc = pthread_mutex_lock(bufferFile.mutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_lock() failed, Error: %s", strerror(errno));
            } 

            // write to file
            size_t nmem_written = fwrite(recv_buf, bytes_read_socket, 1, bufferFile.fptr);
            if (nmem_written == 0){
                syslog(LOG_ERR, "Error with fwrite(), tid: %ld, Error: %s", tid, strerror(errno));
            }

            for (int i = 0; i < bytes_read_socket; i++){
                if (recv_buf[i] == '\n'){
                    last_char_is_newline = true;
                    
                    // set fileptr to beginning
                    rc = fseek(bufferFile.fptr, 0, SEEK_SET);
                    if (rc == -1){
                        syslog(LOG_ERR, "Error with fseek(), tid: %ld, Error: %s", tid, strerror(errno));
                    }

                    // send file
                    size_t bytes_to_send = fread(send_buf, 1, 1, bufferFile.fptr);

                    while (bytes_to_send > 0){
                        // Blocking write
                        ssize_t bytes_sent_socket = send(conn_fd, send_buf, bytes_to_send, 0);
                        if (bytes_sent_socket != bytes_to_send){
                            syslog(LOG_ERR, "Error with send(), tid: %ld, Error: %s", tid, strerror(errno));
                        }
                        bytes_to_send = fread(send_buf, 1, 1, bufferFile.fptr);
                    }
                    
                    syslog(LOG_DEBUG, "File sent to %s\n", thread_func_args->client_addr_str);
                    
                    // reset file pointer
                    rc = fseek(bufferFile.fptr, 0, SEEK_END);
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
                rc = pthread_mutex_unlock(bufferFile.mutex);
                if (rc != 0){
                    syslog(LOG_ERR,"pthread_mutex_unlock() failed, Error: %s", strerror(errno));
                } 
            }

        }
        if (CLOSE_SERVER || !CONNECTION_ALIVE){
            thread_func_args->connection_alive = false;
            // Main Thread will call pthread_join()
            break;
        }
    }
    return thread_param; 
}


int main(int argc, char **argv){

    // set up global variables;
    threadList.head = NULL;
    // Mutexes are set up later so they don't have to be destroyed if initialization fails early

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
    
    bufferFile.fptr = fopen(FILENAME, "w+"); // create file, overwrite if exists, with read and write permission

    // Check if the file was opened successfully
    if (bufferFile.fptr == NULL) {
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

    // Set up mutex
    pthread_mutex_t fileMutex, listMutex;
    threadList.mutex = &listMutex;
    bufferFile.mutex = &fileMutex;
    
    // Setup Mutexes required for threads
    if (pthread_mutex_init(threadList.mutex, NULL) != 0) {
        syslog(LOG_ERR, "pthread_mutex_init() failed, Error: %s", strerror(errno));
        close(sockfd);
        closelog();
        exit(-1);
    }
    if (pthread_mutex_init(bufferFile.mutex, NULL) != 0) {
        syslog(LOG_ERR, "pthread_mutex_init() failed, Error: %s", strerror(errno));
        close(sockfd);
        closelog();
        pthread_mutex_destroy(threadList.mutex);
        exit(-1);
    }

    pthread_t accept_thread; 

    //Create a new child thread too handle connection
    struct thread_acceptData* threadData = (struct thread_acceptData*) malloc(sizeof(struct thread_acceptData));
    if (!threadData){
        syslog(LOG_ERR, "malloc() failed creating accept() thread, Error: %s", strerror(errno));
        close(sockfd);
        closelog();
        pthread_mutex_destroy(threadList.mutex);
        pthread_mutex_destroy(bufferFile.mutex);
        exit(-1);
    }
    threadData->sockfd = sockfd;

    rc = pthread_create(&accept_thread, NULL, acceptRequests, (void*)threadData);
    if (rc != 0){
        syslog(LOG_ERR, "pthread_create() failed creating new thread, Error: %s", strerror(errno));
        close(sockfd);
        closelog();
        pthread_mutex_destroy(threadList.mutex);
        pthread_mutex_destroy(bufferFile.mutex);
        free(threadData);
        exit(-1);
    }

	while(1) {  // main loop
        if (CLOSE_SERVER){
            syslog(LOG_DEBUG, "Caught signal, exiting");

            // closes acceptRequests() thread
            shutdown(sockfd, SHUT_RDWR);
            close(sockfd);
            free(threadData);
            pthread_join(accept_thread, NULL);
            
            // Close all connections
            rc = pthread_mutex_lock(threadList.mutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_lock() failed, Error: %s", strerror(errno));
                goto close_file;
            }
            while (threadList.head){
                // close connections
                close(threadList.head->paramData->conn_fd);
                // wait for handleRequests thread to finish
                pthread_join(threadList.head->thread, NULL);
                // free data
                free(threadList.head->paramData);
                struct Node_thread *tmpNode = threadList.head;
                threadList.head = threadList.head->next;
                free(tmpNode);
            }
            syslog(LOG_DEBUG, "Existing connections disconnected");

            rc = pthread_mutex_unlock(threadList.mutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_unlock() failed, Error: %s", strerror(errno));
                goto close_file;
            }

            // close file
            close_file:
            rc = pthread_mutex_lock(bufferFile.mutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_lock() failed, Error: %s", strerror(errno));
                goto close_server;
            }
            if (bufferFile.fptr){
                fclose(bufferFile.fptr);
            }
            rc = pthread_mutex_unlock(bufferFile.mutex);
            if (rc != 0){
                syslog(LOG_ERR,"pthread_mutex_unlock() failed, Error: %s", strerror(errno));
                goto close_server;
            }
            
            close_server:
            // Delete mutex
            pthread_mutex_destroy(threadList.mutex);
            pthread_mutex_destroy(bufferFile.mutex);
            remove(FILENAME); 
            syslog(LOG_DEBUG, "File %s destroyed", FILENAME);
            closelog();
            exit(0);   
        }


        // TODO: Join threads that completed 
        // there are active connections 
        struct Node_thread* previous = NULL;
        struct Node_thread* current = threadList.head;
        
        while (current){
            
            if (current->paramData->connection_alive){
                previous = current;
                current = current->next;
            }
            else{
                // close connections
                close(current->paramData->conn_fd);
                // wait for handleRequests thread to finish
                pthread_join(current->thread, NULL);
                syslog(LOG_DEBUG, "Close connection with fd: %d", current->paramData->conn_fd);

                // free data
                free(current->paramData);
                if (previous){
                    previous->next = current->next;
                }
                else{
                    threadList.head = current->next;
                }
                struct Node_thread *tmpNode = current;
                previous = current;
                current = current->next;
                free(tmpNode);
            }
        }


    }
	return 0;
}