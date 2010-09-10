#ifndef NETWORK_H
#define NETWORK_H

#include <sys/time.h>

struct delegation_interface_params_t {
  int sock_sctp, sock_tcp;
};//delegation_if_arg;

typedef enum request_type {
  REQ_QUIT, 
  REQ_ALLOCATE,
  RESP_ALLOCATE,  // response for an allocation request
  REQ_CREATE,
  REQ_WRITE_ISTRUCT,
  REQ_WRITE_ISTRUCT_MEM,  // writes an istructure that corresponds to a mem stub. The request also
                          // contains the first range and the number of ranges of the descriptor
  REQ_CONFIRMATION,
  REQ_PULL_DATA,
  REQ_PULL_DATA_DESCRIBED,
  REQ_PULL_DESC,  // request to pull a descriptor
  RESP_PULL_DESC,
  REQ_WRITE_GLOBAL_TO_CHAIN,  // request to write a global to a chain of TCs from the child family
  REQ_PING,
  RESP_PING
}request_type;

typedef struct net_request_t {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;
  int response_identifier;
}net_request_t;

typedef struct {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;
  int response_identifier;
  struct timeval send_time;
  int request_unblock;
  i_struct* istructp;
  tc_t* reading_tc;
}req_ping;

typedef struct {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;
  int response_identifier;
  struct timeval ping_send_time;
  struct timeval pong_send_time;
}resp_ping;


typedef struct req_allocate {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;
  int response_identifier;

  int proc_index;
  int no_tcs;
}req_allocate;

typedef struct resp_allocate {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;
  int response_identifier;

  unsigned int no_tcs;
  tc_ident_t first_tc, last_tc;
}resp_allocate;

typedef struct req_create {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;
  int response_identifier;

  thread_func func;
  unsigned int no_tcs;
  bool is_first_proc_on_fam, is_last_proc_on_fam;
  unsigned long no_generations, no_threads_per_generation, no_threads_per_generation_last;
  unsigned long gap_between_generations;
  unsigned long start_index, start_index_last_generation;  // normalized indexes; index of the first thread from the first range run by the first TC on this proc
  long denormalized_fam_start_index;  // the real start index of the family. Used to de-normalize start_index
  long step;

  tc_ident_t first_tc;  //the first TC assigned to this family on this proc (the head of the chain)
  tc_ident_t parent;
  tc_ident_t prev;  // the TC that is going to run the ranges just before the first TC in this chain
  tc_ident_t next;  // the TC that is going to run the ranges just after the first TC in this chain
  i_struct* final_shareds; // pointer to the shareds in the FC (NULL if !final_ranges)
  memdesc_t* final_descs;  // pointer to the descriptor table in the FC 
  i_struct* done;          // pointer to done in the FC, valid on the parent node
  default_place_policy_enum default_place_policy;  // policy to be used when deciding then PLACE_DEAFULT
                                                   // of the new threads
  sl_place_t default_place_parent;    // PLACE_DEFAULT of the parent
}req_create;

typedef struct req_write_istruct {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;
  int response_identifier;

  i_struct* istruct;
  long val;  // value to be written
  tc_t* reader_tc;  // the tc that's potentially blocked on the istruct (valid on the destination node)
}req_write_istruct;

typedef struct req_write_istruct_mem {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;
  int response_identifier;

  i_struct* istruct;
  memdesc_stub_t val;  // value to be written
  
  int desc_set;        // 1 if the .desc field is valid, 0 if .first_range and .no_ranges are valid
  mem_range_t first_range;
  int no_ranges;
  memdesc_t desc;  // the descriptor associated with the stub;
  
  tc_t* reader_tc;  // the tc that's potentially blocked on the istruct (valid on the destination node)
}req_write_istruct_mem;

typedef struct req_confirmation {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;  // index of the pending request slot to unblock
  int response_identifier;
}req_confirmation;

typedef struct {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;  // index of the pending request slot to unblock
  int response_identifier;

  memdesc_t* desc_pointer;  // the address of the descriptor that needs to be pulled
  memdesc_t* destination;   // where the descriptor is to be put on the requesting node
}req_pull_desc;

typedef struct {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;  
  int response_identifier;  // index of the pending request slot to unblock

  memdesc_t desc;
  memdesc_t* destination;   // where the descriptor is to be put on the requesting node
}resp_pull_desc;


/*
 * Request to pull data from a descriptor on this node
 */
typedef struct {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;  // index of the pending request slot that needs to be written when the data is received
  int response_identifier;
 
  memdesc_t* desc; 
}req_pull_data;

