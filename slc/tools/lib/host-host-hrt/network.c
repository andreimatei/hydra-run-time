#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include "hrt.h"
#include "sl_hrt.h"
#include "network.h"


#define MAX_NO_PENDING_REQUESTS 1000
pending_request_t pending_requests[MAX_NO_PENDING_REQUESTS];
pthread_spinlock_t pending_requests_lock;




struct delegation_interface_params_t delegation_if_arg;
static int tcp_incoming_sockets[1000];
static int no_tcp_incoming_sockets = 0;

static int port_sctp;  // local port used for everything except memory requests (not the "daemon" part)
static int port_tcp;   // local port used for incoming memory requests

static pthread_t delegation_if_thread;  // identifier of the thread running sockets of the delegation interface

pthread_mutex_t delegation_if_finished_mutex;
pthread_cond_t delegation_if_finished_cv;  // TODO: do I need to init this?
int delegation_if_finished = 0;

static void terminate_delegation_interface() {
  pthread_mutex_lock(&delegation_if_finished_mutex);
  delegation_if_finished = 1;
  pthread_cond_signal(&delegation_if_finished_cv);
  pthread_mutex_unlock(&delegation_if_finished_mutex);
}

static void send_sctp_msg(int node_index, void* buf, int len);
static void handle_req_allocate(const req_allocate* req);
static void handle_resp_allocate(const resp_allocate* req);
static void handle_req_write_istruct(const req_write_istruct* req);
static void handle_req_create(const req_create* req);

/*
 * This function does not block.
 */
static void handle_sctp_request(int sock) {
  char buf[5000];
  struct sctp_sndrcvinfo sndrcvinfo;
  int flags;
  int read = sctp_recvmsg(sock, buf, sizeof(buf), NULL, 0, &sndrcvinfo, &flags);
  if (read < 0) handle_error("sctp_recvmsg");
  LOG(DEBUG, "SCTP REQUEST: got %d bytes\n", read);
  assert(read < sizeof(buf));
  //buf[read] = 0;  // NULL-terminate the string

  //assert that we got a full message
  assert(flags & MSG_EOR);

  //LOG(DEBUG, "SCTP REQUEST: \"%s\"\n", buf);
  net_request_t* req = (net_request_t*)buf;
  
  switch (req->type) {
    case REQ_QUIT:
      terminate_delegation_interface();
      break;
    case REQ_ALLOCATE:
      assert(read == sizeof(req_allocate));
      handle_req_allocate((req_allocate*)req);
      break;
    case RESP_ALLOCATE:
      assert(read == sizeof(resp_allocate));
      handle_resp_allocate((resp_allocate*)req);
      break;
    case REQ_CREATE:
      assert(read == sizeof(req_create));
      handle_req_create((req_create*)req);
      break;
    case REQ_WRITE_ISTRUCT:
      assert(read == sizeof(req_write_istruct));
      handle_req_write_istruct((req_write_istruct*)req);
      break;
    default:
      LOG(CRASH, "SCTP REQUEST: invalid request type: %d\n", req->type);
      exit(EXIT_FAILURE);
  }
}

static void handle_req_allocate(const req_allocate* req) {
  resp_allocate resp;
  resp.identifier = req->response_identifier;
  resp.type = RESP_ALLOCATE;
  resp.node_index = NODE_INDEX;
  resp.response_identifier = -1;
  resp.no_tcs = 0;

  LOG(DEBUG, "network: handle_req_allocate: got a request for %d tcs\n", req->no_tcs);
  allocate_local_tcs(req->proc_index, req->no_tcs, resp.tcs, &resp.no_tcs);

  /*
  for (int i = 0; i < req->no_tcs; ++i) {
    int tc = atomic_increment_next_tc(req->proc_index);
    if (tc != -1) {
      resp.tcs[resp.no_tcs++] = tc;
    } else {  // couldn't allocate a TC
      break;
    }
  }
  */

  LOG(DEBUG, "network: handle_req_allocate: seding allocation reply. Giving them %d tcs.\n",
      resp.no_tcs);
  send_sctp_msg(req->node_index, &resp, sizeof(resp));
}

