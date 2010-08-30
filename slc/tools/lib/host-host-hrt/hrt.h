#ifndef HRT_H
#define HRT_H

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>


#define MAX_NODES 1000  // maximum number of nodes supported

#define MIN(a,b) (((a)<(b))?(a):(b))

/*
 * Struct used for blocking until a network response targeting a specific such struct arrives.
 */
typedef struct pending_request {
  volatile i_struct istruct;
  tc_t* blocking_tc;
  int id;  // index of a pending_request within the array of pending requests
  char buf[1000];
  int buf_len;
  int free;
  struct timeval blocking_time;  // time at which the TC blocked
}pending_request_t;

typedef struct secondary {
  char addr[500];
  int port_daemon;
  int port_sctp, port_tcp;
  int no_procs;
  
  int socket;  // socket to daemon

  struct addrinfo* addr_sctp;  // address of the delegation interface
  struct addrinfo* addr_tcp;  // address of the tcp interface
  int socket_tcp;  // socket to the tcp interface
  //int socket_sctp;  // socket to the delegation interface
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
    //const int* tcs,
    const thread_range_t* ranges, 
    int no_tcs, 
    thread_func func,
    //int no_shareds, int no_globals, 
    tc_ident_t parent, tc_ident_t prev, tc_ident_t next,
    int final_ranges,  // 1 if these tcs are the last ones of the family
    i_struct* final_shareds, // pointer to the shareds in the FC (NULL if !final_ranges)
    memdesc_t* final_descs,  // pointer to the descriptor table in the FC (NULL if !final_ranges)
    i_struct* done,          // pointer to done in the FC (NULL if !final_ranges)
    default_place_policy_enum default_place_policy,// policy to be used when deciding the 
                                                   // PLACE_DEFAULT to be inheritied by
                                                   // the child
    sl_place_t default_place_parent     // PLACE_DEFAULT of the parent. Used if 
                                        // default_place_policy == INHERIT_DEFAULT_PLACE
    );

/*
 * Caps p1 by p2.
 */
void restrict_place(sl_place_t* p1, sl_place_t p2);

/*
 * Resolve PLACE_DEFAULT and PLACE_LOCAL for current context.
 * If the place passed is neither of these, return it verbatim.
 */
//sl_place_t place_2_canonical_place(sl_place_t place);

#endif

