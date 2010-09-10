#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include "sl_hrt.h"
#include "hrt.h"
#include "mem-comm.h"
#include "network.h"


#define MAX_NO_PENDING_REQUESTS 1000
secondary secondaries[MAX_NODES];
unsigned int no_secondaries = 0;

pending_request_t pending_requests[MAX_NO_PENDING_REQUESTS];
pthread_spinlock_t pending_requests_lock;


struct delegation_interface_params_t delegation_if_arg;
static int tcp_incoming_sockets[MAX_NODES];
// number of sockets in tcp_incoming_sockets array 
static int no_tcp_incoming_sockets = 0;  // TODO: add some locking for this
static int new_sending_socket_pipe_fd[2];  // the 2 end of a pipe use to wake up the delegation interface from
                        // it's select() syscall when a new tcp socket potentially needs to be added to the
                        // collection of sockets to be polled for sending


/*
 * struct representing the state of reading incoming memory ranges
 */
typedef struct {
 memdesc_t desc;  // descriptor for the memory that we're currently receiving
 int node_index;  // index of the node that is sending us data; useful if we need to send a confirmation
 int cur_range;   // index of the range within the descriptor desc that we're currently receiving
 int offset_within_range;
 int pending_req_index;  // the index of a pending request slot to write to when all the data
                         // is received. This can be a local slot or a remote slot.
                         // -1 if no operation is needed.
 int remote_confirm;   // 1 if the pending_req_index refers to a remote slot; 0 if it refers to a local slot
}tcp_incoming_state_t;

static tcp_incoming_state_t incoming_state[MAX_NODES];

/*
 * struct representing a request to push some memory to a remote node
 */
typedef struct{
  mem_range_t ranges[100];
  int no_ranges;
  int pending_req_index;    // this index will be embedded in the data stream that
                            // we pushes. When the remote node receives this stream and reads it all,
                            // it will write to the pending request slot to unblock either itself or this node.
  int remote_confirm_needed;  // specifies wether pending_req_index refers to a slot on the remote node or
                              // on the local node.
  struct timeval create_time;  // time when this request was created (enqueued)
} push_request_t;

/*
 * struct describing the state of a memory send operation.
 */
typedef struct {
  int active;  // 0 if there is no sending state for this remote node
  push_request_t req;  // the memory that we're currently pushing
  int header_sent;  // 1 if the header (describing the ranges, etc.) has been fully sent. 0 otherwise.
  int cur_range;    // index of the range that is currently in the process of being sent
  int bytes_sent;   // bytes already sent from cur_range
  struct timeval start_time;  // time when this state has been created
}tcp_sending_state_t;

static tcp_sending_state_t outgoing_state[MAX_NODES];

#define MAX_PUSH_REQUESTS_PER_NODE 100

/*
 * queues of memory push requests for each remote node
 */
static push_request_t push_requests[MAX_NODES][MAX_PUSH_REQUESTS_PER_NODE];
static int no_push_requests[MAX_NODES];
static pthread_spinlock_t push_requests_locks[MAX_NODES];

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

static void handle_req_allocate(const req_allocate* req);
static void handle_resp_allocate(const resp_allocate* req);
static void handle_req_write_istruct(const req_write_istruct* req);
static void handle_req_write_istruct_mem(const req_write_istruct_mem* req);
static void handle_req_create(const req_create* req);
static void handle_req_confirmation(const req_confirmation* req);
static void handle_req_pull_data(const req_pull_data* req);
static void handle_req_pull_data_described(const req_pull_data_described* req);
static void handle_req_ping(const req_ping* req);
static void handle_resp_ping(const resp_ping* req);
static void handle_req_pull_desc(const req_pull_desc* req);
static void handle_resp_pull_desc(const resp_pull_desc* req);
static void handle_req_write_global_to_chain(const req_write_global_to_chain* req);

static int push_queue_not_empty(unsigned int node_index);
static int dequeue_push_request(unsigned int node_index, push_request_t* req);


/*
 * This function does not block.
 */
