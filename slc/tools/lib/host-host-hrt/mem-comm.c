#include <sys/time.h>
#include "hrt.h"
#include "sl_hrt.h"
#include "network.h"
#include "mem-comm.h"

/*
 *  The range of memory that is currently being received over the network.
 *  The SIGSEGV handler uses this to mmap the range when the network interface tries to write it. 
 */
void* cur_incoming_mem_range_start;
unsigned long cur_incoming_mem_range_len;


void _memdesc(memdesc_t* memdesc, void* p, unsigned int no_elements, unsigned int sizeof_element) {
  memdesc->no_ranges = 1;
  memdesc->ranges[0].p = p;
  memdesc->ranges[0].orig_p = p;
  memdesc->ranges[0].no_elements = no_elements;
  memdesc->ranges[0].sizeof_element = sizeof_element;
}

/*--------------------------------------------------
* static void* get_elem_pointer(const mem_range_t* range, int start_elem) {
*   return range->p + (start_elem * range->sizeof_element);
* }
*--------------------------------------------------*/

bool memdesc_desc_local(memdesc_stub_t stub) {
  return (stub.node == NODE_INDEX);
}

static int memdesc_data_local(memdesc_stub_t stub) {
  return (stub.data_provider == NODE_INDEX) || stub.S || stub.have_data;
}

/*--------------------------------------------------
* static int memdesc_get_effective_data_prov(memdesc_stub_t stub) {
*   if (stub.have_data) return NODE_INDEX;
*   else return stub.data_provider;
* }
*--------------------------------------------------*/

/*
 * Create a new descriptor referring to part of the first range of an existing descriptor.
 * The new descriptor, upon activation, will return the same pointer as the original one.
 */
memdesc_stub_t _memrestrict(
    memdesc_stub_t orig_stub, 
    memdesc_t* new_desc, //memdesc_stub_t* new_stub, 
    int start_elem,
    int no_elems) {
  mem_range_t range = get_stub_pointer(orig_stub)->ranges[0]; //first_range;//get_first_range(orig_stub);
  new_desc->no_ranges = 1;
  new_desc->ranges[0] = range; 
  // range[0].orig_p remains unchanged
  new_desc->ranges[0].p = range.orig_p + (start_elem * range.sizeof_element);
  new_desc->ranges[0].no_elements = no_elems;
  return _create_memdesc_stub(new_desc, 
                              orig_stub.data_provider, 
                              orig_stub.have_data,
                              orig_stub.S // S
                              );
}

/*
 * Generate a stub suitable for passing to a child based on an existing stub.
 */
memdesc_stub_t _stub_2_canonical_stub(memdesc_stub_t stub, int S) {
  memdesc_stub_t r = stub;
  r.S = S;
  r.have_data = 0;
  // note that, when memdesc_data_local() will be called on the child, it might still return true
  // if stub.data_provider == node id of the child
  return r;
}


/*
 * Pull a descriptor from the descriptor provider of stub and updates the stub to point to the local copy.
 * Note that if the descriptor has a single range, no network operation is done, since info about the first 
 * range is provided directly to this function.
 */
void pull_desc(memdesc_t* new_desc, memdesc_stub_t* stub, memdesc_t* orig_desc) {//,
                      //mem_range_t first_range, unsigned int no_ranges) {
  if (orig_desc->no_ranges > 1) {

    if (stub->node == NODE_INDEX) {
      // just copy the descriptor
      *new_desc = *orig_desc;
    } else {
      // get pending request slot
      pending_request_t* pending = get_pending_request_slot(_cur_tc);
      assert(pending != NULL);  // TODO: handle error
      req_pull_desc req;
      req.type = REQ_PULL_DESC;
      req.node_index = NODE_INDEX;
      req.identifier = pending->id;
      req.response_identifier = -1;
      req.desc_pointer = orig_desc;
      req.destination = new_desc;
      send_sctp_msg(stub->node, &req, sizeof(req));
      block_for_confirmation(pending);
    }
  } else {
    new_desc->no_ranges = 1;
    new_desc->ranges[0] = orig_desc->ranges[0]; //first_range;
  }
  // we have the descriptor now, so set the descriptor provider to the local node
  stub->node = NODE_INDEX; 
  // make the stub point to the new (local) descriptor
  set_stub_pointer(stub, new_desc);
}

