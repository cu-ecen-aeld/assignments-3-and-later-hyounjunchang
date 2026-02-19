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

#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 10     // how many pending connections queue holds
#define TMPFILE "/var/tmp/aesdsocketdata"
#define BUFSIZE 2048

// Global Variable
bool END_CONNECTION = false;
bool CONNECTION_ALIVE = false;

// for SIGINT termination (since using single-thread)
FILE *fptr;
FILE *rptr;
int sockfd, conn_fd;
char s[INET6_ADDRSTRLEN];

// handler for SIGINT and SIGTERM, exiting if signal
void signal_handler (int signo)
{
    if (signo == SIGINT || signo == SIGTERM){
        if (CONNECTION_ALIVE){
            END_CONNECTION = true;
        }
        else{
            syslog(LOG_DEBUG, "Caught signal, exiting");
            if (fptr){
                fclose(fptr);
            }
            remove(TMPFILE); // deleting the file /var/tmp/aesdsocketdata.
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

int main(int argc, char **argv)
{
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
    
    fptr = fopen(TMPFILE, "w+"); // create file, overwrite if exists, with read and write permission

    // Check if the file was opened successfully
    if (fptr == NULL) {
        syslog(LOG_ERR, "Error opening /var/tmp/aesdsocketdata, Error: %s", strerror(errno));
        closelog();
        exit(-1);
    }

	// listen on sock_fd, new connection on conn_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address info
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

    printf("server: waiting for connections...\n");

	sin_size = sizeof their_addr;
	conn_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
	if (conn_fd == -1) {
		syslog(LOG_ERR, "Failed during accept(), Error: %s", strerror(errno));
		closelog();
		exit(-1);
	}
    int status;
    status = fcntl(conn_fd, F_SETFL, O_NONBLOCK);
    if (status == -1){
        syslog(LOG_ERR, "Failed during fnctl(), Error: %s", strerror(errno));
		closelog();
		exit(-1);
    }
    CONNECTION_ALIVE = true;

    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
    syslog(LOG_DEBUG, "Accepted connection from %s\n", s);
    
    char recv_buf[BUFSIZE+1];
    char send_buf[BUFSIZE+1];

	while(1) {  // main loop
        // connection operations first
        ssize_t bytes_read_socket = recv(conn_fd, recv_buf, BUFSIZE, MSG_DONTWAIT);
        if (bytes_read_socket < 0){
            if (errno != EAGAIN){
                syslog(LOG_ERR, "Errors recv(), Error: %s", strerror(errno));
                exit (-1);
            }
        }
        else if (bytes_read_socket == 0){
            // if recv returns zero, that means the connection has been closed:
            close(conn_fd);
            syslog(LOG_DEBUG, "Closed connection from %s\n", s);
            CONNECTION_ALIVE = false;

            // listen again
            if (listen(sockfd, BACKLOG) == -1) {
		        syslog(LOG_ERR, "Failed during listen(), Error: %s", strerror(errno));
		        closelog();
		        exit(-1);
            }

            printf("server: waiting for connections...\n");          
        }
        else{
            syslog(LOG_DEBUG, "%ld bytes received from %s", bytes_read_socket, s);

            // write to file
            size_t nmem_written = fwrite(recv_buf, bytes_read_socket, 1, fptr);
            if (nmem_written == 0){
                syslog(LOG_ERR, "Error with fwrite(), Error: %s", strerror(errno));
            }
            
            bool send_file = false;

            for (int i = 0; i < bytes_read_socket; i++){
                if (recv_buf[i] == '\n'){
                    send_file = true;
                }
            }
            if (send_file){
                // set fileptr
                rv = fseek(fptr, 0, SEEK_SET);
                if (rv == -1){
                    syslog(LOG_ERR, "Error with fseek(), Error: %s", strerror(errno));
                }

                // send file
                size_t bytes_to_send = fread(send_buf, 1, 1, fptr);

                /*
                if (ferror(fptr)) {
                    syslog(LOG_ERR, "Error with fseek(), Error: %s", strerror(errno));
                    END_CONNECTION = true;
                }
                // reached end of file 
                else if (feof(fptr)) {
                    bytes_to_send = bytes_read_file;
                }
                */
                while (bytes_to_send > 0){
                    // Blocking write
                    ssize_t bytes_sent_socket = send(conn_fd, send_buf, bytes_to_send, 0);
                    if (bytes_sent_socket != bytes_to_send){
                        syslog(LOG_ERR, "Error with send(), Error: %s", strerror(errno));
                    }
                    bytes_to_send = fread(send_buf, 1, 1, fptr);
                }
                
                syslog(LOG_DEBUG, "File sent");
                
                // reset file pointer
                rv = fseek(fptr, 0, SEEK_END);
                if (rv == -1){
                    syslog(LOG_ERR, "Error with fseek(), Error: %s", strerror(errno));
                }          
            }
        }
        if (!CONNECTION_ALIVE){
            sin_size = sizeof their_addr;
            conn_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
            if (conn_fd == -1) {
                syslog(LOG_ERR, "Failed during accept(), Error: %s", strerror(errno));
                closelog();
                exit(-1);
            }
            int status;
            status = fcntl(conn_fd, F_SETFL, O_NONBLOCK);
            if (status == -1){
                syslog(LOG_ERR, "Failed during fnctl(), Error: %s", strerror(errno));
                closelog();
                exit(-1);
            }
            CONNECTION_ALIVE = true;
        }
        if (END_CONNECTION){
            syslog(LOG_DEBUG, "Caught signal, exiting");
            if (fptr){
                fclose(fptr);
            }
            remove(TMPFILE); // deleting the file /var/tmp/aesdsocketdata.
            if (CONNECTION_ALIVE){           
                // closing any open sockets
                close(conn_fd);
                syslog(LOG_DEBUG, "Closed connection from %s\n", s);
            }
            closelog();
            exit(0);    
        }
    }
	return 0;
}