static void handle_resp_allocate(const resp_allocate* req) {
  int index = req->identifier;
  LOG(DEBUG, "network: handle_resp_allocate: got response with id %d giving us %d tcs\n", 
      index, req->no_tcs);
  pending_request_t* pending = &pending_requests[index];

  //copy the response to the pending slot
  memcpy(pending->buf, req, sizeof(*req));
  LOG(DEBUG, "network: handle_resp_allocate: after memcpy: %d tcs\n", 
      ((resp_allocate*)pending->buf)->no_tcs);

  LOG(DEBUG, "network: handle_resp_allocate: unblocking tc %p\n", pending->blocking_tc);
  write_istruct_different_proc(&pending->istruct, 1, pending->blocking_tc);
}

/*
 * This function does not block.
 */
static int handle_new_tcp_connection(int listening_sock) {
  int conn = accept(listening_sock, NULL, NULL);
  tcp_incoming_sockets[no_tcp_incoming_sockets++] = conn;
  return conn; 
}

/*
 * This function does not block.
 */
static void handle_incoming_mem_chunk(int sock) {
  char buf[10000];
  while (1) {
    int r = read(sock, buf, 10000);
    assert(r > 0);

    //FIXME: actually handle the read data

    if (r == 10000) {  // maybe there's more data
      //poll with 0 timeout to return immediately
      fd_set set;
      FD_ZERO(&set);
      FD_SET(sock, &set);
      struct timeval tv;
      tv.tv_sec = 0; tv.tv_usec=0;
      int res = select(sock, &set, NULL, NULL, &tv);
      if (res < 0) handle_error("select");
      if (FD_ISSET(sock, &set)) {
        continue;
      } else {
        break;
      }
    } else {
      break;
    }
  }
}

/*
 * Creates a socket where this node will listen for delegation requests;
 */
void* delegation_interface(void* parm) {
  int sock_tcp = ((struct delegation_interface_params_t*)parm)->sock_tcp;
  int sock_sctp = ((struct delegation_interface_params_t*)parm)->sock_sctp;
  fd_set all_sockets;
  FD_ZERO(&all_sockets);
  FD_SET(sock_sctp, &all_sockets);
  FD_SET(sock_tcp, &all_sockets);
  
  // spin until the runtime has finished initializing, if necessary
  pthread_spin_lock(&rt_init_done_lock);
  while (!rt_init_done) {
    pthread_spin_unlock(&rt_init_done_lock);
    usleep(50000);
    pthread_spin_lock(&rt_init_done_lock);
  }
  pthread_spin_unlock(&rt_init_done_lock);
  
  while (1) {
    fd_set copy = all_sockets;
    int res = select(FD_SETSIZE, &copy, NULL, NULL, NULL);
    if (res < 0) handle_error("select");

    if (FD_ISSET(sock_sctp, &copy)) {
      handle_sctp_request(sock_sctp);
    } else if (FD_ISSET(sock_tcp, &copy)) {
      int newsock = handle_new_tcp_connection(sock_tcp);
      assert(newsock < FD_SETSIZE);
      FD_SET(newsock, &all_sockets);
    } else {
      // go through all tcp sockets
      for (int i = 0; i < no_tcp_incoming_sockets; ++i) {
        if (FD_ISSET(tcp_incoming_sockets[i], &copy)) {
          handle_incoming_mem_chunk(tcp_incoming_sockets[i]);
          break;
        }
      }

      assert(0);  // TODO: if we got here, it means that some socket received an error.. I think... treat it
    }

  }
  return NULL;

  //char buf[1000];
  //int len = 1000;
  //if (getsockname(sock, (struct sockaddr*)buf, &len) < 0) handle_error("getsockname");

  //assert(((struct sockaddr*)buf)->sa_family == AF_INET);  // test that we got an IPv4 address
  //*port = ntohs(((struct sockaddr_in*)buf)->sin_port);
  //return sock;
}


