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
//void allocate_local_tcs(int proc_index, int no_tcs, int* tcs, int* no_allocated_tcs);
void allocate_local_tcs(int proc_index, tc_ident_t parent, unsigned int no_tcs, 
    //int* tcs, 
    unsigned int* no_allocated_tcs,
    tc_ident_t* first_tc,
    tc_ident_t* last_tc);
/*
 *  Prepares every TC in a chain to run parts of a family. The TC's are also scheduled to run.
 */
void populate_local_tcs(
    thread_func func,
    unsigned int no_tcs,                      // number of TC's in the chain
    bool is_last_proc_on_fam,
    unsigned long no_generations,
    unsigned long no_threads_per_generation,  // total number of threads to be run on these TCs as part 
                                              // of one generation
    unsigned long no_threads_per_generation_last,  // ditto above for the last generation, which is special
                                                   // because it can have more threads than the rest
    long gap_between_generations,
    long start_index,         // index of the first thread from the first range run by the first TC on this proc
    long start_index_last_generation,
    long denormalized_fam_start_index,  // the real start index of the family. Used to de-normalize start_index
    long step,

    tc_ident_t first_tc,  //the first TC assigned to this family on this proc (the head of the chain)
    tc_ident_t parent,
    tc_ident_t prev,  // the TC that is going to run the ranges just before the first TC in this chain
    tc_ident_t next,  // the TC that is going to run the ranges just after the first TC in this chain
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

