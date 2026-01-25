/*
Notes: Gemini AI overview result from Google search "linux write to file example in c" was used
as a reference to understand linux system calls
*/
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>  // For EXIT_SUCCESS/FAILURE

int main(int argc, char **argv) {
    
    // write to console if no logger, Include PID, no delay
    openlog("writer_syslog", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_USER);

    if (argc < 2){
        fprintf(stderr, "Usage: writer [writefile] [writestr]\n");
        syslog(LOG_ERR, "Not enough input parameters");
        closelog();
        return EXIT_FAILURE;
    }
    char* writefile = argv[1];
    char* writestr = argv[2];

    // Gemini AI-generated result was used as a reference, but not used
    // Using system calls, open() allows us to specify permissiosn
    int fd = open(writefile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0){
        fprintf(stderr, "Error opening/creating file\n");
        syslog(LOG_ERR, "Error opening/creating file");
        closelog();
        return EXIT_FAILURE;
    }

    // ignore null-byte
    ssize_t bytes_written;
    bytes_written = write(fd, writestr, strlen(writestr));

    if (bytes_written < 0){
        fprintf(stderr, "Error writing to file\n");
        syslog(LOG_ERR, "Error writing to file");
        closelog();
        return EXIT_FAILURE;
    }
    else{
        syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    }

    int res = close(fd);
    // Not a fatal error
    if (res < 0){
        syslog(LOG_WARNING, "Error closing file");
    }
    
    closelog();
    return EXIT_SUCCESS;
}