void create_delegation_socket(int* port_sctp_out, int* port_tcp_out) {
  struct addrinfo hints, *addr_sctp, *addr_tcp;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char port1[10], port2[10];
  char* s;
  if (s = getenv("SCTP_PORT")) {
    port_sctp = atoi(s);
  } else {  // TODO: if the envvar wasn't set, we choose a random port... obviously it could be in use
            // how the hell do I get the OS to give me one?
    port_sctp = (rand() % 20000) + 2000;
  }
  if (s = getenv("TCP_PORT")) {
    port_tcp = atoi(s);
  } else {  // TODO: if the envvar wasn't set, we choose a random port... obviously it could be in use
            // how the hell do I get the OS to give me one?
    port_tcp = (rand() % 20000) + 2000;
  }
  sprintf(port1, "%d", port_sctp);
  sprintf(port2, "%d", port_tcp);
  LOG(DEBUG, "starting delegation interface on ports %d, %d\n", port_sctp, port_tcp);

  int res;
  if ((res = getaddrinfo(NULL, port1, &hints, &addr_sctp)) < 0) {
    LOG(CRASH, "getaddrinfo failed: %s\n", gai_strerror(res)); exit(EXIT_FAILURE);
  }
  if ((res = getaddrinfo(NULL, port2, &hints, &addr_tcp)) < 0) {
    LOG(CRASH, "getaddrinfo failed: %s\n", gai_strerror(res)); exit(EXIT_FAILURE);
  }
  
  int sock_sctp, sock_tcp;
  sock_sctp = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
  if (sock_sctp < 0) handle_error("socket");
  sock_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock_tcp < 0) handle_error("socket");


  /* Enable receipt of SCTP Snd/Rcv Data via sctp_recvmsg */
  struct sctp_event_subscribe events;
  memset( (void *)&events, 0, sizeof(events) );
  events.sctp_data_io_event = 1;
  setsockopt( sock_sctp, SOL_SCTP, SCTP_EVENTS, (const void *)&events, sizeof(events) );
  
  if (bind(sock_sctp, addr_sctp->ai_addr, addr_sctp->ai_addrlen) < 0) handle_error("bind");
  if (bind(sock_tcp, addr_tcp->ai_addr, addr_tcp->ai_addrlen) < 0) handle_error("bind");

  if (listen(sock_sctp, 5) < 0) handle_error("listen");  // TODO: think about the backlog argument here...
  if (listen(sock_tcp, 5) < 0) handle_error("listen");  // TODO: think about the backlog argument here...
  
  // spawn a thread for the interface 
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  size_t stack_size;
  void* stack_low_addr = mmap_delegation_interface_stack(&stack_size);
  if (pthread_attr_setstack(&attr, stack_low_addr, stack_size) != 0) 
    handle_error("attr_setstack");
  delegation_if_arg.sock_sctp = sock_sctp; 
  delegation_if_arg.sock_tcp = sock_tcp;
  if (pthread_create(&delegation_if_thread, &attr, &delegation_interface, &delegation_if_arg))
    handle_error("pthread_create"); 

  *port_sctp_out = port_sctp;
  *port_tcp_out = port_tcp;
}


/*
 *
 * params:
 *  port_sctp, port_tcp - the local ports used by the delegation interface
 *  no_procs - the number of processors created on this node
 *  [OUT] node_index - the index of this node, as assigned by the primary
 *  [OUT] tc_holes - an array where the indices of the tc that can't be created will be places
 *  [OUT] no_tc_holes - number of items placed in tc_holes
 */