static void pull_data(memdesc_stub_t* stub) {
  assert(stub->data_provider != NODE_INDEX);  // shouldn't be called on such a descriptor; the compiler
                            // should ensure that the generic version of a thread func is not invoked on the
                            // same node as the parent
  assert(!stub->have_data);

  // get pending request slot
  pending_request_t* pending = get_pending_request_slot(_cur_tc);  // this index will be sent as part of
                            // the pull request, and then the remote node will embed it in the data stream that
                            // it pushes. When we receive this stream and read it all, we will write to the
                            // pending request slot to unblock ourselves.

  // send the request
  if (memdesc_desc_local(*stub)) {
    // build a req_pull_data_described
    req_pull_data_described req;
    req.type = REQ_PULL_DATA_DESCRIBED;
    req.node_index = NODE_INDEX;
    req.identifier = pending->id;  
    req.response_identifier = -1;
    req.desc = *get_stub_pointer(*stub);
    LOG(DEBUG, "mem-comm: pull_data: sending pull described request\n");
    send_sctp_msg(stub->data_provider, &req, sizeof(req));
  } else {
    // build a req_pull_data
    req_pull_data req;
    req.type = REQ_PULL_DATA;
    req.node_index = NODE_INDEX;
    req.identifier = pending->id;  // request a confirm message to be sent to this pending request slot
    req.response_identifier = -1;
    req.desc = get_stub_pointer(*stub);
    assert(stub->data_provider == stub->node);
    LOG(DEBUG, "mem-comm: pull_data: sending pull request\n");
    send_sctp_msg(stub->node, &req, sizeof(req));
  }

  // block on the pending request slot
  block_for_confirmation(pending);

  stub->have_data = 1;  // we have the data now 
}

/*
 * Adds objects to a descriptor (everything that was part of stub_to_copy).
 */
void _memextend(memdesc_stub_t stub, memdesc_stub_t stub_to_copy) {
  assert(memdesc_data_local(stub) && memdesc_desc_local(stub));
  assert(stub_to_copy.data_provider == NODE_INDEX && memdesc_desc_local(stub_to_copy));
  memdesc_t* src = get_stub_pointer(stub_to_copy);
  memdesc_t* dest = get_stub_pointer(stub);
  assert((dest->no_ranges + src->no_ranges) <= MAX_RANGES_PER_MEM);
  for (int i = 0; i < src->no_ranges; ++i) {
    dest->ranges[dest->no_ranges++] = src->ranges[i];
  }
}

/*
 * Create a consistent view upon the object described by stub.
 * Copies the first range of the object to new_desc.
 * Return a pointer to the first range of the descriptor.
 * Optionally initializes a new descriptor and a new stub setting the data provider to be the local node.
 */
void* _memactivate(memdesc_stub_t* stub,
                   //mem_range_t first_range,
                   //unsigned int no_ranges,
                   memdesc_t* new_desc,
                   memdesc_stub_t* new_stub
                   ) {
  LOG(DEBUG, "mem-comm: _memactivate: entering\n");
  if (!memdesc_data_local(*stub)) {
    pull_data(stub);
  } else {
    LOG(DEBUG, "mem-comm: _memactivate: nothing to do since we already have the data.\n");
  }
  if (new_stub != NULL) {
    assert(new_desc != NULL);

    new_stub->have_data = 1;
    new_stub->data_provider = NODE_INDEX;
    new_stub->S = 0;
    new_stub->node = stub->node;
    //new_stub->pointer = stub->pointer;
    pull_desc(new_desc, new_stub, get_stub_pointer(*stub));  // this will initialize new_stub.pointer
  }
  
  memdesc_t *desc = get_stub_pointer(*stub);
  void *res = desc->ranges[0].orig_p;
  LOG(DEBUG, "mem-comm: _memactivate: returning %p (desc = %p)\n", res, desc);
  return res;
}


