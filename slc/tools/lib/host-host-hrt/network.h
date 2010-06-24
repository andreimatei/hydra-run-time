#ifndef NETWORK_H
#define NETWORK_H

struct delegation_interface_params_t {
  int sock_sctp, sock_tcp;
};//delegation_if_arg;

typedef enum request_type {
  REQ_QUIT, 
  REQ_ALLOCATE,
  RESP_ALLOCATE,  // response for an allocation request
  REQ_CREATE,
  REQ_WRITE_ISTRUCT
}request_type;

typedef struct net_request_t {
  request_type type;
  int node_index;  // originating node
  int identifier;
  int response_identifier;
}net_request_t;

typedef struct req_allocate {
  request_type type;
  int node_index;  // originating node
  int identifier;
  int response_identifier;

  int proc_index;
  int no_tcs;
}req_allocate;

typedef struct resp_allocate {
  request_type type;
  int node_index;  // originating node
  int identifier;
  int response_identifier;

  int tcs[100];  // TODO: think about how many TC's we should support
  int no_tcs;
}resp_allocate;

typedef struct req_create {
  request_type type;
  int node_index;  // originating node
  int identifier;
  int response_identifier;

  int tcs[100];
  int no_tcs;
  struct thread_range_t ranges[100];
  thread_func func;
  tc_ident_t parent, prev, next;
  int final_ranges;  // 1 if these tcs are the last ones of the family
  i_struct* final_shareds; // pointer to the shareds in the FC (NULL if !final_ranges)
  i_struct* done;          // pointer to done in the FC, valid on the parent node (NULL if !final_ranges)
}req_create;

typedef struct req_write_istruct {
  request_type type;
  int node_index;  // originating node
  int identifier;
  int response_identifier;

  i_struct* istruct;
  long val;  // value to be written
  tc_t* reader_tc;  // the tc that's potentially blocked on the istruct (valid on the destination node)
}req_write_istruct;

extern struct delegation_interface_params_t delegation_if_arg;
extern pthread_mutex_t delegation_if_finished_mutex;
extern pthread_cond_t delegation_if_finished_cv;  // TODO: do I need to init this?
extern int delegation_if_finished;


//void* delegation_interface(void* parm);
void init_network();
void create_delegation_socket(int* port_sctp_out, int* port_tcp_out);
void sync_with_primary(int port_sctp, int port_tcp, int no_procs, int* node_index, int* tc_holes, int* no_tc_holes);
void send_quit_message_to_secondaries();
pending_request_t* request_remote_tcs(
    int node_index, int proc_index, int no_tcs);
void block_for_allocate_response(pending_request_t* req, resp_allocate* resp);
void populate_remote_tcs(
    int node_index,  // destination node
    int* tcs,  // indexes of the TC's on the destination node
    struct thread_range_t* ranges,
    int no_tcs,
    thread_func func,
    tc_ident_t parent, tc_ident_t prev, tc_ident_t next,
    int final_ranges,  // 1 if these tcs are the last ones of the family
    i_struct* final_shareds, // pointer to the shareds in the FC (NULL if !final_ranges)
    i_struct* done          // pointer to done in the FC, valid on the parent node (NULL if !final_ranges)
    );
void allocate_remote_tcs(int node_index, int proc_index, int no_tcs, int* tcs, int* no_allocated_tcs);
void write_remote_istruct(int node_index, i_struct* istructp, long val, const tc_t* reader_tc);


#endif