void sync_with_primary(
    int port_sctp, 
    int port_tcp, 
    int no_procs, 
    int* node_index,
    int* tc_holes,
    int* no_tc_holes) {
  int sock;
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) { perror("socket"); exit(EXIT_FAILURE); }
  struct addrinfo hints, *addr;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  char port[10];
  
  int daemon_port = 33333;
  char *s;
  if (s = getenv("DAEMON_PORT")) {
    daemon_port = atoi(s);
  }

  sprintf(port, "%d", daemon_port);
  if (getaddrinfo(NULL, port, &hints, &addr) < 0) handle_error("getaddrinfo");
  int on = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) handle_error("setsockopt");
  if (bind(sock, addr->ai_addr, addr->ai_addrlen) < 0) handle_error("bind");
  if (listen(sock, 5) < 0) handle_error("listen");

  LOG(INFO, "waiting for connection from primary, on port %s\n", port);
  
  int cfd = accept(sock, NULL, NULL);
  if (cfd < 0) handle_error("accept"); 
  LOG(INFO, "got connection from primary, on port %s\n", port);
 
  char map[3000];
  parse_own_memory_map(map);

  /* send own port number and mem map to primary */

  LOG(INFO, "sending own ports (%d, %d), number of processors (%d) and memory map to primary\n", 
      port_sctp, port_tcp, no_procs);
  char buf2[3010];
  sprintf(buf2, "%d;%d;%d;", port_sctp, port_tcp, no_procs);
  strcat(buf2, map);
  LOG(DEBUG, "sending data to primary: \"%s\"\n", buf2); 
  int sent = 0;
  while (sent < strlen(buf2)) {
    int res = write(cfd, buf2 + sent, strlen(buf2) - sent);
    if (res < 0) {perror("sending map"); exit(1);}
    sent += res;
  }
  
  /* get index, holes, and addresses of other nodes from primary */
  LOG(INFO, "expecting information about peers from primary\n");
  int read_bytes = 0;
  char buf[5000];
  do {
    int res = read(cfd, buf + read_bytes, 5000 - read_bytes);
    LOG(DEBUG, "got %d bytes from primary\n", res);
    if (res < 0) {perror("read from socket"); exit(1);}
    if (buf[read_bytes + res - 1] == '!') {
      buf[read_bytes + res - 1] = 0;
      break;
    }
    read_bytes += res;
  } while (1);
  LOG(INFO, "got information about virtual memory holes and peers from primary\n");
  
  LOG(DEBUG, "got data \"%s\" from primary\n", buf);
  /* parse what we got from the primary; should be <own index>;<info about holes>;addr:port1:port2;add:port:port1:port2,...; */
  char* saveptr_buf, *saveptr2;
  char* tok = strtok_r(buf, ";", &saveptr_buf);
  *node_index = atoi(tok);
  LOG(INFO, "my node index is %d\n", *node_index);
  tok = strtok_r(NULL, ";", &saveptr_buf);
  if (strcmp(tok, "-1")) {  // "-1" would signify no hole
    // we have received some holes
    LOG(DEBUG, "received info about some memory holes\n");
    char* tok2 = strtok_r(tok, ",", &saveptr2);
    while (tok2) {
      tc_holes[*no_tc_holes++] = atoi(tok2);
      tok2 = strtok_r(NULL, ",", &saveptr2);
    }
  } else {
    LOG(DEBUG, "there are no holes in my address space\n");
  }
  do {
    tok = strtok_r(NULL, ";", &saveptr_buf);
    if (tok == NULL) break;
    char* addr = strtok_r(tok, ":", &saveptr2);
    char* port1 = strtok_r(NULL, ":", &saveptr2);
    char* port2 = strtok_r(NULL, ":", &saveptr2);
    strcpy(secondaries[no_secondaries].addr, addr);
    secondaries[no_secondaries].port_sctp = atoi(port1);
    secondaries[no_secondaries].port_tcp = atoi(port2);
    secondaries[no_secondaries].socket = -1;
    secondaries[no_secondaries].socket_sctp = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
    // fill in the addr_sctp field
    if (no_secondaries != *node_index) {
      int res;
      char port[10];
      sprintf(port, "%d", secondaries[no_secondaries].port_sctp);
      if ((res = getaddrinfo(secondaries[no_secondaries].addr, port, &hints, &secondaries[no_secondaries].addr_sctp)) < 0) {
        LOG(CRASH, "getaddrinfo failed: %s\n", gai_strerror(res)); exit(EXIT_FAILURE);
      }
    }

    ++no_secondaries;
  } while (1);
  LOG(INFO, "got information about %d nodes, including myself\n", no_secondaries);
  LOG(DEBUG, "Running with nodes:\n");
  for (int i = 0; i < no_secondaries; ++i) {
    LOG(DEBUG, "%s:%d:%d\n", secondaries[i].addr, secondaries[i].port_sctp, secondaries[i].port_tcp);
  }
}

