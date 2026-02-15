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

// Global Variable
bool END_CONNECTION = false;
bool CONNECTION_ALIVE = false;

// for SIGINT termination (since using single-thread)
FILE *fptr;

// handler for SIGINT and SIGTERM, exiting if signal
void signal_handler (int signo)
{
    if (signo == SIGINT || signo == SIGTERM){
        // Graceful termination
        if (CONNECTION_ALIVE){
            END_CONNECTION = true;
        }
        else{
            if (fptr){
                fclose(fptr);
            }
            remove(TMPFILE);
            closelog();
            exit(0);            
        }
    }
    return;
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
    
    // create file, overwrite if exists
    fptr = fopen(TMPFILE, "w");

    // Check if the file was opened successfully
    if (fptr == NULL) {
        syslog(LOG_ERR, "Error opening /var/tmp/aesdsocketdata");
        closelog();
        exit(-1);
    }

	// listen on sock_fd, new connection on new_fd
	int sockfd, new_fd;
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
            syslog(LOG_ERR, "setsockopt() failed");
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
        syslog(LOG_ERR, "Failed to bind()");
		closelog();
		exit(-1);
	}
	if (listen(sockfd, BACKLOG) == -1) {
		syslog(LOG_ERR, "Failed during listen()");
		closelog();
		exit(-1);
	}

    printf("server: waiting for connections...\n");

	sin_size = sizeof their_addr;
	new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
	if (new_fd == -1) {
		syslog(LOG_ERR, "Failed during accept()");
		closelog();
		exit(-1);
	}
    int status;
    status = fcntl(new_fd, F_SETFL, O_NONBLOCK);
    if (status == -1){
        syslog(LOG_ERR, "Failed during fnctl()");
		closelog();
		exit(-1);
    }
    CONNECTION_ALIVE = true;

	while(1) {  // main accept() loop

        // completing any open connection operations first


        // Gracefully exits when SIGINT or SIGTERM is received
        if (END_CONNECTION){
            syslog(LOG_DEBUG, "Caught SIGINT or SIGTERM, exiting");
            // closing any open sockets
            close(new_fd);
            // deleting the file /var/tmp/aesdsocketdata.
            fclose(fptr);
            remove(TMPFILE);
            exit(0);
        }
	}



	return 0;
}
