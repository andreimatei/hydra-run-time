#ifndef NETWORK_H
#define NETWORK_H

struct delegation_interface_params_t {
  int sock_sctp, sock_tcp;
};//delegation_if_arg;

extern struct delegation_interface_params_t delegation_if_arg;
extern pthread_mutex_t delegation_if_finished_mutex;
extern pthread_cond_t delegation_if_finished_cv;  // TODO: do I need to init this?
extern int delegation_if_finished;


//void* delegation_interface(void* parm);
void create_delegation_socket(int* port_sctp_out, int* port_tcp_out);
void sync_with_primary(int port_sctp, int port_tcp, int no_procs, int* node_index, int* tc_holes, int* no_tc_holes);
void send_quit_message_to_secondaries();


#endif

