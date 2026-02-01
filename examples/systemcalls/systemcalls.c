#include "systemcalls.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
// following the man page https://man7.org/linux/man-pages/man3/system.3.html
    int status = system(cmd);

    if (cmd == NULL){
        return false;
    }
    // error occurred in system() call
    if (status == -1){
        return false;
    }

    // normal termination
    if (WIFEXITED (status)){
        return WEXITSTATUS(status) == 0; // return true if exit status is 0
    }

    return false;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    // From Linux System Programming page 161
    int status;
    pid_t pid;

    pid = fork();
    // fork failed
    if (pid == -1){
        va_end(args);
        return false;
    }
    else if (pid == 0) {
        execv(command[0], command);
        exit(-1); // exec() functions only return if an error has occurred
    }

    // error in child process
    if (waitpid (pid, &status, 0) == -1){
        va_end(args);
        return false;
    }
    
    // execv returned exit status
    if (WIFEXITED (status)){
        /* for printf messages for debuging
        printf("Command executed: \n");
        for (int i = 0; i < count; i++){
            printf("arg %d: %s\n", i, command[i]);
        }
        printf("exit status was %d\n", WEXITSTATUS(status));
        */
        va_end(args);
        return WEXITSTATUS(status) == 0; // return true if exit status is 0
    }

    va_end(args);
    return false;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

    // Create file
    int fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1){
        va_end(args);
        return false;
    }


    // From Linux System Programming page 161
    int status;
    pid_t pid;

    pid = fork();
    // fork failed
    if (pid == -1){
        va_end(args);
        return false;
    }
    else if (pid == 0) {
        // redirect strdout to file
        int redirect_status = dup2(fd, 1);
        if (redirect_status == -1){ // error in dup2
            exit(-1);
        }
        close(fd);

        execv(command[0], command);
        exit(-1); // exec() functions only return if an error has occurred
    }

    // error in child process
    if (waitpid (pid, &status, 0) == -1){
        va_end(args);
        return false;
    }

    // execv returned exit status
    if (WIFEXITED (status)){
        va_end(args);
        return WEXITSTATUS(status) == 0; // return true if exit status is 0
    }

    va_end(args);
    return false;
}