// send a quit message to all peers
void send_quit_message_to_secondaries() {
  net_request_t req;
  req.type = REQ_QUIT;

  for (int i = 1; i < no_secondaries; ++i) {  // we are at index 0
    int res = sctp_sendmsg(secondaries[i].socket_sctp, 
                       &req, 
                       sizeof(req), 
                       secondaries[i].addr_sctp->ai_addr, 
                       secondaries[i].addr_sctp->ai_addrlen,
                       1, 0, 0,0,0);
    if (res < 0) handle_error("sctp_sendmsg");
    LOG(INFO, "sent quit message to secondary %d\n", i);
  }

}

static void send_sctp_msg(int node_index, void* buf, int len) {
  assert(node_index != NODE_INDEX);  // we don't want to send to ourselves
  ((net_request_t*)buf)->node_index = NODE_INDEX;  // fill in sender
  int res = sctp_sendmsg(secondaries[node_index].socket_sctp, 
      buf, 
      len, 
      secondaries[node_index].addr_sctp->ai_addr, 
      secondaries[node_index].addr_sctp->ai_addrlen,
      1, 0, 0,0,0);
  if (res < 0) handle_error("sctp_sendmsg");
}


void init_network() {
  // init pending requests
  if (pthread_spin_init(&pending_requests_lock, PTHREAD_PROCESS_PRIVATE) != 0) handle_error("pthread_spin_init");
  for (int i = 0; i < MAX_NO_PENDING_REQUESTS; ++i) {
    if (pthread_spin_init(&pending_requests[i].istruct.lock, PTHREAD_PROCESS_PRIVATE) != 0) handle_error("pthread_spin_init");
    pending_requests[i].free = 1;
    pending_requests[i].id = i;
  }

}

/*
 * Allocate a slot from the pending_requests array. Also initializes the slot.
 * blocking_tc - [IN] - the tc that will eventually read the istruct in the allocated slot and possibly block
 *                      on it.
 */
pending_request_t* get_pending_request_slot(tc_t* blocking_tc) {
  pthread_spin_lock(&pending_requests_lock);
  int i;
  for (i = 0; i < MAX_NO_PENDING_REQUESTS; ++i) {
    if (pending_requests[i].free) {
      break;
    }
  }
  if (i < MAX_NO_PENDING_REQUESTS) {
    pending_requests[i].free = 0;
    pthread_spin_unlock(&pending_requests_lock);
    pending_requests[i].istruct.state = EMPTY;
    pending_requests[i].blocking_tc = blocking_tc;
    return &pending_requests[i];
  } else {
    assert(0); //TODO: handle this in callers
    pthread_spin_unlock(&pending_requests_lock);
    return NULL;
  }
}

void free_pending_request_slot(pending_request_t* p) {
  pthread_spin_lock(&pending_requests_lock);
  p->free = 1;
  pthread_spin_unlock(&pending_requests_lock);
}