static void handle_sctp_request(int sock) {
  char buf[5000];
  struct sctp_sndrcvinfo sndrcvinfo;
  int flags;
  int read = sctp_recvmsg(sock, buf, sizeof(buf), NULL, 0, &sndrcvinfo, &flags);
  if (read < 0) handle_error("sctp_recvmsg");
  LOG(DEBUG, "network: handle_sctp_request: got a SCTP request of %d bytes\n", read);
  assert(read < (int)sizeof(buf));
  //buf[read] = 0;  // NULL-terminate the string

  //assert that we got a full message
  assert(flags & MSG_EOR);

  //LOG(DEBUG, "SCTP REQUEST: \"%s\"\n", buf);
  net_request_t* req = (net_request_t*)buf;
  
  switch (req->type) {
    case REQ_QUIT:
      LOG(DEBUG, "network: handle_sctp_request: got REQ_QUIT\n");
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
    case REQ_WRITE_ISTRUCT_MEM:
      assert(read == sizeof(req_write_istruct_mem));
      handle_req_write_istruct_mem((req_write_istruct_mem*)req);
      break;
    case REQ_CONFIRMATION:
      assert(read == sizeof(req_confirmation));
      handle_req_confirmation((req_confirmation*)req);
      break;
    case REQ_PULL_DATA:
      assert(read == sizeof(req_pull_data));
      LOG(DEBUG, "network: handle_sctp_request: got REQ_PULL_DATA\n");
      handle_req_pull_data((req_pull_data*)req);
      break;
    case REQ_PULL_DATA_DESCRIBED:
      LOG(DEBUG, "network: handle_sctp_request: got REQ_PULL_DATA_DESCRIBED\n");
      assert(read == sizeof(req_pull_data_described));
      handle_req_pull_data_described((req_pull_data_described*)req);
      break;
    case REQ_PULL_DESC:
      LOG(DEBUG, "network: handle_sctp_request: got REQ_PULL_DESC\n");
      assert(read == sizeof(req_pull_desc));
      handle_req_pull_desc((const req_pull_desc*)req);
      break;
    case RESP_PULL_DESC:
      LOG(DEBUG, "network: handle_sctp_request: got RESP_PULL_DESC\n");
      assert(read == sizeof(resp_pull_desc));
      handle_resp_pull_desc((const resp_pull_desc*)req);
      break;
    case REQ_WRITE_GLOBAL_TO_CHAIN: 
      LOG(DEBUG, "network: handle_sctp_request: got REQ_WRITE_GLOBAL_TO_CHAIN\n");
      assert(read == sizeof(req_write_global_to_chain));
      handle_req_write_global_to_chain((const req_write_global_to_chain*)req);
    case REQ_PING:
      LOG(DEBUG, "network: handle_sctp_request: got REQ_PING\n");
      assert(read == sizeof(req_ping));
      handle_req_ping((const req_ping*)req);
      break;
    case RESP_PING:
      LOG(DEBUG, "network: handle_sctp_request: got RESP_PING\n");
      assert(read == sizeof(resp_ping));
      handle_resp_ping((const resp_ping*)req);
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

  LOG(DEBUG, "network: handle_req_allocate: got a request for %d TC's\n", req->no_tcs);
  allocate_local_tcs(req->proc_index, req->no_tcs, &resp.no_tcs, &resp.first_tc, &resp.last_tc);
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

static void handle_req_confirmation(const req_confirmation* req) {
  int index = req->identifier;
  struct timeval t; gettimeofday(&t, NULL);
  LOG(DEBUG, "network: handle_req_confirmation: got response with index %d. Time is %ld:%ld\n", 
      index, t.tv_sec, t.tv_usec/1000);
  pending_request_t* pending = &pending_requests[index];

  LOG(DEBUG, "network: handle_resp_allocate: unblocking tc %p\n", pending->blocking_tc);
  write_istruct_different_proc(&pending->istruct, 1, pending->blocking_tc);
}

/*
 * This function does not block.
 */
static int handle_new_tcp_connection(int listening_sock) {
  int conn = accept(listening_sock, NULL, NULL);
  incoming_state[no_tcp_incoming_sockets].cur_range = -1;
  tcp_incoming_sockets[no_tcp_incoming_sockets] = conn;
  no_tcp_incoming_sockets++;
  return conn; 
}

/*
 * Called when we get a memory stream.
 * Reads a header from buf and initializes incoming_state[incoming_index].
 */
char* parse_memchunk_header(char* buf, int len __attribute__((__unused__)), int incoming_index) {
  // TODO: handle the case where the header is split and the read() call only returns a part

  assert(incoming_state[incoming_index].cur_range == -1); // assert we weren't in the middle of receiving another object

  memdesc_t res;
  char* tmp;
  char* tok = strtok_r(buf, ";", &tmp);  // node_index
  assert(tok);
  incoming_state[incoming_index].node_index = atoi(tok);
  tok = strtok_r(NULL, ";", &tmp);  // no_ranges
  assert(tok);
  res.no_ranges = atoi(tok);
  tok = strtok_r(NULL, ";", &tmp);  // pending_req_index
  assert(tok);
  incoming_state[incoming_index].pending_req_index = atoi(tok);
  tok = strtok_r(NULL, ";", &tmp);  // is the pending_req_index referring to a remote slot (or 0 for a local slot)?
  assert(tok);
  incoming_state[incoming_index].remote_confirm = atoi(tok);
  for (int i = 0; i < res.no_ranges; ++i) {
    tok = strtok_r(NULL, ";", &tmp);  // pointer
    assert(tok);
    res.ranges[i].p = (void*)atol(tok);
    tok = strtok_r(NULL, ";", &tmp);  // no_elements
    assert(tok);
    res.ranges[i].no_elements = atoi(tok);
    tok = strtok_r(NULL, ";", &tmp);  // sizeof_elemement
    assert(tok);
    res.ranges[i].sizeof_element = atoi(tok);
  }
  incoming_state[incoming_index].desc = res;
  incoming_state[incoming_index].cur_range = 0;
  incoming_state[incoming_index].offset_within_range = 0;

  return tmp;
}
    
/*
 * Read from a buffer and copies to local memory.
 * Returns 1 if we got all that was required by the incoming_state[invoming_index]; 0 if more data is needed.
 * len - [IN] - size of buf
 * bytes_used - [OUT] - counter for the number of bytes consumed from buf
 */
static int parse_incoming_memchunk(int incoming_index, char* buf, int len, int* bytes_used) {
  LOG(DEBUG, "network: parse_incoming_memchunk: incoming_index = %d, len = %d\n", incoming_index, len);
  *bytes_used = 0;
  memdesc_t* desc = &incoming_state[incoming_index].desc;
  int bytes_needed = 0;
  int i;

  do {
    i = incoming_state[incoming_index].cur_range;
    // compute how much data is still to be received for this range
    bytes_needed = desc->ranges[i].no_elements * desc->ranges[i].sizeof_element - 
                   incoming_state[incoming_index].offset_within_range;
    void* mem_dest = desc->ranges[i].p + incoming_state[incoming_index].offset_within_range;
    LOG(DEBUG, "network: parse_incoming_mem_chunk: we need %d bytes for range %d\n", bytes_needed, i);
    int read = MIN(bytes_needed, len);
    *bytes_used += read;
    memcpy(mem_dest, buf, read);
    LOG(DEBUG, "network: parse_incoming_mem_chunk: copied %d bytes at %p\n", read, mem_dest);
    bytes_needed -= read;
    len -= read;
    buf += read;
    incoming_state[incoming_index].offset_within_range += read;
    if (bytes_needed == 0) {
      incoming_state[incoming_index].cur_range++;
      incoming_state[incoming_index].offset_within_range = 0;
    }
  } while (len > 0 && incoming_state[incoming_index].cur_range < desc->no_ranges);

  if (incoming_state[incoming_index].cur_range == desc->no_ranges) {
    return 1;
  } else {
    return 0;
  }

  /*
  // if we filled up
  if (incoming_state[incoming_index].cur_range == desc->no_ranges) {
    LOG(DEBUG, "network: parse_incoming_mem_chunk: finished receiving memory for incoming_state slot %d\n",
        incoming_index);
    assert(len == 0);
  }

  for (i = incoming_state[incoming_index].cur_range; i < desc->no_ranges; ++i) {
    if (i > incoming_state[incoming_index].cur_range) {
      mem_dest = desc->ranges[incoming_state[incoming_index].cur_range].p;
      incoming_state[incoming_index].offset_within_range = 0;
    }
    // compute how much data is still to be received for this range
    bytes_needed = desc->ranges[i].no_elements * desc->ranges[i].sizeof_element - 
                   incoming_state[incoming_index].offset_within_range;
    int read = MIN(bytes_needed, len);
    *bytes_used += read;
    memcpy(mem_dest, buf, read);
    LOG(DEBUG, "network: parse_incoming_mem_chunk: copied %d bytes at %p\n", read, mem_dest);
    bytes_needed -= read;
    len -= read;
    if (len == 0) {
      LOG(DEBUG, "network: parse_incoming_mem_chunk: exhausted all data in incoming buffer\n");
      break;
    }
    incoming_state[incoming_index].offset_within_range += read;
  }
  incoming_state[incoming_index].cur_range = bytes_needed ? i : i + 1;
  if (incoming_state[incoming_index].cur_range == desc->no_ranges) {
    assert(len == 0);
    LOG(DEBUG, "network: parse_incoming_mem_chunk: finished receiving memory for incoming_state slot %d\n",
        incoming_index);
    return 1;
  } else {
    return 0;
  }
  */
}

/*
 * This function does not block but it does assume that the header is small 
 * and comes all in one piece.
 */
static void handle_incoming_mem_chunk(int incoming_index) {
  //static int ping_id;
  // TODO: don't assume that the header comes all in one piece
  char buf[10000];
  char* tmp = buf;
  int sock = tcp_incoming_sockets[incoming_index];
  while (1) {  // loop until this we can't read from this socket any more

    LOG(DEBUG, "network: handle_incoming_mem_chunk: reading from socket.\n");
    int r = read(sock, buf, 10000);
    assert(r > 0);

    while (tmp < (buf + r)) {  // loop until all the data was consumed
      LOG(DEBUG, "network: handle_incoming_mem_chunk: we have some data to parse (%d bytes).\n",
          (buf + r) - tmp);
      // if this is the first time we're receiving data for this incoming_state slot, read the header
      if (incoming_state[incoming_index].cur_range == -1) {
        LOG(DEBUG, "network: handle_incoming_mem_chunk: parsing a header since incoming_state is not initialized.\n");
        // this will initialize incoming_state[incoming_index]
        char* old_tmp = tmp;
        tmp = parse_memchunk_header(tmp, r - (tmp - buf), incoming_index);
        LOG(DEBUG, "network: handle_incoming_mem_chunk: done parsing header. It consumed %d bytes.\n",
            tmp - old_tmp);
      }

      if (!incoming_state[incoming_index].remote_confirm) {  // if it was this node who requested the data
        int slot_index;
        if ((slot_index = incoming_state[incoming_index].pending_req_index) != -1) {
          struct timeval t; gettimeofday(&t, NULL);
          pending_request_t* pending = &pending_requests[slot_index];
          LOG(DEBUG, "network: handle_incoming_mem_chunk: it seems we have received some memory data," \
              "The TC pulling the data has been blocked for %ld ms.\n", 
              timediff(t,pending->blocking_tc->blocking_time));
        }
      }

      // init the range that we're currently receiving, so that the SIGSEGV handler maps it for us if needed
      memdesc_t* desc = &incoming_state[incoming_index].desc;
      int range_no = incoming_state[incoming_index].cur_range;
      cur_incoming_mem_range_start = desc->ranges[range_no].p;
      cur_incoming_mem_range_len = desc->ranges[range_no].no_elements * desc->ranges[range_no].sizeof_element; 

      int bytes_used = 0;
      LOG(DEBUG, "network: handle_incoming_mem_chunk: parsing a memchunk\n");
      LOG(DEBUG, "network: handle_incoming_mem_chunk: r = %d, buf = %p, tmp = %p\n", r, buf, tmp);

      int finished = parse_incoming_memchunk(incoming_index, tmp, r - (tmp - buf), &bytes_used);
      LOG(DEBUG, "network: handle_incoming_mem_chunk: done parsing a memchunk. finished = %d\n", finished);
      tmp += bytes_used;
      if (finished) {  // we've got all the data
        LOG(DEBUG, "network: handle_incoming_mem_chunk: finished receiving data for a memdesc\n");
        
        // assert that we've used up all the data read from the socket
        // // FIXME: the current code seems to not handle the case where a single remote node quickly seends different
        // // memory chunks (with different headers). If the read() in this function grabs more than one header, we
        // // ignore the second one (hence the next assert). Fix this somehow.
        // //assert(bytes_used == r - (tmp - buf));
        
        // clear the state
        cur_incoming_mem_range_start = NULL;
        cur_incoming_mem_range_len = 0;
        incoming_state[incoming_index].cur_range = -1;
        int slot_index;
        if ((slot_index = incoming_state[incoming_index].pending_req_index) != -1) {
          if (incoming_state[incoming_index].remote_confirm) {
            // send a confirmation
            req_confirmation req;
            req.type = REQ_CONFIRMATION;
            req.node_index = NODE_INDEX;
            req.identifier = incoming_state[incoming_index].pending_req_index;
            req.response_identifier = -1;
            LOG(DEBUG, "network: handle_incoming_mem_chunk: sending remote confirmation for received data\n");
            int dest_node = incoming_state[incoming_index].node_index;
            send_sctp_msg(dest_node, &req, sizeof(req));

          } else {
            // the pending slot was local; write to it
            LOG(DEBUG, "network: handle_incoming_mem_chunk: sending local confirmation for received data\n");
            pending_request_t* pending = &pending_requests[slot_index];
            write_istruct_different_proc(&pending->istruct, 1, pending->blocking_tc);
          }
        }  
      }
    }  // loop until all the data that was read from the socket is consumed 
    LOG(DEBUG, "network: handle_incoming_mem_chunk: all the data that was read so far from the socket was consumed\n");

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
        LOG(DEBUG, "network: handle_incoming_mem_chunk: socket has more data for us. looping.\n");
        continue;
      } else {
        LOG(DEBUG, "network: handle_incoming_mem_chunk: socket doesn't have more data for us. quitting.\n");
        break;
      }
    } else {
      LOG(DEBUG, "network: handle_incoming_mem_chunk: not testing socket since we didn't fill our buffer before. quitting.\n");
      break;
    }
  }  // loop until we can't read from the socket any more
}

