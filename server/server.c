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

// Global Variable
bool END_CONNECTION = false;

// handler for SIGINT and SIGTERM, exiting if signal
void signal_handler (int signo)
{
    if (signo == SIGINT || signo == SIGTERM){
        END_CONNECTION = true;
    }
    return;
}

void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
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
            return -1;
        }
        // log as daemon
        openlog("server_syslog", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON);
        syslog(LOG_DEBUG, "Starting server as daemon...");
    }
    else{
        // write to console if no logger, Include PID, no delay, error to console
        openlog("server_syslog", LOG_CONS | LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_USER);
        syslog(LOG_DEBUG, "Starting server...");
    }

    // File write example from Google Serach
    FILE *fptr;

    // create file, overwrite if exists
    fptr = fopen("/var/tmp/aesdsocketdata", "w");

    // Check if the file was opened successfully
    if (fptr == NULL) {
        syslog(LOG_ERR, "Error opening /var/tmp/aesdsocketdata");
        closelog();
        return -1;
    }

	// listen on sock_fd, new connection on new_fd
	int sockfd, new_fd;
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address info
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	memset(&hints, 0, sizeof hints); // clear memory
	hints.ai_family = AF_INET; // IPv4
	hints.ai_socktype = SOCK_STREAM; // TCP
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo: %s\n", gai_strerror(rv));
        closelog();
		return -1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			continue;
		}
        // set socket options
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			exit(1);
		}
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			continue;
		}
        // bind successful
		break;
    }
	
	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
        syslog(LOG_ERR, "Failed to bind()");
		closelog();
		return -1;
	}
	if (listen(sockfd, BACKLOG) == -1) {
		syslog(LOG_ERR, "Failed during listen()");
		closelog();
		return -1;
	}

    // reap all dead processes
	sa.sa_handler = sigchld_handler; 
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		syslog(LOG_ERR, "Failed during sigaction()");
		return -1;
	}

	printf("server: waiting for connections...\n");

	while(1) {  // main accept() loop

        // Gracefully exits when SIGINT or SIGTERM is received
        if (END_CONNECTION){
            syslog(LOG_DEBUG, "Caught SIGINT or SIGTERM, exiting");
            // completing any open connection operations
            // closing any open sockets
            // deleting the file /var/tmp/aesdsocketdata.
            fclose(fptr);
            exit (0);
        }

		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
				&sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			if (send(new_fd, "Hello, world!", 13, 0) == -1)
				perror("send");
			close(new_fd);
			exit(0);
		}
		close(new_fd);  // parent doesn't need this
	}



	return 0;
}