/*
 * Used by propagate operations.
 */
// TODO: make this function static and remove it from sl_hrt.h. It was put there temporarily.
void push_data(memdesc_stub_t stub, unsigned int dest_node) {
  //static int ping_id = 100;
  LOG(DEBUG, "mem-comm: push_data: pushing data to node %d (stub.S = %d)\n", dest_node, stub.S);
  if (dest_node == NODE_INDEX) return;  // nothing to do
  assert(!stub.S);  // if the descriptor if involved in a scatter/gather, we shouldn't have gotten here
  assert(memdesc_data_local(stub));  // we should have the data...
  assert(dest_node != NODE_INDEX);  // shouldn't call this on such a provider... Compiler has to
                            // make sure that a generic version of a gen_callee function is not called on
                            // the same node as the parent.

  assert(memdesc_desc_local(stub));  // for now, only handle local descriptors
                                     // TODO: handle remote descriptors (right now, those can be involved in
                                     // propagate operations. Maybe if we make _memactivate always pull the descriptor,
                                     // we would get rid of this problem...
  memdesc_t* desc = get_stub_pointer(stub);
  // get pending request slot
  pending_request_t* pending = get_pending_request_slot(_cur_tc);  // this index will embedded in the data 
                            // stream that
                            // we push. When all the data is received remotely, we will get a message to
                            // unblock ourselves.
  struct timeval t; gettimeofday(&t, NULL);
  LOG(DEBUG, "mem-comm: push_data: enqueueing a push request. Time is %ld:%ld\n", t.tv_sec, t.tv_usec/1000);
  enqueue_push_request(dest_node,  // destination node
                       pending->id,  // pending request slot to be written on this node
                       1,                // remote confirmation needed 
                       desc->ranges,           // memory to push
                       desc->no_ranges,
                       false,
                       0,0,0,0,0,0);

  // block on the pending request slot
  LOG(DEBUG, "mem-comm: push_data: blocking until all the push data is received.\n");
  struct timeval t1, t2;
  assert(!gettimeofday(&t1, NULL));
  block_for_confirmation(pending);
  assert(!gettimeofday(&t2, NULL));
  long microsec = 1000000 * (t2.tv_sec - t1.tv_sec);
  microsec += t2.tv_usec - t1.tv_usec;
  LOG(DEBUG, "mem-comm: push_data: unblocked... all the data was received by the remote node." \
      " TC was blocked for %ld ms.\n", microsec/1000);
}

/*
 * Propagate local changes back to the data provider.
 */
void _mempropagate(memdesc_stub_t stub) {
  assert(memdesc_data_local(stub));  // we should have the data...
  if (stub.data_provider != NODE_INDEX && !stub.S) {
    LOG(DEBUG, "mem_comm: _mempropagate: pushing data\n");
    push_data(stub, stub.data_provider);
  } else {
    LOG(DEBUG, "mem_comm: _mempropagate: nothing to do since either the destination is the local node " \
               "or the S bit is set.\n");
  }
}

/*
 * Pushes elements [first_elem...last_elem] from the first range of a descriptor to a particular node.
 * Return value: a pointer to a pending request slot that will be unblocked 
 * when the push operation is done.
 * Params:
 *  - node_index - [IN] - destination node
 */
