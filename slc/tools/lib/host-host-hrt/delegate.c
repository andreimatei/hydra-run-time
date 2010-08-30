#include "sl_hrt.h"
#include "svp/delegate.h"


/*
 * Caps p1 by p2.
 */
void restrict_place(sl_place_t* p1, sl_place_t p2) {
  assert(!p1->place_default && !p1->place_local && !p2.place_default && !p2.place_local);
  
  // check that p1 doesn't contradict p2
  if (p2.node_index != -1) {
    assert(p1->node_index == -1 || p1->node_index == p2.node_index);
  } else if (p2.proc_index != -1) {
    assert(p1->proc_index == -1 || p1->proc_index == p2.proc_index);
  } else if (p2.tc_index != -1) {
    assert(p1->tc_index == -1 || p1->tc_index == p2.tc_index);
  }

  // cap p1
  if (p2.node_index != -1) {
    p1->node_index = p2.node_index;
  }
  if (p2.proc_index != -1) {
    p1->proc_index = p2.proc_index;
  }
  if (p2.tc_index != -1) {
    p1->tc_index = p2.tc_index;
  }
}
