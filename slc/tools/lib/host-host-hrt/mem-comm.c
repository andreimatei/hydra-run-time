#include "hrt.h"
#include "sl_hrt.h"
#include "network.h"
#include "mem-comm.h"

void _memdesc(memdesc_t* memdesc, void* p, unsigned int no_elements, unsigned int sizeof_element) {
  memdesc->no_ranges = 1;
  memdesc->ranges[0].p = p;
  memdesc->ranges[0].orig_p = p;
  memdesc->ranges[0].no_elements = no_elements;
  memdesc->ranges[0].sizeof_element = sizeof_element;
}


static void* get_elem_pointer(const mem_range_t* range, int start_elem) {
  return range->p + (start_elem * range->sizeof_element);
}

static int memdesc_desc_local(memdesc_stub_t stub) {
  return (stub.node == NODE_INDEX);
}

static int memdesc_data_local(memdesc_stub_t stub) {
  return stub.have_data || (stub.data_provider == NODE_INDEX);
}

static int memdesc_get_effective_data_prov(memdesc_stub_t stub) {
  if (stub.have_data) return NODE_INDEX;
  else return stub.data_provider;
}

/*--------------------------------------------------
* static mem_range_t get_first_range(memdesc_stub_t stub, mem_range_t* first_range, int ) {
*   if (memdesc_desc_local) {
*     return get_stub_pointer(stub)->ranges[0];
*   } else {
*     return pull_first_range(stub);
*   }
* }
*--------------------------------------------------*/

/*
 * Create a stub for a local descriptor
 */
static memdesc_stub_t _create_memdesc_stub(
    const memdesc_t* desc, 
    int data_provider,
    int have_data) {
  memdesc_stub_t stub;
  stub.node = NODE_INDEX;
  stub.have_data = have_data;
  stub.data_provider = data_provider;
  set_stub_pointer(&stub, desc);
  assert(get_stub_pointer(stub) == desc);  // just for debugging
  return stub;
}

/*
 * Create a new descriptor referring to part of the first range of an existing descriptor.
 * The new descriptor, upon activation, will return the same pointer as the original one.
 */
memdesc_stub_t _memrestrict(memdesc_stub_t orig_stub, memdesc_t* new_desc, //memdesc_stub_t* new_stub, 
                  mem_range_t first_range,  // first range from new_desc
                  int start_elem, int no_elems) {
  mem_range_t range = first_range;//get_first_range(orig_stub);
  new_desc->no_ranges = 1;
  new_desc->ranges[0] = range; 
  // range[0].orig_p remains unchanged
  new_desc->ranges[0].p += (start_elem * range.sizeof_element);
  new_desc->ranges[0].no_elements = no_elems;
  return _create_memdesc_stub(new_desc, orig_stub.data_provider, orig_stub.have_data); //get_effective_data_provider(orig_stub));
}

/*
 * Pull a descriptor from the descriptor provider of stub and updates the stub to point to the local copy.
 * Note that if the descriptor has a single range, no network operation is done, since info about the first 
 * range is provided directly to this function.
 */
static void pull_desc(memdesc_t* new_desc, memdesc_stub_t* stub,
                      mem_range_t first_range, unsigned int no_ranges) {
  assert(no_ranges == 1); // TODO: if > 1, pull the metadata from the remote node
  assert(stub->node != NODE_INDEX);  // should never pull from the local node
  new_desc->no_ranges = 1;
  new_desc->ranges[0] = first_range;
  // we have the descriptor now, so set the descriptor provider to the local node
  stub->node = NODE_INDEX; 
  // make the stub point to the new (local) descriptor
  set_stub_pointer(stub, new_desc);
}

static void pull_data(memdesc_stub_t* stub) {
  assert(!stub->have_data);

  // get pending request slot
  pending_request_t* pending = get_pending_request_slot(_cur_tc);  

  // send the request
  if (memdesc_desc_local(*stub)) {
    // build a req_pull_data_described
    req_pull_data_described req;
    req.type = REQ_PULL_DATA_DESCRIBED;
    req.node_index = NODE_INDEX;
    req.identifier = pending->id;
    req.response_identifier = -1;
    assert(get_stub_pointer(*stub)->no_ranges == 0);  // so far, we only support this for pulling with
                        // a local descriptor. This is usually sufficient, since usually you would get a
                        // local descriptor by a restrict operation, which produces a desc with one range
    req.range = get_stub_pointer(*stub)->ranges[0];
    send_sctp_msg(stub->data_provider, &req, sizeof(req));
  } else {
    // build a req_pull_data
    req_pull_data req;
    req.type = REQ_PULL_DATA;
    req.node_index = NODE_INDEX;
    req.identifier = pending->id;
    req.response_identifier = -1;
    req.desc = get_stub_pointer(*stub);
    assert(stub->data_provider == stub->node);
    send_sctp_msg(stub->node, &req, sizeof(req));
  }

  // block on the pending request slot
  block_for_confirmation(pending);

  stub->have_data = 1;  // we have the data now 
}