static pending_request_t* push_elems_affine(
                                     unsigned int node_index, 
                                     memdesc_stub_t stub,
                                     int a, int b, int c,
                                     long fam_start_index,
                                     long step,
                                     unsigned long no_generations,
                                     unsigned long gap_between_generations,
                                     unsigned long start_thread,
                                     unsigned long threads_per_generation,
                                     unsigned long start_thread_last_gen,
                                     unsigned long threads_per_generation_last 
                                     ) {
  // TODO: replace calculations in this function with a call to threads_2_scatter_affine_desc
  assert(node_index != NODE_INDEX);
  assert(memdesc_data_local(stub));
  pending_request_t* pending = get_pending_request_slot(_cur_tc);  // this index will embedded in the data 
  assert(pending != NULL);  // TODO: handle error
  mem_range_t first_range = get_stub_pointer(stub)->ranges[0];

  LOG(DEBUG, "mem-comm: push_elems: enqueueing push request for node %d\n", node_index);
  long start_index = _denormalize_index(start_thread, fam_start_index, step);
  long stop_index  = _denormalize_index(start_thread + threads_per_generation - 1, fam_start_index, step);
  long start_elem = a * start_index + b;
  long stop_elem = a * stop_index + c;
  
  long start_index_last = _denormalize_index(start_thread_last_gen, fam_start_index, step);
  long stop_index_last  = _denormalize_index(start_thread_last_gen + threads_per_generation_last - 1, 
                                             fam_start_index, step);
  long start_elem_last = a * start_index_last + b;
  long stop_elem_last  = a * stop_index_last + c;

  long start_index_second_gen = _denormalize_index(start_thread + gap_between_generations, fam_start_index, step);
  //long stop_index_second_gen  = _denormalize_index(
  //                                      start_thread + threads_per_generation - 1 + gap_between_generations,
  //                                      fam_start_index, step);
  
  long start_elem_second_gen = a * start_index_second_gen + b;
  //long stop_elem_second_gen  = a * stop_index_second_gen + c;

  unsigned long no_elems_per_segment = stop_elem - start_elem + 1;
  unsigned long no_elems_per_segment_last = stop_elem_last - start_elem_last + 1;

  enqueue_push_request(node_index,   // destination node
                       pending->id,  // pending request slot to be written on this node
                       true,         // remote confirmation needed 
                       &first_range, // memory to push
                       1,            // number of ranges to push
                       true,         // segmented
                       no_generations,
                       start_elem,
                       start_elem_last,
                       no_elems_per_segment,
                       no_elems_per_segment_last,
                       start_elem_second_gen - start_elem
                       );
  return pending;
}

static scatter_affine_desc_t threads_2_scatter_affine_desc(
    mem_range_t range,
    unsigned long no_generations,
    long fam_start_index,          // denormalized (real) index of the first thread of the family 
                                   // (not specific to this node)
    long step,
    int a, int b, int c,           // parameters used for memscatter_affine
    unsigned long start_thread,    // (normalized) index of the first thread to be run on a node
    unsigned long start_thread_last_gen,  // (normalized) index of the first thread to be run as part of 
                                          // the last generation (on a particular node)
    unsigned long threads_per_generation,
    unsigned long threads_per_generation_last,
    unsigned long gap_between_generations  // number of threads that are skipped between the last one run
                                           // on a particular node as part of a generation, and the first one
                                           // run on the same node as part of the same generation
    ) {
  scatter_affine_desc_t desc;

  long start_index = _denormalize_index(start_thread, fam_start_index, step);
  long stop_index = _denormalize_index(start_thread + threads_per_generation - 1, fam_start_index, step);
  long start_index_second_gen = _denormalize_index(start_thread + gap_between_generations, fam_start_index, step);
  long start_index_last = _denormalize_index(start_thread_last_gen, fam_start_index, step);
  long stop_index_last  = _denormalize_index(start_thread_last_gen + threads_per_generation_last - 1, fam_start_index, step);
  unsigned long start_elem = a * start_index + b;
  unsigned long start_elem_second_gen = a * start_index_second_gen + b;
  unsigned long stop_elem = a * stop_index + c;
  unsigned long start_elem_last = a * start_index_last + b;
  unsigned long stop_elem_last = a * stop_index_last + c;


  desc.range = range;
  desc.no_segments = no_generations;
  desc.start_first_segment = start_elem;
  desc.start_last_segment = a * start_index_last + b;
  desc.no_elems_per_segment = stop_elem - start_elem + 1;
  desc.no_elems_per_segment_last = stop_elem_last - start_elem_last + 1;
  desc.gap_between_segments = start_elem_second_gen - start_elem;

  return desc;
}


