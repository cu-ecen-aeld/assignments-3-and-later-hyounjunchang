#include <sys/queue.h>
// from https://man.archlinux.org/man/core/man-pages/SLIST_INIT.3.en
struct entry {
    pthread_t tid;
    int conn_fd;
    bool conn_closed;
    struct sockaddr_in client_addr;
    SLIST_ENTRY(entry) entries;             /* Singly linked list */
};