/*
 * Adds objects to a descriptor (everything that was part of stub_to_copy).
 * no_ranges[OUT] -> will be set to the new number of ranges in stub.
 */
void _memextend(memdesc_stub_t stub, memdesc_stub_t stub_to_copy, int* no_ranges) {
  assert(memdesc_data_local(stub) && memdesc_desc_local(stub));
  assert(memdesc_data_local(stub_to_copy) && memdesc_desc_local(stub_to_copy));
  memdesc_t* src = get_stub_pointer(stub_to_copy);
  memdesc_t* dest = get_stub_pointer(stub);
  for (int i = 0; i < src->no_ranges; ++i) {
    dest->ranges[dest->no_ranges] = src->ranges[i];
    dest->no_ranges++;
  }
  *no_ranges = dest->no_ranges;
}

/*
 * Pulls data and descriptor for a stub. Updates orig to make it LL, and also returns the new value.
 */
memdesc_stub_t _memlocalize(memdesc_t* new_desc, //memdesc_stub_t* new_stub,
                            memdesc_stub_t* orig,
                            mem_range_t first_range,  // first range of the original descriptor
                            unsigned int no_ranges  // no ranges from the original descriptor
                            ) {
  if (memdesc_data_local(*orig)) {
    if (memdesc_desc_local(*orig)) { // LL
      return *orig;
    } else {  // LR
      pull_desc(new_desc, orig, 
                first_range, no_ranges);
      return *orig;
      //return _create_memdesc_stub(new_desc, get_effective_data_provider(*orig));
    } 
  } else {
    pull_data(orig);//, first_range, no_ranges);
    if (memdesc_desc_local(*orig)) {  // RL
      return *orig;
    } else {  // RR
      pull_desc(new_desc, orig, first_range, no_ranges);
      return *orig;
      //return _create_memdesc_stub(new_desc, get_effective_data_provider(*orig));
    }
  }
}


/*
 * Create a consistent view upon the object described by stub.
 * Return a pointer to the first range of the descriptor.
 */
void* _memactivate(memdesc_stub_t* stub, mem_range_t first_range, unsigned int no_ranges) {
  if (!memdesc_data_local(*stub)) {
    pull_data(stub);
  }
  return first_range.orig_p;
}


void push_data(memdesc_stub_t stub, int dest_node) {
  assert(0); // TODO FIXME
}

/*
 * Propagate local changes back to the data provider.
 */
void _mempropagate(memdesc_stub_t stub) {
  if (stub.data_provider != NODE_INDEX) {
    push_data(stub, stub.data_provider);
  }
}

/*
 * Propagate local changes to the parent (as opposed to the data provider)
 */
void _mempropagate_up(memdesc_stub_t stub, int parent_node) {
  if (parent_node != NODE_INDEX) {
    push_data(stub, parent_node);
  }
}

/*
 * Pushes elements [first_elem...last_elem] from the first range of a descriptor to a particular node.
 */
static void push_elems(int node_index, memdesc_stub_t stub, int first_elem, int last_elem) {
  assert(memdesc_data_local(stub));
  assert(0); //FIXME TODO
}

/*
 * Pulls elements [first_elem...last_elem] from the first range of a descriptor, from a particular node.
 */
static void pull_elems(int node_index, memdesc_stub_t stub, int first_elem, int last_elem) {
  assert(memdesc_data_local(stub));
  assert(0); //FIXME TODO
}

/*
 * Scatters the first range of a descriptor so that each thread i in a family gets a consistent view on
 * elements [a*i + a, a*i + b].
 */
void _memscatter_affine(fam_context_t* fc, memdesc_stub_t stub, int a, int b, int c) {
  int start_thread, end_thread;
  int cur_node = -1;
  for (int i = 0; i < fc->no_ranges; ++i) {
    thread_range_t r = fc->ranges[i];
    if (r.dest.node_index != cur_node) {
      if (cur_node != -1) {
        push_elems(cur_node, stub, a*start_thread + b, a*end_thread + c);
      }
      cur_node = r.dest.node_index;
      start_thread = r.index_start;
      end_thread = r.index_end;
    } else {
      // continuing on a node
      assert(end_thread == r.index_start - 1);
      end_thread = r.index_end;
    }
  } 
}

/*
 * Gathers from a descriptor that was scattered with _memscatter_affine(.. a,b,c)
 */
void _gathermem_affine(fam_context_t* fc, memdesc_stub_t stub, int a, int b, int c) {
  int start_thread, end_thread;
  int cur_node = -1;
  for (int i = 0; i < fc->no_ranges; ++i) {
    thread_range_t r = fc->ranges[i];
    if (r.dest.node_index != cur_node) {
      if (cur_node != -1) {
        pull_elems(cur_node, stub, a*start_thread + b, a*end_thread + c);
      }
      cur_node = r.dest.node_index;
      start_thread = r.index_start;
      end_thread = r.index_end;
    } else {
      // continuing on a node
      assert(end_thread == r.index_start - 1);
      end_thread = r.index_end;
    }
  } 
}


void init_mem_comm() {
}