/*
 * Pulls elements of the first range of a descriptor according to the way they were scattered by
 * memscatter_affin().
 * Used for gather operations.
 */
static pending_request_t* pull_elems_affine(
    int node_index,
    scatter_affine_desc_t scatter_desc
    ) {

  pending_request_t* pending = get_pending_request_slot(_cur_tc);  // this index will embedded in the data 
  assert(pending != NULL);  // TODO: handle error
  
  // build a req_pull_data_described
  req_pull_data_affine req;
  req.type = REQ_PULL_DATA_AFFINE;
  req.node_index = NODE_INDEX;
  req.identifier = pending->id;  // a confirmation will be sent using this slot
  req.response_identifier = -1;

  req.scatter_desc = scatter_desc;
  
  send_sctp_msg(node_index, &req, sizeof(req));

  return pending;
}

/*
 * Scatters the first range of a descriptor so that the thread with index i in a family gets a consistent view on
 * elements [a*i + b, a*i + c].
 */
void _memscatter_affine(fam_context_t* fc, 
                        memdesc_stub_t stub, 
                        //mem_range_t first_range,
                        int a, int b, int c,
                        long fam_start_index,
                        long step) {
  pending_request_t* pending_pushes[MAX_NODES];
  size_t no_pushes = 0;
  int cur_node = -1;
  unsigned long start_thread, start_thread_last_gen;
  unsigned long no_threads = 0, no_threads_last_gen = 0;
  unsigned long no_generations = fc->distribution.no_generations;
  unsigned long total_threads_per_generation = 0;  // across all procs/tcs...
  assert(memdesc_data_local(stub));  // we must have the data for scattering
  assert(get_stub_pointer(stub)->no_ranges == 1);  // we only support this
  assert(get_stub_pointer(stub)->ranges[0].p == get_stub_pointer(stub)->ranges[0].orig_p);  // for now, this is the only thing we support (since prepare_push_buffer()
                                  // and push_data() in network.c use the .p member). TODO: think if we can
                                  // support more than this (i.e. scattering for stubs that are the result
                                  // of a restrict operation) and decide whether the functions in network.c
                                  // should continue to use .p or should switch to .orig_p
  for (size_t i = 0; i < fc->distribution.no_reservations; ++i) {
    total_threads_per_generation += fc->distribution.reservations[i].no_threads_per_generation;
  }
  
  start_thread = 0;
  start_thread_last_gen = fc->distribution.start_index_last_generation;
  for (size_t i = 0; i < fc->distribution.no_reservations; ++i) {
    proc_reservation res = fc->distribution.reservations[i];
    if (res.first_tc.node_index == cur_node) {  // continue adding to current node
      no_threads += res.no_threads_per_generation;
      no_threads_last_gen += res.no_threads_per_generation_last;
    } else {
      if (cur_node != -1 && cur_node != (signed)NODE_INDEX) {
        // send elements
        pending_pushes[no_pushes++] = push_elems_affine(
                                                 cur_node,
                                                 stub,
                                                 a, b, c, fam_start_index, step,
                                                 no_generations,
                                                 total_threads_per_generation,
                                                 start_thread,
                                                 no_threads,
                                                 start_thread_last_gen,
                                                 no_threads_last_gen);
                                                  
      }

      cur_node = res.first_tc.node_index;
      start_thread += no_threads;
      start_thread_last_gen += no_threads_last_gen;
      no_threads = res.no_threads_per_generation;
      no_threads_last_gen = res.no_threads_per_generation_last;
    }
  }

  if (cur_node != -1 && cur_node != (signed)NODE_INDEX) {
    // send elements to last node
    pending_pushes[no_pushes++] = push_elems_affine(
      cur_node,
      stub,
      a, b, c, 
      fam_start_index, 
      step,
      no_generations,
      total_threads_per_generation,
      start_thread,
      no_threads,
      start_thread_last_gen,
      no_threads_last_gen);
  }

  // block until all the push operations complete
  // TODO: this is ugly; we block on one operation at a time.
  // What we'd really want is a semaphore 
  LOG(DEBUG, "mem-comm: scatter: blocking for %d confirmation(s)\n", no_pushes);
  for (size_t i = 0; i < no_pushes; ++i) {
    block_for_confirmation(pending_pushes[i]); 
  }
}

