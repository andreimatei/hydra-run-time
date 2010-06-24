#ifndef HRT_H
#define HRT_H

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

typedef struct pending_request {
  volatile i_struct istruct;
  tc_t* blocking_tc;
  int id;
  char buf[1000];
  int buf_len;
  int free;
}pending_request_t;

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

extern int NODE_INDEX;

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

void LOG(LOG_LEVEL level, char* fmt, ...);

void* mmap_delegation_interface_stack(size_t* size);
void parse_own_memory_map(char* map);
//int atomic_increment_next_tc(int proc_id);
void allocate_local_tcs(int proc_index, int no_tcs, int* tcs, int* no_allocated_tcs);
void populate_local_tcs(
    const int* tcs, 
    const struct thread_range_t* ranges, 
    int no_tcs, 
    thread_func func,
    //int no_shareds, int no_globals, 
    tc_ident_t parent, tc_ident_t prev, tc_ident_t next,
    int final_ranges,  // 1 if these tcs are the last ones of the family
    i_struct* final_shareds, // pointer to the shareds in the FC (NULL if !final_ranges)
    i_struct* done          // pointer to done in the FC (NULL if !final_ranges)
    );

#endif