/*
 * Request to pull data from a descriptor which is specified in the request (actually, a single range for now).
 */
typedef struct {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;  // index of the pending request slot that needs to be written when the data is received
  int response_identifier;
 
  //mem_range_t range; 
  memdesc_t desc;  // descriptor of the data that needs to be pulled
}req_pull_data_described;

/*
 * Request to write a global to a chain of TC's from a child family
 */
typedef struct {
  request_type type;
  unsigned int node_index;  // originating node
  int identifier;
  int response_identifier;

  tc_ident_t first_tc;  // first TC in the chain
  unsigned int index;   // index of the global
  long val;             // value to be written
  bool is_mem;          // true if the value is a mem_stub
}req_write_global_to_chain;

extern struct delegation_interface_params_t delegation_if_arg;
extern pthread_mutex_t delegation_if_finished_mutex;
extern pthread_cond_t delegation_if_finished_cv;  // TODO: do I need to init this?
extern int delegation_if_finished;


//void* delegation_interface(void* parm);
void send_ping(unsigned int node_index, int identifier, int request_unblock, i_struct* istructp);
void init_network();
void create_delegation_socket(int* port_sctp_out, int* port_tcp_out);
void sync_with_primary(int port_sctp, int port_tcp, int no_procs, 
                       unsigned int* node_index, unsigned int* tc_holes, unsigned int* no_tc_holes);
void send_quit_message_to_secondaries();
pending_request_t* request_remote_tcs(
    int node_index, int proc_index, int no_tcs);
void block_for_allocate_response(pending_request_t* req, resp_allocate* resp);
void write_global_to_remote_chain(unsigned int node_index, tc_ident_t first_tc, unsigned int index,
                                  long val, bool is_mem);
void populate_remote_tcs(
    unsigned int node_index,
    thread_func func,
    unsigned int no_tcs,                      // number of TC's in the chain
    bool is_first_proc_on_fam,
    bool is_last_proc_on_fam,
    unsigned long no_generations,
    unsigned long no_threads_per_generation,  // total number of threads to be run on these TCs as part 
                                              // of one generation
    unsigned long no_threads_per_generation_last,  // ditto above for the last generation, which is special
                                                   // because it can have more threads than the rest
    long gap_between_generations,
    unsigned long start_index,                 // (normalized) index of the first thread from the first range run by the first TC on this proc
    unsigned long start_index_last_generation, // (normalized) index of the first thread from the _last_ range run by the first TC on this proc
    long denormalized_fam_start_index,  // the real (denormalized) start index of the family. Used to de-normalize start_index
    long step,

    tc_ident_t first_tc,  //the first TC assigned to this family on this proc (the head of the chain)
    tc_ident_t parent,
    tc_ident_t prev,  // the TC that is going to run the ranges just before the first TC in this chain
    tc_ident_t next,  // the TC that is going to run the ranges just after the first TC in this chain
    i_struct* final_shareds, // pointer to the shareds in the FC (NULL if !final_ranges)
    memdesc_t* final_descs,  // pointer to the descriptor table in the FC
    i_struct* done,          // pointer to done in the FC
    default_place_policy_enum default_place_policy,// policy to be used when deciding the 
                                                   // PLACE_DEFAULT to be inheritied by
                                                   // the child
    sl_place_t default_place_parent     // PLACE_DEFAULT of the parent. Used if 
                                        // default_place_policy == INHERIT_DEFAULT_PLACE
    );
void allocate_remote_tcs(unsigned int node_index, unsigned int proc_index, unsigned int no_tcs, 
                         unsigned int* no_allocated_tcs, tc_ident_t* first_tc, tc_ident_t* last_tc);
void write_remote_istruct(unsigned int node_index, i_struct* istructp, long val, const tc_t* reader_tc);
void write_remote_istruct_mem(unsigned int node_index, 
                              i_struct* istructp, 
                              memdesc_stub_t val, 
                              memdesc_t* desc,
                              int copy_desc,
                              const tc_t* reader);
pending_request_t* get_pending_request_slot(tc_t* blocking_tc);
void send_sctp_msg(unsigned int node_index, void* buf, int len);
void enqueue_push_request(unsigned int node_index, 
                          int pending_req_index, 
                          int remote_confirm_needed, 
                          const mem_range_t* ranges, 
                          int no_ranges);

/*
 * Block the current TC until the istruct in req has been written to.
 */
void block_for_confirmation(pending_request_t* req);


#endif

