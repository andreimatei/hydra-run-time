#include "sl_hrt.h"
#include "delegate.h"

/*
 * Resolve PLACE_DEFAULT and PLACE_LOCAL for current context.
 * If the place passed is neither of these, return it verbatim.
 */
sl_place_t place_2_canonical_place(sl_place_t place) {
  if (!place.place_local && !place.place_default) return place;
  if (place.place_default) return _cur_tc->place_default;
  assert(place.place_local); 
  return _cur_tc->place_local;
}

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