/*
 * Gathers from a descriptor that was scattered with _memscatter_affine(... a,b,c).
 */
void _memgather_affine(
    fam_context_t* fc,
    mem_range_t range,
    int a, int b, int c,
    long fam_start_index,
    long step) {
  unsigned long no_threads = 0, no_threads_last_gen = 0;
  unsigned long start_thread = 0, start_thread_last_gen = fc->distribution.start_index_last_generation;
  int cur_node = -1;
  pending_request_t* pending_pulls[MAX_NODES];
  size_t no_pulls = 0;

  LOG(DEBUG, "mem-comm: _memgather_affine: entering\n");

  unsigned long total_threads_per_generation = 0;  // across all procs/tcs...
  for (size_t i = 0; i < fc->distribution.no_reservations; ++i) {
    total_threads_per_generation += fc->distribution.reservations[i].no_threads_per_generation;
  }
  
  for (size_t i = 0; i < fc->distribution.no_reservations; ++i) {
    proc_reservation res = fc->distribution.reservations[i];
    if (res.first_tc.node_index == cur_node) {  // continue adding to the current node
      no_threads += res.no_threads_per_generation;
      no_threads_last_gen += res.no_threads_per_generation_last;
    } else {
      if (cur_node != -1 && cur_node != (signed)NODE_INDEX) {
        // pull elements
        scatter_affine_desc_t desc = threads_2_scatter_affine_desc(range,
                                                                   fc->distribution.no_generations,
                                                                   fam_start_index,
                                                                   step,
                                                                   a, b, c,
                                                                   start_thread,
                                                                   start_thread_last_gen,
                                                                   no_threads,
                                                                   no_threads_last_gen,
                                                                   total_threads_per_generation);
                                                                
        LOG(DEBUG, "mem-comm: _memgather_affine: pulling from a node\n");
        pending_request_t* pending = pull_elems_affine(cur_node, desc); 
        pending_pulls[no_pulls++] = pending;
      }
      cur_node = res.first_tc.node_index;
      start_thread += no_threads;
      start_thread_last_gen += no_threads_last_gen;
      no_threads = res.no_threads_per_generation;
      no_threads_last_gen = res.no_threads_per_generation_last;
    }
  }

  if (cur_node != -1 && cur_node != (signed)NODE_INDEX) {
    // pull elements from last node
    LOG(DEBUG, "mem-comm: _memgather_affine: pulling from last node\n");
    scatter_affine_desc_t desc = threads_2_scatter_affine_desc(range,
        fc->distribution.no_generations,
        fam_start_index,
        step,
        a, b, c,
        start_thread,
        start_thread_last_gen,
        no_threads,
        no_threads_last_gen,
        total_threads_per_generation);

    pending_request_t* pending  = pull_elems_affine(cur_node, desc); 
    pending_pulls[no_pulls++] = pending;
  }
  // block until all the push operations complete
  // TODO: this is ugly; we block on one operation at a time.
  // What we'd really want is a semaphore 
  LOG(DEBUG, "mem-comm: _memgather_affine: blocking for %d confirmation(s)\n", no_pulls);
  for (size_t i = 0; i < no_pulls; ++i) {
    block_for_confirmation(pending_pulls[i]); 
  }
}


void init_mem_comm() {
}