void open_tcp_conn(int node_index) {
  assert(secondaries[node_index].socket_tcp == -1);
  secondaries[node_index].socket_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  struct addrinfo hints, *addr;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  int res;
  char port[10];
  sprintf(port, "%d", secondaries[node_index].port_tcp);
  if ((res = getaddrinfo(secondaries[node_index].addr, 
          port, 
          &hints, 
          &addr)) < 0) {
    LOG(CRASH, "getaddrinfo failed: %s\n", gai_strerror(res)); exit(EXIT_FAILURE);
  }

  if (connect(secondaries[node_index].socket_tcp,
              addr->ai_addr,
              addr->ai_addrlen) < 0) handle_error("connect tcp");
  //// set the socket to non-blocking mode
  // O_NONBLOCK or O_NDELAY ?
  //if (fcntl(secondaries[node_index].socket_tcp, F_SETFL, O_NDELAY) < 0) handle_error("setting non-blocking");
}

fd_set get_sending_sockets() {
  fd_set res;
  FD_ZERO(&res);
  for (unsigned int i = 0; i < no_secondaries; ++i) {
    if (outgoing_state[i].active || push_queue_not_empty(i)) {
      LOG(DEBUG, "network: get_sending_sockets: found socket that wants to send data for secondary %d "
          "because of condition %d\n", i, outgoing_state[i].active ? 0 : 1);
      if (secondaries[i].socket_tcp == -1) {
        // open a connection
        LOG(DEBUG, "network: get_sending_sockets: opening TCP connection to secondary %d\n", i);
        open_tcp_conn(i);
        LOG(DEBUG, "network: get_sending_sockets: done opening TCP connection to secondary %d\n", i);
      }
      if (!outgoing_state[i].active) {
        //dequeue a send request
        int got_req = dequeue_push_request(i, &outgoing_state[i].req);
        assert(got_req);
        gettimeofday(&outgoing_state[i].start_time, NULL);
        LOG(DEBUG, "network: get_sending_sockets: dequeued push request. It stayed in queue for %ld ms.\n",
            timediff(outgoing_state[i].start_time, outgoing_state[i].req.create_time));
        outgoing_state[i].active = 1;
        outgoing_state[i].header_sent = 0;
        outgoing_state[i].cur_range = 0;
        outgoing_state[i].bytes_sent = 0;
      }

      assert(secondaries[i].socket_tcp < FD_SETSIZE);
      FD_SET(secondaries[i].socket_tcp, &res);
    }
  }
  return res;
}

