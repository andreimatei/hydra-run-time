#ifndef HRT_H
#define HRT_H

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

typedef struct secondary {
  char addr[500];
  int port_daemon;
  int port_sctp, port_tcp;
  int no_procs;
  
  int socket;  // socket to daemon

  struct addrinfo* addr_sctp;  // address of the delegation interface
  int socket_sctp;  // socket to the delegation interface
} secondary;
extern secondary secondaries[1000];
extern int no_secondaries;

extern volatile int rt_init_done;
extern pthread_spinlock_t rt_init_done_lock;


#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

void LOG(LOG_LEVEL level, char* fmt, ...);

void* mmap_delegation_interface_stack(size_t* size);
void parse_own_memory_map(char* map);

#endif