pending_request_t* request_remote_tcs(int node_index, int proc_index, int no_tcs) {
  req_allocate req;
  req.type = REQ_ALLOCATE;
  req.node_index = NODE_INDEX;
  req.identifier = -1;
  req.proc_index = proc_index;
  pending_request_t* pending_request = get_pending_request_slot(_cur_tc);
  req.response_identifier = pending_request->id;
  req.no_tcs = no_tcs;
  send_sctp_msg(node_index, &req, sizeof(req));
  return pending_request;
}

/*
 * Blocks until a response for a particular pending request slot comes in.
 * resp - [OUT] - the response
 */
void block_for_allocate_response(pending_request_t* req, resp_allocate* resp) {
  tc_ident_t writer; 
  writer.node_index = NODE_INDEX; writer.proc_index = -1; // will be written by the network thread
  read_istruct(&req->istruct, &writer);
  LOG(DEBUG, "block_for_allocate_response: unblocking. Got %d tcs.\n", ((resp_allocate*)req->buf)->no_tcs);
  memcpy(resp, req->buf, sizeof(*resp));
  LOG(DEBUG, "block_for_allocate_response: after memcpy. Got %d tcs.\n", resp->no_tcs);
  free_pending_request_slot(req);
}

void allocate_remote_tcs(int node_index, int proc_index, int no_tcs, int* tcs, int* no_allocated_tcs) {
  pending_request_t* req = request_remote_tcs(node_index, proc_index, no_tcs);
  resp_allocate resp;
  LOG(DEBUG, "allocate_remote_tcs: blocking for reply; asked for %d tcs\n", no_tcs);
  block_for_allocate_response(req, &resp);
  LOG(DEBUG, "allocate_remote_tcs: got reply; obtained %d tcs\n", resp.no_tcs);
  assert(resp.no_tcs <= no_tcs); // we shouldn't get more tcs than we asked for
  *no_allocated_tcs = resp.no_tcs;
  for (int i = 0; i < resp.no_tcs; ++i) {
    LOG(DEBUG, "allocate_remote_tcs: got tc %d\n", resp.tcs[i]);
    tcs[i] = resp.tcs[i];
  }
}

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
    ) {
  req_create req;
  req.type = REQ_CREATE;
  req.node_index = NODE_INDEX;
  req.identifier = -1;
  req.response_identifier = -1;

  memcpy(req.tcs, tcs, no_tcs * sizeof(req.tcs[0]));
  req.no_tcs = no_tcs;
  memcpy(req.ranges, ranges, no_tcs * sizeof(req.ranges[0]));
  req.func = func;
  req.parent = parent; req.prev = prev; req.next = next;
  req.final_ranges = final_ranges;
  req.final_shareds = final_shareds;
  req.done = done;
  send_sctp_msg(node_index, &req, sizeof(req));
}

void write_remote_istruct(int node_index, i_struct* istructp, long val, const tc_t* reader) {
  req_write_istruct req;
  req.type = REQ_WRITE_ISTRUCT;
  req.identifier = -1;
  req.node_index = NODE_INDEX;
  req.response_identifier = -1;

  req.istruct = istructp;
  req.val = val;
  req.reader_tc = (tc_t*)reader;
  send_sctp_msg(node_index, &req, sizeof(req));
}

static void handle_req_write_istruct(const req_write_istruct* req) {
  LOG(DEBUG, "network: handle_req_write_istruct: got istruct write request\n");
  write_istruct_different_proc(req->istruct, req->val, req->reader_tc);
}

static void handle_req_create(const req_create* req) {
  LOG(DEBUG, "network: handle_req_create: got create request\n");
  populate_local_tcs(req->tcs,
                     req->ranges,
                     req->no_tcs,
                     req->func,
                     req->parent, req->prev, req->next,
                     req->final_ranges,
                     req->final_shareds,
                     req->done);
  LOG(DEBUG, "network: handle_req_create: finished populating local TC's\n");
}