/*
 * Build the header for a push request.
 */
void build_push_header(const tcp_sending_state_t* s, char* buf, int buf_size, int* len) {
  // the header looks like <local node index>;<number of ranges>;<pending_req_index>;<remote_confirm>;
  // (<pointer>;<no elements>;<sizeof element>;)*
  
  assert(s->active);
  *len = sprintf(buf, "%d;%d;%d;%d;", 
                 NODE_INDEX, 
                 s->req.no_ranges, 
                 s->req.pending_req_index, 
                 s->req.remote_confirm_needed);
  for (int i = 0; i < s->req.no_ranges; ++i) {
    const mem_range_t* r = &s->req.ranges[i];
    *len += sprintf(buf + *len, "%ld;%d;%d;",
                    (long)r->p,
                    r->no_elements,
                    r->sizeof_element);
  }
  assert(*len < buf_size);
}

static void push_data(int node_index) {
  // TODO: as implemented now, we send the header in blocking mode, but with MSG_MORE set,
  // and then we continue in non blocking mode... test if this does the right thing (i.e. don't block if we
  // send a GB).
  assert(outgoing_state[node_index].active);
  LOG(DEBUG, "network: push_data: entering push_data with node_index = %d\n", node_index);
  tcp_sending_state_t* s = &outgoing_state[node_index];
  int sock = secondaries[node_index].socket_tcp;
  int res;
  
  assert(s->cur_range < s->req.no_ranges);
  if (!s->header_sent) {
    // send the header in a blocking fashion
    LOG(DEBUG, "network: push_data: sending header\n");
    char c[1000];
    int len;
    build_push_header(s, c, 1000, &len);
    res = send(sock, c, len, MSG_MORE);
    assert(res == len);
    s->header_sent = 1;
    s->cur_range = 0;
  }
  // send data in non-blocking mode
  LOG(DEBUG, "network: push_data: sending data. cur_range = %d total ranges = %d\n", s->cur_range, s->req.no_ranges);
  mem_range_t r = s->req.ranges[s->cur_range];
  int tot_send = r.no_elements * r.sizeof_element;
  int to_send = tot_send - s->bytes_sent;
  assert((to_send > 0) || (r.no_elements == 0));
  int flags = MSG_DONTWAIT;
  if (s->cur_range < s->req.no_ranges - 1)  // if there are more ranges to come
    flags |= MSG_MORE;  // TODO: verify that select still returns this socket so we can continue sending
                        // or, loop in this function until send returns 0
  res = send(sock, r.p + s->bytes_sent, to_send, flags);
  if (res == -1) {
    assert(errno == EWOULDBLOCK || errno == EAGAIN);
    res = 0;
    LOG(WARNING, "network: push_data: send returned -1 and errno was EWOULDBLOCK or EAGAIN\n");
  }
  if (res == to_send) {
    LOG(DEBUG, "network: push_data: finished sending a range\n");
    s->cur_range++;
    s->bytes_sent = 0;
    if (s->cur_range == s->req.no_ranges) {  // we just finished sending the last part of the last range
      s->active = 0;
      struct timeval t;
      gettimeofday(&t, NULL);
      LOG(DEBUG, "network: push_data: finished sending memory. The sending state was active for %ld ms. (%ld ms since request for push was enqueued)\n",
          timediff(t, s->start_time), timediff(t, s->req.create_time));
    }
  } else {
    s->bytes_sent += res;
    // TODO: check if we can send more now; use MSG_MORE 
  }
}

static int delegation_sock_sctp;

/*
 * Creates a socket where this node will listen for delegation requests;
 */
