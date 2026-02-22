#include <sys/queue.h>
// from https://man.archlinux.org/man/core/man-pages/SLIST_INIT.3.en
struct entry {
    pthread_t thread;
    int conn_fd;
    bool connection_closed;
    char client_addr_str[INET6_ADDRSTRLEN];
    SLIST_ENTRY(entry) entries;             /* Singly linked list */
};