void* delegation_interface(void* parm) {
  _cur_tc = NULL;  // so that context identification based on _cur_tc works.
  int sock_tcp = ((struct delegation_interface_params_t*)parm)->sock_tcp;
  int sock_sctp = ((struct delegation_interface_params_t*)parm)->sock_sctp;
  delegation_sock_sctp = sock_sctp;
  fd_set all_sockets;
  FD_ZERO(&all_sockets);
  assert(sock_sctp < FD_SETSIZE);
  FD_SET(sock_sctp, &all_sockets);
  assert(sock_tcp < FD_SETSIZE);
  FD_SET(sock_tcp, &all_sockets);
  assert(new_sending_socket_pipe_fd[0] < FD_SETSIZE);
  FD_SET(new_sending_socket_pipe_fd[0], &all_sockets);   
  int old_tcp_incoming_sockets = 0;  // used to check if new sockets are added to the array over time
  for (int i = 0; i < no_tcp_incoming_sockets; ++i) {
    FD_SET(tcp_incoming_sockets[i], &all_sockets);
    old_tcp_incoming_sockets++;
  }
  
  // spin until the runtime has finished initializing, if necessary
  pthread_spin_lock(&rt_init_done_lock);
  while (!rt_init_done) {
    pthread_spin_unlock(&rt_init_done_lock);
    usleep(50000);
    pthread_spin_lock(&rt_init_done_lock);
  }
  pthread_spin_unlock(&rt_init_done_lock);
  
  //int print_msg = 1;
  while (1) {
    fd_set copy, sending;
    int got_work = 0;
    while(!got_work) {   // loop while we get messages to rebuild the writing sockets collection
      LOG(DEBUG, "network: delegation interface: starting building sockets collection\n");
      // add new tcp incoming sockets to the collection, if any
      if (no_tcp_incoming_sockets != old_tcp_incoming_sockets) {
        // if we have new incoming sockets, add them to the collection
        assert(no_tcp_incoming_sockets > old_tcp_incoming_sockets);
        // TODO: add some locking for no_tcp_incoming_sockets
        for (int i = old_tcp_incoming_sockets; i < no_tcp_incoming_sockets; ++i) {
          assert(tcp_incoming_sockets[i] < FD_SETSIZE);
          FD_SET(tcp_incoming_sockets[i], &all_sockets);
          old_tcp_incoming_sockets++;
        }
      }
      copy = all_sockets;
      // build collection of sockets that need to send data
      sending = get_sending_sockets();
      assert(FD_ISSET(new_sending_socket_pipe_fd[0], &copy));
      LOG(DEBUG, "network: delegation_interface: entering select.\n");
      int res = select(FD_SETSIZE, &copy, &sending, NULL, NULL);//&time);
      LOG(DEBUG, "network: delegation_interface: out of select!\n");
      if (res <= 0) handle_error("select");
      if (!FD_ISSET(new_sending_socket_pipe_fd[0], &copy)) { // if we got anything but the pipe, we need to treat it
        LOG(DEBUG, "network: delegation interface: got out of select. It's not the pipe.\n");
        got_work = 1;
      } else {
        LOG(DEBUG, "network: delegation interface: we have something on the pipe; need to loop\n");
        char c;//buf[100];
        int res = read(new_sending_socket_pipe_fd[0], &c, 1);
        // TODO: do a fcnt or whatever to set the pipe in non-blocking mode, and read as much as possible
        // (in case 2 TC's did enqueue_push_request in the meantime).
        assert(res == 1);
      }
    }
    LOG(DEBUG, "network: delegation interface: got out of select; we actually have work to do\n");

    if (FD_ISSET(sock_sctp, &copy)) {
      handle_sctp_request(sock_sctp);
    } else if (FD_ISSET(sock_tcp, &copy)) {
      int newsock = handle_new_tcp_connection(sock_tcp);
      assert(newsock < FD_SETSIZE);
      FD_SET(newsock, &all_sockets);
    } else {
      int found = 0;
      // go through all tcp sockets
      for (int i = 0; i < no_tcp_incoming_sockets; ++i) {
        if (FD_ISSET(tcp_incoming_sockets[i], &copy)) {
          handle_incoming_mem_chunk(i);
          found = 1;
          break;
        }
      }
      if (found) continue;
      // go through sending sockets
      for (unsigned int i =0; i < no_secondaries; ++i) {
        if (secondaries[i].socket_tcp == -1) continue;
        if (FD_ISSET(secondaries[i].socket_tcp, &sending)) {
          // can send some more data on this socket
          found = 1;
          push_data(i);
        }
      }

      if (found) continue;
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
  hints.ai_flags = AI_PASSIVE;
  char port1[10], port2[10];
  char* s;
  if ((s = getenv("SCTP_PORT"))) {
    port_sctp = atoi(s);
  } else {  // TODO: if the envvar wasn't set, we choose a random port... obviously it could be in use
            // how the hell do I get the OS to give me one?
    port_sctp = (rand() % 20000) + 2000;
  }
  if ((s = getenv("TCP_PORT"))) {
    port_tcp = atoi(s);
  } else {  // TODO: if the envvar wasn't set, we choose a random port... obviously it could be in use
            // how the hell do I get the OS to give me one?
    port_tcp = (rand() % 20000) + 2000;
  }
  sprintf(port1, "%d", port_sctp);
  sprintf(port2, "%d", port_tcp);
  LOG(DEBUG, "starting delegation interface on ports %d, %d\n", port_sctp, port_tcp);

  int res;
  hints.ai_socktype = SOCK_STREAM;
  if ((res = getaddrinfo(NULL, port2, &hints, &addr_tcp)) < 0) {
    LOG(CRASH, "getaddrinfo failed: %s\n", gai_strerror(res)); exit(EXIT_FAILURE);
  }
  hints.ai_socktype = SOCK_SEQPACKET;
  hints.ai_protocol = IPPROTO_SCTP;
  if ((res = getaddrinfo(NULL, port1, &hints, &addr_sctp)) < 0) {
    LOG(CRASH, "getaddrinfo failed: %s\n", gai_strerror(res)); exit(EXIT_FAILURE);
  }
  
  int sock_sctp, sock_tcp;
  sock_sctp = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
  if (sock_sctp < 0) handle_error("socket");
  sock_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock_tcp < 0) handle_error("socket");


  /* Enable receipt of SCTP Snd/Rcv Data via sctp_recvmsg */
  //struct sctp_event_subscribe events;
  //memset( (void *)&events, 0, sizeof(events) );
  //events.sctp_data_io_event = 1;
  //setsockopt( sock_sctp, SOL_SCTP, SCTP_EVENTS, (const void *)&events, sizeof(events) );
  int on = 1;
  if (setsockopt(sock_sctp, IPPROTO_SCTP, SCTP_NODELAY, &on, sizeof(on)) < 0) handle_error("setsockopt");
  
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
    unsigned int* node_index,
    unsigned int* tc_holes,
    unsigned int* no_tc_holes) {
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
  if ((s = getenv("DAEMON_PORT"))) {
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
  LOG(DEBUG, "preparing mem map to send to primary: \"%s\"\n", map); 
  LOG(DEBUG, "sending data to primary: \"%s\"\n", buf2); 
  unsigned int sent = 0;
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
    //secondaries[no_secondaries].socket_sctp = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
    secondaries[no_secondaries].socket_tcp = -1;
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
  for (unsigned int i = 0; i < no_secondaries; ++i) {
    LOG(DEBUG, "%s:%d:%d\n", secondaries[i].addr, secondaries[i].port_sctp, secondaries[i].port_tcp);
  }
}

// send a quit message to all peers
void send_quit_message_to_secondaries() {
  net_request_t req;
  req.type = REQ_QUIT;

  for (unsigned int i = 1; i < no_secondaries; ++i) {  // we are at index 0
    int res = //sctp_sendmsg(secondaries[i].socket_sctp, 
              sctp_sendmsg(delegation_sock_sctp, 
                       &req, 
                       sizeof(req), 
                       secondaries[i].addr_sctp->ai_addr, 
                       secondaries[i].addr_sctp->ai_addrlen,
                       1, 
                       SCTP_UNORDERED,  // flags
                       0,0,0);
    if (res < 0) handle_error("sctp_sendmsg");
    LOG(INFO, "sent quit message to secondary %d\n", i);
  }

}

void send_sctp_msg(unsigned int node_index, void* buf, int len) {
  assert(node_index != NODE_INDEX);  // we don't want to send to ourselves
  ((net_request_t*)buf)->node_index = NODE_INDEX;  // fill in sender
  int res = //sctp_sendmsg(secondaries[node_index].socket_sctp, 
            sctp_sendmsg(delegation_sock_sctp,
      buf, 
      len, 
      secondaries[node_index].addr_sctp->ai_addr, 
      secondaries[node_index].addr_sctp->ai_addrlen,
      1, 
      0, //SCTP_UNORDERED, // flags
      0,0,0);
  // TODO: the unordered flag was removed for now, because the current implementation of sync
  // seems to need for the write to the "done" istruct to be done after the writes of all the shareds.
  // Check if this is true, and add a param to this function specifying if the message has to be ordered.
  // Or, add another version of the function.


  if (res < 0) handle_error("sctp_sendmsg");
}

void send_ping(unsigned int node_index, int identifier, int request_unblock, i_struct* istructp) {
  req_ping req;
  req.type = REQ_PING;
  req.node_index = NODE_INDEX;
  req.identifier = identifier;
  req.response_identifier = identifier;
  req.request_unblock = request_unblock;
  req.istructp = istructp;
  req.reading_tc = _cur_tc;
  gettimeofday(&req.send_time, NULL);
  send_sctp_msg(node_index, &req, sizeof(req));
  LOG(DEBUG, "network: send_ping: sent ping %d\n", identifier);
}

void init_network() {
  // init pending requests
  if (pthread_spin_init(&pending_requests_lock, PTHREAD_PROCESS_PRIVATE) != 0) handle_error("pthread_spin_init");
  for (int i = 0; i < MAX_NO_PENDING_REQUESTS; ++i) {
    if (pthread_spin_init(&pending_requests[i].istruct.lock, PTHREAD_PROCESS_PRIVATE) != 0) handle_error("pthread_spin_init");
    pending_requests[i].free = 1;
    pending_requests[i].id = i;
  }
  // init push_requests_locks
  for (int i = 0; i < MAX_NODES; ++i) {
    if (pthread_spin_init(&push_requests_locks[i], PTHREAD_PROCESS_PRIVATE) != 0) handle_error("pthread_spin_init");
  }
  // init new_sending_socket_pipe_fd
  if (pipe(new_sending_socket_pipe_fd) < 0) handle_error("pipe");
  if (fcntl(new_sending_socket_pipe_fd[1], F_SETFL, O_NONBLOCK) == -1)
    handle_error("fcntl");
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
 */
void block_for_confirmation(pending_request_t* req) {
  tc_ident_t writer; 
  writer.node_index = NODE_INDEX; writer.proc_index = -1; // will be written by the network thread
  read_istruct(&req->istruct, &writer);
  LOG(DEBUG, "network: block_for_confirmation: unblocking.\n");
  free_pending_request_slot(req);
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

void allocate_remote_tcs(unsigned int node_index, 
                         unsigned int proc_index, 
                         unsigned int no_tcs,
                         unsigned int* no_allocated_tcs,
                         tc_ident_t* first_tc,
                         tc_ident_t* last_tc) {
  pending_request_t* req = request_remote_tcs(node_index, proc_index, no_tcs);
  resp_allocate resp;
  LOG(DEBUG, "allocate_remote_tcs: blocking for reply; asked for %d TC's from node %d\n", no_tcs, node_index);
  block_for_allocate_response(req, &resp);
  LOG(DEBUG, "allocate_remote_tcs: got reply; obtained %d TC's from \n", resp.no_tcs, node_index);
  assert(resp.no_tcs <= no_tcs); // we shouldn't get more tcs than we asked for
  *no_allocated_tcs = resp.no_tcs;
  *first_tc         = resp.first_tc;
  *last_tc          = resp.last_tc; 
  /*
  for (int i = 0; i < resp.no_tcs; ++i) {
    LOG(DEBUG, "allocate_remote_tcs: got tc %d\n", resp.tcs[i]);
    tcs[i] = resp.tcs[i];
  }
  */
}

void write_global_to_remote_chain(unsigned int node_index, tc_ident_t first_tc, unsigned int index,
                                  long val, bool is_mem) {
  req_write_global_to_chain req;
  req.type = REQ_WRITE_GLOBAL_TO_CHAIN;
  req.node_index = NODE_INDEX;
  req.identifier = -1;
  req.response_identifier = -1;
  
  req.first_tc = first_tc;
  req.index = index;
  req.val = val;
  req.is_mem = is_mem;

  send_sctp_msg(node_index, &req, sizeof(req));
}

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
    long denormalized_fam_start_index,  // the real start index of the family. Used to de-normalize start_index
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
    /*
    unsigned int node_index,  // destination node
    thread_range_t* ranges,
    int no_ranges,
    thread_func func,
    tc_ident_t parent, tc_ident_t prev, tc_ident_t next,
    int final_ranges,  // 1 if these tcs are the last ones of the family
    i_struct* final_shareds, // pointer to the shareds in the FC (NULL if !final_ranges)
    memdesc_t* final_descs,  // pointer to the descriptor table in the FC (NULL if !final_ranges)
    i_struct* done,          // pointer to done in the FC, valid on the parent node (NULL if !final_ranges)
    default_place_policy_enum default_place_policy,// policy to be used when deciding the 
                                                   // PLACE_DEFAULT to be inheritied by
                                                   // the child
    sl_place_t default_place_parent     // PLACE_DEFAULT of the parent. Used if 
                                        // default_place_policy == INHERIT_DEFAULT_PLACE
    */
    ) {
  req_create req;
  req.type = REQ_CREATE;
  req.node_index = NODE_INDEX;
  req.identifier = -1;
  req.response_identifier = -1;

  req.func                 = func;
  req.no_tcs               = no_tcs;
  req.is_first_proc_on_fam = is_first_proc_on_fam;
  req.is_last_proc_on_fam  = is_last_proc_on_fam;
  req.no_generations       = no_generations;
  req.no_threads_per_generation = no_threads_per_generation;
  req.no_threads_per_generation_last = no_threads_per_generation_last;
  req.gap_between_generations = gap_between_generations;
  req.start_index          = start_index; 
  req.start_index_last_generation = start_index_last_generation;
  req.denormalized_fam_start_index = denormalized_fam_start_index;
  req.step                 = step;
  req.first_tc             = first_tc;
  req.parent               = parent;
  req.prev                 = prev;
  req.next                 = next;
  req.final_shareds        = final_shareds;
  req.final_descs          = final_descs;
  req.done                 = done;
  req.default_place_policy = default_place_policy;
  req.default_place_parent = default_place_parent;
  
  send_sctp_msg(node_index, &req, sizeof(req));
}

void write_remote_istruct(unsigned int node_index, 
                          i_struct* istructp, 
                          long val, 
                          const tc_t* reader) {
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

/*
 * Sends a request to write a stub to a remote istruct. The request also carries the first range and 
 * the number of ranges of the descriptor, which will be written to the descriptor on the remote node.
 * The values for the first range and the number of ranges are read from a descriptor which might be
 * different than the descriptor pointed to by the stub. This is because the pointer of the stub might
 * have been changed before calling this function to point to a location valid only on the destination 
 * node. This happens because, before copying them to the next thread, pointers in stubs are set to
 * point to locations in the next TC or in the FC. See write_argmem in tc.c
 */
void write_remote_istruct_mem(unsigned int node_index, 
                              i_struct* istructp, 
                              memdesc_stub_t val, 
                              memdesc_t* desc,
                              int copy_desc,
                              const tc_t* reader) {
  req_write_istruct_mem req;
  req.type = REQ_WRITE_ISTRUCT_MEM;
  req.identifier = -1;
  req.node_index = NODE_INDEX;
  req.response_identifier = -1;

  req.istruct = istructp;
  req.val = val;
  req.desc_set = copy_desc;
  if (copy_desc) {
    req.desc = *desc;
  } else {
    req.first_range = desc->ranges[0]; //get_stub_pointer(val)->ranges[0];
    req.no_ranges = desc->no_ranges; //get_stub_pointer(val)->no_ranges;
  }
  req.reader_tc = (tc_t*)reader;
  send_sctp_msg(node_index, &req, sizeof(req));
}

static void handle_req_write_istruct(const req_write_istruct* req) {
  LOG(DEBUG, "network: handle_req_write_istruct: got istruct write request\n");
  write_istruct_different_proc(req->istruct, req->val, req->reader_tc);
}

static void handle_req_write_istruct_mem(const req_write_istruct_mem* req) {
  LOG(DEBUG, "network: handle_req_write_istruct_mem: got istruct write request\n");

  // copy the first range and no_ranges to the descriptor; prepare for a possible segfault
  memdesc_t* desc = get_stub_pointer(req->val);
  cur_incoming_mem_range_start = desc;
  cur_incoming_mem_range_len = (unsigned long)desc + sizeof(*desc); 
  // initialize the local descriptor, or at least the first range and no_ranges, depending on what
  // the request provides
  if (req->desc_set) {
    *desc = req->desc;
    assert(req->val.node == NODE_INDEX);  // the stub should indicate that the descriptor is to be
                                          // found on this node
  } else {
    desc->no_ranges = req->no_ranges;
    desc->ranges[0] = req->first_range;
  }

  // write the i-structure
  write_istruct_different_proc(req->istruct, _stub_2_long(req->val), req->reader_tc);
}

static void handle_req_write_global_to_chain(const req_write_global_to_chain* req) {
  LOG(DEBUG, "network: handle_req_write_global_to_chain: got a write request for chain starting with tc %d\n", req->first_tc.tc_index);
  write_global_to_chain_of_tcs(req->first_tc.tc, req->index, req->val, req->is_mem);
}

static void handle_req_create(const req_create* req) {
  LOG(DEBUG, "network: handle_req_create: got create request\n");
  // sanity check
  assert(req->default_place_parent.node_index == -1 || req->default_place_parent.node_index == (signed)NODE_INDEX);
  populate_local_tcs(req->func,
                     req->no_tcs,
                     req->is_first_proc_on_fam,
                     req->is_last_proc_on_fam,
                     req->no_generations,
                     req->no_threads_per_generation,
                     req->no_threads_per_generation_last,
                     req->gap_between_generations,
                     req->start_index,
                     req->start_index_last_generation,
                     req->denormalized_fam_start_index,
                     req->step,
                     req->first_tc,
                     req->parent, req->prev, req->next,
                     req->final_shareds,
                     req->final_descs,
                     req->done,
                     req->default_place_policy,
                     req->default_place_parent
                     );
  LOG(DEBUG, "network: handle_req_create: finished populating local TC's\n");
}

/*
 * Enqueues a requerst to push some memory ranges to a remote node.
 * node_index - [IN] - the node to push to
 * pending_req_index - [IN] - index of pending request slot (local or remote) to be written to when all
 *                            the memory is made consistent
 * remote_confirm_needed - [IN] - 1 if pending_req_index refers to a remote slot, 0 if it refers to a local slot
 * ranges, no_ranges - [IN] - array of ranges that need to be pushed; the contents are copied to an
 *                            internal data structure
 */
void enqueue_push_request(unsigned int node_index, 
                          int pending_req_index, 
                          int remote_confirm_needed, 
                          const mem_range_t* ranges, 
                          int no_ranges) {
  assert(node_index < no_secondaries);
 
  pthread_spin_lock(&push_requests_locks[node_index]); 

  int slot = no_push_requests[node_index];
  push_requests[node_index][slot].no_ranges = no_ranges;
  push_requests[node_index][slot].pending_req_index = pending_req_index;
  push_requests[node_index][slot].remote_confirm_needed = remote_confirm_needed;
  gettimeofday(&push_requests[node_index][slot].create_time, NULL);
  for (int i = 0; i < no_ranges; ++i) {
    push_requests[node_index][slot].ranges[i] = ranges[i];
  }
  ++no_push_requests[node_index];

  pthread_spin_unlock(&push_requests_locks[node_index]); 
  LOG(DEBUG, "network: enqueue_push_request: enqueued push request. Informing delegation interface.\n");
  LOG(DEBUG, "network: enqueue_push_request: writing to pipe fd %d\n", new_sending_socket_pipe_fd[1]);
  write(new_sending_socket_pipe_fd[1], "1", 1);
}

/*
 * Returns 0 if queue is empty, 1 otherwise
 */
static int push_queue_not_empty(unsigned int node_index) {
  int res;
  assert(node_index < no_secondaries);
  pthread_spin_lock(&push_requests_locks[node_index]); 
  res = (no_push_requests[node_index] > 0);
  pthread_spin_unlock(&push_requests_locks[node_index]); 
  return res;
}

/*
 * Returns 0 if queue is empty, 1 otherwise
 * req - [OUT] - the dequeued request
 */
static int dequeue_push_request(unsigned int node_index, push_request_t* req) {
  int res;
  assert(node_index < no_secondaries);
 
  pthread_spin_lock(&push_requests_locks[node_index]); 
  int no_reqs = no_push_requests[node_index];
  if (no_reqs) res = 1;
  else res = 0;
  if (no_reqs) {
    *req = push_requests[node_index][no_reqs-1];
    --no_push_requests[node_index];
  }
  pthread_spin_unlock(&push_requests_locks[node_index]); 
  return res;
}

/*
 * Handles a request made by a node that wants to pull some data. The data requested is described by a local
 * descriptor.
 * Pushes the requested data.
 */
static void handle_req_pull_data(const req_pull_data* req) {
  memdesc_t* desc = req->desc;
  mem_range_t ranges[desc->no_ranges];
  LOG(DEBUG, "network: handle_req_pull_data: received a request to pull from a desc with %d ranges\n",
      desc->no_ranges);
  for (int i = 0; i < desc->no_ranges; ++i) {
    ranges[i] = desc->ranges[i];
  }
  enqueue_push_request(req->node_index,  // destination node
                       req->identifier,  // pending request slot to be written on the remote node
                       0,                // no remote confirmation needed 
                       ranges,           // memory to push
                       desc->no_ranges);
}

/*
 * Handles a request made by a node that wants to pull some data. The data requested is described by a 
 * mem_range_t in the request.
 * Pushes the requested data.
 */
static void handle_req_pull_data_described(const req_pull_data_described* req) {
  enqueue_push_request(req->node_index,  // destination node
                       req->identifier,  // pending request slot to be written on the remote node
                       0,                // no remote confirmation needed 
                       req->desc.ranges,
                       req->desc.no_ranges
                       //&req->range,      // memory to push
                       //1                 // only one range to push 
                       );
}

static void handle_req_pull_desc(const req_pull_desc* req) {
  resp_pull_desc resp;
  resp.type = RESP_PULL_DESC;  
  resp.node_index = NODE_INDEX;
  resp.response_identifier = req->identifier;  // this is the index of a pending request that will be unblocked

  resp.desc = *req->desc_pointer;
  resp.destination = req->destination;
  send_sctp_msg(req->node_index, &resp, sizeof(resp));
}

static void handle_resp_pull_desc(const resp_pull_desc* req) {
  // copy the descriptor
  *req->destination = req->desc;
  // unblock the pending request
  pending_request_t* pending = &pending_requests[req->response_identifier];
  LOG(DEBUG, "network: handle_resp_pull_desc: unblocking tc %p\n", pending->blocking_tc);
  write_istruct_different_proc(&pending->istruct, 1, pending->blocking_tc);
}

static void handle_req_ping(const req_ping* req) {
  if (!req->request_unblock) {
    resp_ping resp;
    resp.identifier = req->response_identifier;
    resp.type = RESP_PING;
    resp.node_index = NODE_INDEX;
    resp.response_identifier = -1;
    resp.ping_send_time = req->send_time;
    gettimeofday(&resp.pong_send_time, NULL);

    LOG(DEBUG, "network: handle_req_ping: sending pong %d \n", resp.identifier);
    send_sctp_msg(req->node_index, &resp, sizeof(resp));
    LOG(DEBUG, "network: handle_req_ping: sending pong %d done (%ld ms since ping was sent)\n", 
        resp.identifier, timediff_now(req->send_time));
  } else {
    write_remote_istruct(req->node_index, req->istructp, 1, req->reading_tc);
    LOG(DEBUG, "network: handle_req_ping: unblocking remote TC for ping %d \n", req->identifier);
  }
}

static void handle_resp_ping(const resp_ping* req) {
  LOG(DEBUG, "network: handle_resp_ping: got PONG %d -> total roundtrip time: %ld ms; time since pong send: %ld\n", 
      req->identifier, timediff_now(req->ping_send_time), timediff_now(req->pong_send_time));
}

