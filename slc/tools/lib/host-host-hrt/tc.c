#include <stdlib.h>
#include <ucontext.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/sctp.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include "hrt.h"
#include "network.h"
#include "mem-comm.h"
#include "sl_hrt.h"

#define CACHE_LINE_SIZE 128  // in bytes
#define MAPPING_CHUNK (1<<20)  // the size and allignment of chunks to map on SIGSEGV

/* the start of then address space used by the TCs of nodes */
#define ADDR_SPACE_START ((void*)0x100000)  // 1 MB

#define VM_BITS_PER_NODE 40
#define VM_PER_NODE (1L<<VM_BITS_PER_NODE)
#define VM_BITS_PER_TC (25)
#define VM_PER_TC (1L<<VM_BITS_PER_TC)
#define VM_TC_CONTROL_STRUCTURES (1L<<20)  // vm for TC control structures,
                                           // available at the beginning of each TC

/* start address for the vm of the current node */
#define NODE_START_VM ((void*)(ADDR_SPACE_START + NODE_INDEX * VM_PER_NODE))
#define TC_START_VM(index) ((tc_t*)(NODE_START_VM + VM_PER_TC + (index)*VM_PER_TC))
                            // first VM_PER_TC is left for the runtime

//#define RT_START_VM (node_start_addr)
#define RT_START_VM ((void*)NODE_START_VM)
//#define RT_END_VM (node_start_addr + VM_PER_TC - 1)  // RT's vm is equal to a TC's vm
#define RT_END_VM ((void*)(NODE_START_VM + VM_PER_TC - 1))  // RT's vm is equal to a TC's vm
#define PROCESSOR_PTHREAD_STACK_SIZE ((unsigned long long)1<<15)

#define NO_FAM_CONTEXTS_PER_PROC 1024

unsigned int NODE_INDEX = 0;
//#define NODE_INDEX 16L
//void* node_start_addr = (void*)(NODE_INDEX * VM_PER_NODE);
unsigned int NO_PROCS = 2;  // the number of processors that will be created and used on the current node
processor_t _processor[MAX_PROCS_PER_NODE];


static char primary_address[100];  // ip address of the primary node (for now, it is only populated on
                            // that node itself)
static int primary_port;  // see above


static unsigned int tc_holes[1000];
static unsigned int no_tc_holes = 0;

static const char* log_prefix[20] = {"[CRASH]!!!!!!!!!!!!!!!!!!!! ",  // 0
                              "[CRASH+1] ",  // 1
                              "[CRASH+2] ",  // 2
                              "[WARNING]~~~~~~~~~~~~~~~~~~ ",  // 3
                              "[WARNING + 1] ",  // 4
                              "[WARNING + 2] ",  // 5
                              "[WARNING + 3] ",  // 6
                              "[INFO] ",  // 7
                              "[INFO + 1] ",  // 8
                              "[INFO + 2] ",  // 9
                              "[VERBOSE] ",  // 10
                              "[VERBOSE + 1] ",  // 11
                              "[VERBOSE + 2] ",  // 12
                              "[VERBOSE + 3] ",  // 13
                              "[VERBOSE + 4] ",  // 14
                              "[DEBUG] ",  // 15
                              "[DEBUG + 1] ",  // 16
                              };

#ifdef LOGGING
LOG_LEVEL _logging_level = LOGGING;
#else
LOG_LEVEL _logging_level = DEBUG;
#endif

void LOG(LOG_LEVEL level, char* fmt, ...) {
  if (level <= _logging_level) {
    va_list args;
    va_start(args, fmt);
    char s[strlen(fmt) + 20];
    strcpy(s, log_prefix[level]);
    strcpy(s + strlen(log_prefix[level]), fmt);
    struct timeval t; gettimeofday(&t, NULL);
    fprintf(stdout, "%3ld:%3ld ", t.tv_sec % 1000, t.tv_usec/1000);  //FIXME: write to stderr
    vfprintf(stdout, s, args);  // FIXME: write to stderr
    va_end(args);
  }
}


tc_t* TC_START_VM_EX(int node_index, int tc_index) {
  return ADDR_SPACE_START + (node_index * VM_PER_NODE) + (tc_index + 1) * VM_PER_TC;  // + 1 because
                                                              // first VM_PER_TC is left for runtime 

  /*
  assert(node_index == NODE_INDEX); // TODO
  //TODO: this function will have to look in some table with the start of the address space
  // for all nodes
  return TC_START_VM(tc_index); //TODO: this will have to be removed, see above
  */
}

extern inline void st_rel_istruct(volatile i_struct* istruct, enum istruct_state value) {
  // TODO: store|store -> nop
  // TODO: load|store  -> nop
  __asm__ __volatile__("" : : : "memory");
  istruct->state = value;
  __asm__ __volatile__("" : : : "memory");
  // TODO: store|load => this one is _not_ a no-op on x86 BUT is only needed for seq consist, not for
  // release semantics... right?
}

extern inline enum istruct_state ld_acq_istruct(volatile i_struct* istruct) {
  enum istruct_state rez;
  __asm__ __volatile__("" : : : "memory");
  rez = istruct->state;
  __asm__ __volatile__("" : : : "memory");
  //TODO: load|load -> nop
  //TODO: load|store -> nop
  return rez;
}



/*
struct tc_node {
  struct tc_node* next;
};
typedef struct tc_node tc_node;
tc_node
*/
typedef union {
  pthread_spinlock_t lock;
  char c[CACHE_LINE_SIZE];
} padded_spinlock_t;

pthread_t threads[MAX_PROCS_PER_NODE];
pthread_attr_t threads_attr[MAX_PROCS_PER_NODE];
__thread tc_t* _cur_tc = NULL;  // pointer to the TC currently occupying the accessing processor
              // FIXME: check out where these things are allocated... if it causes mmapings of
              // regions that we have no control over, replace it with a macro that uses the SP
              // to get access to the TC
volatile int rt_init_done = 0;
pthread_spinlock_t rt_init_done_lock;



fam_context_t fam_contexts[MAX_PROCS_PER_NODE][NO_FAM_CONTEXTS_PER_PROC];
/* locks for allocating fam contexts; one per processor */
padded_spinlock_t fam_contexts_locks[MAX_PROCS_PER_NODE];
// circular queue for available tc's
int available_fam_contexts[MAX_PROCS_PER_NODE][NO_FAM_CONTEXTS_PER_PROC];
int count_available_fam_contexts[MAX_PROCS_PER_NODE];
int start_available_fam_contexts[MAX_PROCS_PER_NODE];
int end_available_fam_contexts[MAX_PROCS_PER_NODE];


// circular buffer for TC's which are free for reuse
padded_spinlock_t allocate_tc_locks[MAX_PROCS_PER_NODE];
int available_tcs[MAX_PROCS_PER_NODE][NO_TCS_PER_PROC];
int count_available_tcs[MAX_PROCS_PER_NODE];
int start_available_tcs[MAX_PROCS_PER_NODE];
int end_available_tcs[MAX_PROCS_PER_NODE];
// ----


// circular queue for runnable tc's
padded_spinlock_t runnable_queue_locks[MAX_PROCS_PER_NODE];
//tc_t* runnable_tcs[MAX_PROCS_PER_NODE][NO_TCS_PER_PROC];
int runnable_tcs[MAX_PROCS_PER_NODE][NO_TCS_PER_PROC];
int runnable_count[MAX_PROCS_PER_NODE];
int start_runnable_tcs[MAX_PROCS_PER_NODE];
int end_runnable_tcs[MAX_PROCS_PER_NODE];


int tc_valid[NO_TCS_PER_PROC * MAX_PROCS_PER_NODE];  // 1 if a TC has been created, 0 if not (due to a
                                                        // memory hole)

//void _fam___root_fam(void);
void __slFfmta___root_fam_metaloop();  // thread function for the root family, generated by the compiler based
                              // on the function definition for root_fam in this source file
void unblock_tc(tc_t* tc, int same_processor);
void block_tc_and_unlock(tc_t* tc, pthread_spinlock_t* lock);
void suspend_on_istruct(volatile i_struct* istructp, int same_proc);
/* 
 * Recursively prepares every TC in a chain to run parts of a family. The TC's are also scheduled to run.
 */
static void populate_tc(
                 tc_t* tc,
                 thread_func func,

                 unsigned long no_generations, 
                 unsigned long no_threads_per_gen,  // for this TC
                 unsigned long no_threads_per_gen_last_tc,  // for last TC on proc
                 unsigned long no_threads_last_gen,  // for this TC
                 unsigned long no_threads_last_gen_last_tc,  // for last TC on proc

                 long gap_between_generations,
                 long start_index,
                 long start_index_last_generation,
                
                 long denormalized_fam_start_index,
                 long step,
                 
                 tc_ident_t parent,
                 tc_ident_t prev,  // the TC that is going to run the ranges just before the first TC in this chain
                 tc_ident_t next,  // the TC that is going to run the ranges just after the first TC in this chain
                 bool is_first_proc_on_fam, // will be used by the first tc on the proc
                 bool is_last_proc_on_fam,  // will be used by the last tc on the proc
                 
                 i_struct* final_shareds, 
                 memdesc_t* final_descs,
                 i_struct* done,
                 const tc_ident_t* current_tc, // the TC of the caller; can be passed a dummy or NULL
                 default_place_policy_enum default_place_policy,// policy to be used when deciding the 
                                                                // PLACE_DEFAULT to be inheritied by
                                                                // the child
                 sl_place_t default_place_parent     // PLACE_DEFAULT of the parent. Used if 
                                                     // default_place_policy == INHERIT_DEFAULT_PLACE
                 );
static void free_fc(int proc_id, int fc_id);
static int grab_available_tc(int proc_id);
static int grab_available_fam_context(int proc_id);
static int grab_runnable_tc(int proc_id);
void mark_tc_as_runnable(int proc_id, int tc_id, int lock_needed);
void write_istruct_no_checks(i_struct* istructp, long val);


/*
 * Check wether a particular TC has been created on this node or not.
 * This function should be used whenever we iterate blindly through TCs.
 *
 *
 * *) some TCs might not have been created due to holes in the address space.
 */
int tc_is_valid(int tc_index) {
  return tc_valid[tc_index];
}

mapping_hint_t _interpret_mf_distribute_ex(unsigned int nodes, 
                                           unsigned int procs,
                                           unsigned int tcs,
                                           unsigned long block) {
  mapping_hint_t r;
  r.hint_nodes = nodes; r.hint_procs = procs; r.hint_tcs = tcs; r.hint_block = block;
  return r;
}

mapping_hint_t _interpret_mf_distribute(sl_place_t place,
                                       unsigned long no_threads,
                                       bool independent,
                                       unsigned long n) {
  mapping_hint_t r;
  if (no_threads == 0) no_threads = 1;
  if (n == 0) n = 1;
  sl_place_t final_place = _place_2_canonical_place(place);
  if (final_place.tc_index != -1) {
    r.hint_nodes = r.hint_procs = r.hint_tcs = 1;
    r.hint_block = no_threads;
  } else if (final_place.proc_index != -1) {
    r.hint_nodes = 1; r.hint_procs = 1; 
    r.hint_tcs = MAX(no_threads / n, 1); 
    r.hint_block = independent ? n : 1;
  } else if (final_place.node_index != -1) {
    r.hint_nodes = 1; r.hint_procs = MAX(no_threads / n, 1); r.hint_tcs = 1; 
    r.hint_block = independent ? n : 1;
  } else {
    r.hint_nodes = no_secondaries; 
    r.hint_procs = 1; r.hint_tcs = 1; 
    r.hint_block = independent ? n : 1;
  }

  return r;
}

mapping_hint_t _interpret_mf_spread(sl_place_t place,
                                    unsigned long no_threads,
                                    bool independent,
                                    unsigned long n) {
  return _interpret_mf_distribute(place, no_threads, independent, n);
}

mapping_hint_t _interpret_mf_none(sl_place_t place,
                                  unsigned long no_threads,
                                  bool independent,
                                  unsigned long block) {
  return _interpret_mf_spread(place, no_threads, independent, block); 
}

static void figure_out_fam_distribution(sl_place_t place,
                                        bool gencallee,
                                        unsigned long no_threads,
                                              
                                        mapping_hint_t hint,

                                        unsigned int* no_nodes,
                                        unsigned int* no_procs,
                                        unsigned int* no_tcs,
                                        unsigned long* block,
                                        unsigned long* no_generations
                                        ) {

  sl_place_t final_place = _place_2_canonical_place(place);
  assert(hint.hint_nodes > 0 && hint.hint_procs > 0 && hint.hint_tcs > 0 && hint.hint_block > 0);
  assert(hint.hint_block <= no_threads);

  if (!gencallee && (final_place.node_index != -1)) {
    assert(final_place.node_index == (signed)_cur_tc->ident.node_index);
  }

  if (!gencallee && hint.hint_nodes > 1) {
    LOG(WARNING, "Attempting to distribute a family which was not marked as a \"gencallee\" on more than "
        "the local node. Reverting to distributing on the local node. The requested number of processors "
        "will be incremented accordingly.\n");
    hint.hint_procs *= hint.hint_nodes;
    hint.hint_nodes = 1;
  }

  // limit by resources
  hint.hint_nodes = MIN(hint.hint_nodes, no_secondaries);
  hint.hint_procs = MIN(hint.hint_procs, NO_PROCS);
  hint.hint_tcs   = MIN(hint.hint_tcs,   NO_TCS_PER_PROC);

  // limit by block size, make sure each TC has at least a chunk
  unsigned long no_chunks = no_threads / hint.hint_block;
  assert(no_chunks > 0);
  hint.hint_nodes = MIN(hint.hint_nodes, no_chunks);
  hint.hint_procs = MIN(hint.hint_procs, no_chunks / hint.hint_nodes);
  hint.hint_tcs   = MIN(hint.hint_tcs, no_chunks / (hint.hint_nodes * hint.hint_procs));

  unsigned long threads_per_gen = hint.hint_nodes * hint.hint_procs * hint.hint_tcs * hint.hint_block;
  *no_generations = no_threads / threads_per_gen;

  *no_nodes = hint.hint_nodes;
  *no_procs = hint.hint_procs;
  *no_tcs   = hint.hint_tcs; 
  *block    = hint.hint_block;
}

mapping_decision map_fam(
    thread_func func __attribute__((unused)),
    unsigned long no_threads,
    sl_place_t place,  // place where the family has been delegated to
    bool gencallee,  // 1 if the function can be executed on other nodes than the parent, 0 otherwise
    mapping_hint_t hint,
    struct mapping_node_t* parent_id) {

  LOG(DEBUG, "map_fam: mapping fam of %d threads\n", 
      no_threads);
  assert(parent_id == NULL); // TODO

  mapping_decision rez;

  assert(place.tc_index == -1);  // we don't support delegating to particular TC's. The current TC can be 
                                 // referred to by PLACE_LOCAL.

  LOG(DEBUG, "map_fam: requested place: node: %d, proc: %d, tc:%d, default: %d, local: %d\n",
      place.node_index, place.proc_index, place.tc_index, place.place_default, place.place_local);
  sl_place_t final_place = _place_2_canonical_place(place);
  LOG(DEBUG, "map_fam: resolved place: node: %d, proc: %d, tc:%d\n", final_place.node_index, final_place.proc_index, final_place.tc_index);

  unsigned int no_nodes, no_procs, no_tcs;
  unsigned long block;
  unsigned long no_generations;
  figure_out_fam_distribution(place, gencallee, no_threads, hint,
                              &no_nodes, &no_procs, &no_tcs, &block, &no_generations);
  assert(no_generations > 0);

  if (!gencallee) {
    assert(no_nodes == 1);
  }
  assert(no_nodes <= no_secondaries);
  assert(no_procs <= NO_PROCS);
  assert(no_tcs <= NO_TCS_PER_PROC);

  LOG(DEBUG, "map_fam: settled on %ld generations\n", no_generations);
  rez.no_ranges_per_tc = no_generations;

  int start_node = final_place.node_index != -1 ? final_place.node_index : _cur_tc->ident.node_index;
  int start_proc = final_place.proc_index != -1 ? final_place.proc_index : _cur_tc->ident.proc_index;
  LOG(DEBUG, "map_fam: starting to map from node %d and proc %d\n", start_node, start_proc);
  //int start_tc   = final_place.tc_index   != -1 ? final_place.tc_index   : _cur_tc->ident.tc_index;

  int load_percentage = 100 / (no_nodes * no_procs);
  rez.should_inline = (final_place.tc_index != -1
                       && final_place.node_index == (signed)NODE_INDEX 
                       && final_place.proc_index == (signed)_cur_tc->ident.proc_index
                       && final_place.tc_index   == (signed)_cur_tc->ident.tc_index);
  if (rez.should_inline) assert(no_nodes == 1 && no_procs == 1 && no_tcs == 1);
  rez.no_proc_assignments = no_nodes * no_procs;
  assert(rez.no_proc_assignments <= MAX_NO_PROC_ASSIGNMENTS_PER_MAPPING);
  for (unsigned int i = 0; i < no_nodes; ++i) {
    for (unsigned int j = 0; j < no_procs; ++j) {
      rez.proc_assignments[i * no_procs + j].node_index = (start_node + i) % no_secondaries;
      rez.proc_assignments[i * no_procs + j].proc_index = (start_proc + j) % NO_PROCS;
      rez.proc_assignments[i * no_procs + j].no_tcs = no_tcs;
      rez.proc_assignments[i * no_procs + j].load_percentage = load_percentage;
    }
  }
  
  LOG(DEBUG, "map_fam: returning mapping with %d proc assignments on %d nodes, "
             " (%d procs on each node); inline: %d\n", 
             rez.no_proc_assignments, no_nodes, no_procs, rez.should_inline);
  for (unsigned int i = 0; i < rez.no_proc_assignments; ++i) {
    LOG(DEBUG, "map_fam: assignment %d: node: %d proc: %d no_tcs: %d load: %d\n",
                i, 
                rez.proc_assignments[i].node_index, 
                rez.proc_assignments[i].proc_index, 
                rez.proc_assignments[i].no_tcs,
                rez.proc_assignments[i].load_percentage);
  }

  return rez;
}

int no_allocate_requests_per_proc[MAX_PROCS_PER_NODE];  // FIXME: remove this

/*
 * Allocate a chain of TC's on a specified proc.
 */
void allocate_local_tcs(
    unsigned int proc_index, 
    //tc_ident_t parent, 
    unsigned int no_tcs, 
    //int* tcs, 
    unsigned int* no_allocated_tcs,
    tc_ident_t* first_tc,
    tc_ident_t* last_tc) {
  no_allocate_requests_per_proc[proc_index]++;  // FIXME: remove this
  int tcs[no_tcs];

  *no_allocated_tcs = 0;
  for (unsigned int i = 0; i < no_tcs; ++i) {
    //int tc = atomic_increment_next_tc(proc_index);
    int tc = grab_available_tc(proc_index);
    LOG(DEBUG, "allocate_local_tcs: grab_available_tc(%d) returned %d\n", proc_index, tc);
    if (tc != -1) {
      tcs[*no_allocated_tcs] = tc;
      *no_allocated_tcs = *no_allocated_tcs + 1;
    } else {
      LOG(DEBUG, "allocate_local_tcs: grab_available_tc(%d) returned -1\n", proc_index);
      break;
    }
  }
  
  tc_ident_t dummy; dummy.node_index = dummy.proc_index = dummy.tc_index = -1;

  
  // init .prev and .next for TC's other than the first and the last in the chain. For these,
  // the fields will be initialized at population.
  for (unsigned int i = 0; i < *no_allocated_tcs; ++i) {
    tc_t* tc = (tc_t*)TC_START_VM(tcs[i]);
    if (i < *no_allocated_tcs - 1) {
      tc->next = ((tc_t*)TC_START_VM(tcs[i + 1]))->ident;
      tc->is_last_tc_on_proc = false;
    } else {
      tc->next = dummy;
      tc->is_last_tc_on_proc = true;
      *last_tc = tc->ident;
    }

    if (i > 0) {
      tc->prev = ((tc_t*)TC_START_VM(tcs[i - 1]))->ident;
      tc->is_first_tc_on_proc = false;
    } else {
      tc->prev = dummy;
      tc->is_first_tc_on_proc = true;
      *first_tc = tc->ident;
    }
  }

  LOG(DEBUG, "allocate_local_tcs: allocated %d TC's out of the %d requested on proc %d.\n", 
      *no_allocated_tcs, no_tcs, proc_index);
}


/*
 * Allocates a family context and thread contexts. Fills in the distribution field of the FC.
 * Returns NULL if resources couldn't be allocated (either no FC, or no TC's).
 */
fam_context_t* allocate_fam(
    //long start_index,
    //long end_index,
    //long step,
    unsigned long total_threads,
    struct mapping_node_t* parent_id __attribute__((unused)), 
    const struct mapping_decision* mapping) {


  assert(mapping != NULL);
  //find an empty fam_context within the family contexts of the current proc
  //(technically, it doesn't matter where the fam context is chosen from)
  unsigned int i;
  fam_context_t* fc;
  int proc_index = (_cur_tc == NULL) ? 0 : _cur_tc->ident.proc_index;  // this will be NULL when allocating __root_main

  // allocate a FC
  int fc_index = grab_available_fam_context(proc_index);
  LOG(DEBUG, "allocate_fam: got fc index %d\n", fc_index);
  // return failure if no FC could be allocated
  if (fc_index == -1) {
    return NULL;
  }
  fc = &fam_contexts[proc_index][fc_index];
  assert(fc->empty); // sanity check

  // initialize the allocated FC
  fc->empty = 0;
  fc->done.state = EMPTY;
  for (int j = 0; j < MAX_ARGS_PER_FAM; ++j) {
    fc->shareds[j].state = EMPTY;
  }
  
  typedef struct allocated_tcs_t {
    unsigned int no_allocated_tcs;
    unsigned long no_threads;
    tc_ident_t first_tc, last_tc;
  }allocated_tcs_t;
  allocated_tcs_t allocated_tcs[mapping->no_proc_assignments];

  unsigned int allocated_procs = 0;
  int last_allocated_proc_index = -1;
  unsigned int load_to_redistribute = 0;

  for (i = 0; i < mapping->no_proc_assignments; ++i) {
    proc_assignment as = mapping->proc_assignments[i];
    if (as.node_index == NODE_INDEX) {
      LOG(DEBUG, "allocate_fam: requesting %d local TC's\n", as.no_tcs);
      allocate_local_tcs(as.proc_index, 
                         //_cur_tc->ident, 
                         as.no_tcs, 
                         &allocated_tcs[i].no_allocated_tcs, 
                         &allocated_tcs[i].first_tc,
                         &allocated_tcs[i].last_tc);
    } else {
      LOG(DEBUG, "allocate_fam: requesting %d remote TC's\n", as.no_tcs);
      allocate_remote_tcs(as.node_index,
                          as.proc_index,
                          as.no_tcs,
                          &allocated_tcs[i].no_allocated_tcs, 
                          &allocated_tcs[i].first_tc,
                          &allocated_tcs[i].last_tc);
      LOG(DEBUG, "allocate_fam: back from remote allocation\n");
    }
    if (allocated_tcs[i].no_allocated_tcs == 0) {
      // redistribute the load assigned to this proc
      load_to_redistribute += as.load_percentage;
      LOG(DEBUG, "failed to allocate TC's on node %d processor %d\n", NODE_INDEX, as.proc_index);
    } else {
      last_allocated_proc_index = i;
      ++allocated_procs;
      LOG(DEBUG, "allocated %d TC's on node %d processor %d\n", 
          allocated_tcs[i].no_allocated_tcs, NODE_INDEX, as.proc_index);
    }
  }
    
  // if no TC's could be allocated anywhere, return failure
  if (last_allocated_proc_index == -1) {
    // free the allocated family context
    free_fc(fc->index / NO_FAM_CONTEXTS_PER_PROC, fc->index % NO_FAM_CONTEXTS_PER_PROC);
    return NULL;
  }

  LOG(DEBUG, "finished allocating resources. Load to redistribute: %d\%\n", load_to_redistribute);

  // redistribute the load of the procs where we couldn't get any TC's and
  // compute the number of threads that go to each proc
  //unsigned long total_threads = (end_index - start_index + 1) / step;
  int additional_load = load_to_redistribute / allocated_procs;
  int additional_load_last = load_to_redistribute % allocated_procs;
  unsigned long allocated_threads = 0;
  for (int i = 0; i <= last_allocated_proc_index; ++i) {
    if (allocated_tcs[i].no_allocated_tcs == 0) continue;
    proc_assignment as = mapping->proc_assignments[i];
    // compute the load (in percentages) going to this proc
    int load = as.load_percentage + additional_load;
    if (i == last_allocated_proc_index) load += additional_load_last;
    LOG(DEBUG, "allocate_fam: computing no_threads for %dth proc involved. It's load is %d\%\n",
        i, load);

    // compute the number of threads going to this proc
    if (i < last_allocated_proc_index) {
      allocated_tcs[i].no_threads = load * total_threads / 100;
      allocated_threads += allocated_tcs[i].no_threads;
    } else {
      allocated_tcs[i].no_threads = total_threads - allocated_threads;
    }
    LOG(DEBUG, "allocate_fam: %dth proc involved will be assigned %ld threads.\n",
        i, allocated_tcs[i].no_threads);
  }

  // fill in the distribution in the FC
  fc->distribution.no_reservations = 0;
  fc->distribution.no_generations = mapping->no_ranges_per_tc;
  LOG(DEBUG, "allocate_fam: no_generations = %ld\n", fc->distribution.no_generations);
  unsigned long total_no_threads_per_gen = 0;
  for (int i = 0; i <= last_allocated_proc_index; ++i) {
    if (allocated_tcs[i].no_allocated_tcs == 0) continue;
    fc->distribution.no_reservations++;
    proc_reservation* res = &fc->distribution.reservations[fc->distribution.no_reservations-1];    
    res->no_tcs   = allocated_tcs[i].no_allocated_tcs;
    res->first_tc = allocated_tcs[i].first_tc;
    res->last_tc  = allocated_tcs[i].last_tc;
    res->no_threads_per_generation = allocated_tcs[i].no_threads / mapping->no_ranges_per_tc;
    assert(res->no_threads_per_generation > 0);
    total_no_threads_per_gen += res->no_threads_per_generation;
    res->no_threads_per_generation_last = res->no_threads_per_generation 
                                  + (allocated_tcs[i].no_threads % mapping->no_ranges_per_tc);
    assert(res->no_threads_per_generation_last > 0);

    LOG(DEBUG, "allocate_fam: created %dth reservation. no_threads_per_generation = %ld,"
               " no_threads_per_generation_last = %ld (total threads per TC = %ld)\n", 
               i, res->no_threads_per_generation, res->no_threads_per_generation_last,
               allocated_tcs[i].no_threads);
  }
  //fc->distribution.start_index = start_index;
  fc->distribution.start_index_last_generation = ((mapping->no_ranges_per_tc - 1) * total_no_threads_per_gen);
    //start_index + ((mapping->no_ranges_per_tc - 1) * total_no_threads_per_gen);
  LOG(DEBUG, "allocate_fam: computed start_index_last_generation = %ld\n", fc->distribution.start_index_last_generation);

  return fc;



/*
  //find TC's
  long total_threads = (end_index - start_index + 1) / step;
  long allocated_threads = 0;

  typedef struct allocated_tcs_t {
    int allocated_tcs;
    int tcs[MAX_NO_TCS_PER_ALLOCATION];
    int no_threads; 
  }allocated_tcs_t;

  allocated_tcs_t allocated_tcs[mapping->no_proc_assignments];
  unsigned int load_to_redistribute = 0;
  unsigned int allocated_procs = 0;
  int last_allocated_proc_index = -1;

  for (i = 0; i < mapping->no_proc_assignments; ++i) {
    proc_assignment as = mapping->proc_assignments[i];
    assert(as.no_tcs <= MAX_NO_TCS_PER_ALLOCATION);
    if (as.node_index == NODE_INDEX) {
      LOG(DEBUG, "allocate_fam: requesting %d local TC's\n", as.no_tcs);
      allocate_local_tcs(as.proc_index, as.no_tcs, allocated_tcs[i].tcs, &allocated_tcs[i].allocated_tcs);
    } else {
      LOG(DEBUG, "allocate_fam: requesting %d remote TC's\n", as.no_tcs);
      allocate_remote_tcs(as.node_index, as.proc_index, as.no_tcs, 
                          allocated_tcs[i].tcs, 
                          &allocated_tcs[i].allocated_tcs);
      LOG(DEBUG, "allocate_fam: back from remote allocation\n");
    }

    if (allocated_tcs[i].allocated_tcs == 0) {
      // redistribute the load assigned to this proc
      load_to_redistribute += as.load_percentage;
      LOG(DEBUG, "failed to allocate TC's on node %d processor %d\n", 
          NODE_INDEX, as.proc_index);
    } else {
      last_allocated_proc_index = i;
      ++allocated_procs;
      LOG(DEBUG, "allocated %d TC's on node %d processor %d\n", 
          allocated_tcs[i].allocated_tcs, NODE_INDEX, as.proc_index);
    }
  }

  // if no TC's could be allocated anywhere, return failure
  if (last_allocated_proc_index == -1) {
    // free the allocated family context
    free_fc(fc->index / NO_FAM_CONTEXTS_PER_PROC, fc->index % NO_FAM_CONTEXTS_PER_PROC);
    return NULL;
  }

  LOG(DEBUG, "finished allocating resources. Load to redistribute: %d\%\n", 
      load_to_redistribute);

  // redistribute the load of the procs where we couldn't get any TC's and
  // compute the number of threads that go to each proc
  int additional_load = load_to_redistribute / allocated_procs;
  int additional_load_last = load_to_redistribute % allocated_procs;
  for (int i = 0; i <= last_allocated_proc_index; ++i) {
    if (allocated_tcs[i].allocated_tcs == 0) continue;
    proc_assignment as = mapping->proc_assignments[i];
    // compute the load (in percentages) going to this proc
    int load = as.load_percentage + additional_load;
    if (i == last_allocated_proc_index) load += additional_load_last;

    // compute the number of threads going to this proc
    if (i != last_allocated_proc_index) {
      allocated_tcs[i].no_threads = load * total_threads / 100;
      allocated_threads += allocated_tcs[i].no_threads;
    } else {
      allocated_tcs[i].no_threads = total_threads - allocated_threads;
    }
  }

  // fill in the ranges in the FC
  int no_ranges = 0;
  int thread_index = start_index;  // next thread to allocate
  for (int i = 0; i <= last_allocated_proc_index; ++i) {
    if (allocated_tcs[i].allocated_tcs == 0) continue;

    unsigned long threads_for_proc = allocated_tcs[i].no_threads;
    unsigned long threads_for_tc = threads_for_proc / allocated_tcs[i].allocated_tcs;
    unsigned long threads_for_tc_last = threads_for_proc % allocated_tcs[i].allocated_tcs;
    unsigned long ranges_for_tc = mapping->no_ranges_per_tc;
    assert(ranges_for_tc <= threads_for_tc);
    
    for (int j = 0; j < allocated_tcs[i].allocated_tcs; ++j) { 
      fc->ranges[no_ranges].index_start = thread_index;
      fc->ranges[no_ranges].index_end = thread_index + step * (threads_for_tc - 1);
      thread_index = fc->ranges[no_ranges].index_end + step;
      
      fc->ranges[no_ranges].dest.node_index = mapping->proc_assignments[i].node_index;
      fc->ranges[no_ranges].dest.proc_index = mapping->proc_assignments[i].proc_index;
      fc->ranges[no_ranges].dest.tc_index = allocated_tcs[i].tcs[j];
      fc->ranges[no_ranges].dest.tc = 
        (tc_t*)TC_START_VM_EX(mapping->proc_assignments[i].node_index, allocated_tcs[i].tcs[j]);
      
      no_ranges++;
    }
    // add a few threads to the last tc for a proc
    fc->ranges[no_ranges-1].index_end += step * threads_for_tc_last;
  }
  fc->no_ranges = no_ranges;

  return fc;
*/
}

static inline int test_same_node(const tc_ident_t* l, const tc_ident_t* r) {
  return l->node_index == r->node_index;
}
static inline int test_same_proc(const tc_ident_t* l, const tc_ident_t* r) {
  return test_same_node(l,r) && l->proc_index == r->proc_index;
}
static inline int test_same_tc(const tc_ident_t* l,const tc_ident_t* r) {
  return test_same_proc(l, r) && l->tc_index == r->tc_index;
}


/*
 *  Prepares every TC in a chain to run parts of a family. The TC's are also scheduled to run.
 */
void populate_local_tcs(
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
    memdesc_t* final_descs,  // pointer to the descriptor table in the FC (NULL if !final_ranges)
    i_struct* done,          // pointer to done in the FC (NULL if !final_ranges)
    default_place_policy_enum default_place_policy,// policy to be used when deciding the 
                                                   // PLACE_DEFAULT to be inheritied by
                                                   // the child
    sl_place_t default_place_parent     // PLACE_DEFAULT of the parent. Used if 
                                        // default_place_policy == INHERIT_DEFAULT_PLACE
     
    ) {

  unsigned long tc_threads_per_gen = no_threads_per_generation / no_tcs;
  unsigned long tc_threads_per_gen_last_tc = tc_threads_per_gen + (no_threads_per_generation % no_tcs);
  unsigned long tc_threads_last_gen = no_threads_per_generation_last / no_tcs;
  unsigned long tc_threads_last_gen_last_tc = tc_threads_last_gen + (no_threads_per_generation_last % no_tcs);

  LOG(DEBUG, "populate_local_tcs: no_tcs = %d, no_generations = %d, start_index = %ld, start_index_last_gen = %ld, "
              "tc_threads_per_gen = %ld, tc_threads_per_gen_last_tc = %ld, tc_threads_last_gen = %ld, "
              "tc_threads_last_gen_last_tc = %ld\n",
              no_tcs, no_generations, start_index, start_index_last_generation, 
              tc_threads_per_gen, tc_threads_per_gen_last_tc, tc_threads_last_gen,
              tc_threads_last_gen_last_tc);

  populate_tc(first_tc.tc, 
              func,
              no_generations,
              tc_threads_per_gen,
              tc_threads_per_gen_last_tc,
              tc_threads_last_gen,
              tc_threads_last_gen_last_tc,
              gap_between_generations,
              start_index,
              start_index_last_generation,
              denormalized_fam_start_index,
              step,
              parent, prev, next,
              is_first_proc_on_fam,
              is_last_proc_on_fam,
              final_shareds,
              final_descs,
              done,
              _cur_tc != NULL ? &_cur_tc->ident : NULL,
              default_place_policy,
              default_place_parent
              );
}

/* 
 * Populates and unblocks all the tc-s that have been assigned chunks of the family.
 * Returns an identifier of the first tc to service the family.
*/
tc_ident_t create_fam(fam_context_t* fc,
                      thread_func func,
                      long real_fam_start_index,
                      long step,
                      default_place_policy_enum default_place_policy
                      ) {
  const fam_distribution* dist = &fc->distribution;
  unsigned long total_threads_per_generation = 0;  // across all procs/tcs...
  for (unsigned int i = 0; i < dist->no_reservations; ++i) {
    total_threads_per_generation += dist->reservations[i].no_threads_per_generation;
  }
  LOG(DEBUG, "create_fam: distribution: threads_per_gen = %ld, start_index_last_gen = %ld\n",
      total_threads_per_generation, dist->start_index_last_generation);

  size_t no_res = dist->no_reservations;
  unsigned long first_thread_on_proc = 0;
  unsigned long first_thread_on_proc_last_gen = fc->distribution.start_index_last_generation;
  for (size_t i = 0; i < dist->no_reservations; ++i) {
    const proc_reservation* res = &dist->reservations[i];
   
    if (res->first_tc.node_index == (signed)NODE_INDEX) {
      if (_cur_tc != NULL) {  // this will be NULL when creating root_fam
        populate_local_tcs(func,
                         res->no_tcs,
                         (i == 0),  // is_first_proc_on_fam
                         (i == dist->no_reservations - 1),  // is_last_proc_on_fam
                         dist->no_generations,
                         res->no_threads_per_generation,
                         res->no_threads_per_generation_last,
                         total_threads_per_generation,  // gap_between_generations
                         first_thread_on_proc,
                         first_thread_on_proc_last_gen,
                         //dist->start_index,
                         //dist->start_index_last_generation,
                         real_fam_start_index,  // the real start index of the family. Used to de-normalize start_index

                         step,
                         res->first_tc,    // first_tc
                         _cur_tc->ident,   // parent
                         i > 0 ? dist->reservations[i-1].last_tc : dist->reservations[no_res-1].last_tc,  // prev
                         i < no_res - 1 ? dist->reservations[i+1].first_tc : dist->reservations[0].first_tc, //next
                         fc->shareds,  // final_shareds
                         fc->shared_descs,  //final_descs
                         &fc->done,         // done istruct
                         default_place_policy,
                         _cur_tc->place_default   // default_place_parent
                         );
      } else {  // creating root_fam
        assert(func == &__slFfmta___root_fam_metaloop);
        assert(dist->no_reservations == 1);
        assert(res->no_tcs == 1);
        
        tc_ident_t dummy_parent;
        // set up a dummy parent; it needs to point to the same node, but different proc and different
        // TC than the reader, because we want the reader to use read_istruct_different_proc
        dummy_parent.node_index = NODE_INDEX; // no parent
        dummy_parent.proc_index = -1; dummy_parent.tc_index = -1; dummy_parent.tc = NULL; 
        sl_place_t whole_cluster = {0,0,-1,-1,-1};

        populate_local_tcs(
            func, 
            res->no_tcs,
            true,  // is_first_proc_on_fam
            true,  // is_last_proc_on_fam
            1,     // no_generations
            1,     // no_threads_per_generation
            1,     // no_threads_per_generation_last
            1,     // gap between generations
            0,     // start_index
            0,     // start_index_last_generation
            0,     // denormalize_fam_start_index
            1,     // step
            res->first_tc,  // first_tc 
            dummy_parent,  
            dummy_parent, 
            dummy_parent,
            fc->shareds,
            fc->shared_descs,
            &fc->done,
            INHERIT_DEFAULT_PLACE,
            whole_cluster);
      }
    } else {
      assert(_cur_tc != NULL);  // this should only be null for root_fam, and that shouldn't be allocated remotely
      unsigned int node_index = res->first_tc.node_index;
      LOG(DEBUG, "create_fam: sending remote create request to node %d\n", node_index);
      populate_remote_tcs(
          node_index,
          func,
          res->no_tcs,
          (i == 0),  // is_first_proc_on_fam
          (i == dist->no_reservations - 1),  // is_last_proc_on_fam
          dist->no_generations,
          res->no_threads_per_generation,
          res->no_threads_per_generation_last,
          total_threads_per_generation,  // gap_between_generations
          first_thread_on_proc,
          first_thread_on_proc_last_gen,
          //dist->start_index,
          //dist->start_index_last_generation,
          real_fam_start_index,  // the real start index of the family. Used to de-normalize start_index

          step,
          res->first_tc,    // first_tc
          _cur_tc->ident,   // parent
          i > 0 ? dist->reservations[i-1].last_tc : dist->reservations[no_res-1].last_tc,  // prev
          i < no_res - 1 ? dist->reservations[i+1].first_tc : dist->reservations[0].first_tc, //next
          fc->shareds,  // final_shareds
          fc->shared_descs,  //final_descs
          &fc->done,         // done istruct
          default_place_policy,
          _cur_tc->place_default   // default_place_parent
          );
      LOG(DEBUG, "create_fam: remote create request sent\n");
    }
    
    // first range on next TC starts from where this TC left off
    first_thread_on_proc += res->no_threads_per_generation;
    first_thread_on_proc_last_gen += res->no_threads_per_generation_last;

  }  // for i in reservations
  return dist->reservations[0].first_tc;
}

long sync_fam(fam_context_t* fc, /*long* shareds_dest,*/ int no_shareds, ...) {
  //int same_proc = test_same_proc(&_cur_tc->ident, &fc->ranges[fc->no_ranges-1].dest);
  bool same_proc = 
    test_same_proc(
        &_cur_tc->ident, 
        &fc->distribution.reservations[fc->distribution.no_reservations-1].last_tc);
  assert(
      !test_same_tc(
        &_cur_tc->ident, 
        &fc->distribution.reservations[fc->distribution.no_reservations-1].last_tc));  // parent and
                                                                        // child should never be in the same tc
  LOG(DEBUG, "sync_fam: tc %d proc %d syncing on istruct %p\n", 
      _cur_tc->ident.tc_index, _cur_tc->ident.proc_index, &fc->done);
  suspend_on_istruct(&fc->done, same_proc);
  // copy the shareds to the parent
  int i;
  va_list ap;
  va_start(ap, no_shareds);
  for (i = 0; i < no_shareds; ++i) {
    *(va_arg(ap, long*)) = fc->shareds[i].data; //fc->shareds[i].data;
    //shareds_dest[i] = fc->shareds[i].data;
  }
  va_end(ap);
  //fc->empty = 1;  // mark the FC as reusable
  assert(_cur_tc->ident.proc_index == (signed)(fc->index / NO_FAM_CONTEXTS_PER_PROC));  // sanity check; a FC should be used only
                                                // on it's processor
  free_fc(fc->index / NO_FAM_CONTEXTS_PER_PROC, fc->index % NO_FAM_CONTEXTS_PER_PROC);
  return 0;  // TODO: what exactly is this return code supposed to be?
}

void mmap_tc_ctrl_struct(int tc_index) {
  void* addr = TC_START_VM(tc_index);
  assert(!((unsigned long)addr % getpagesize()));  // must be page-aligned
  int no_pages = sizeof(tc_t) / getpagesize();
  ++no_pages;
  void* mapping = mmap(addr, no_pages * getpagesize(), PROT_READ | PROT_WRITE,
                       MAP_PRIVATE|MAP_FIXED|MAP_ANON, -1, 0);
  if (mapping == MAP_FAILED) {
    perror("mmap"); exit(1);
  }
  if (tc_index % 1024 == 0)
    LOG(DEBUG, "mapped %d pages starting from %p for a TC control structure\n",
      no_pages, addr);
}

void run_tc(unsigned int processor_index, tc_t* tc) {
  //int jumped = 0;
  assert(tc->ident.proc_index == (signed)processor_index);

  _cur_tc = tc;
  LOG(DEBUG, "run_tc: jumping to user code. proc_index = %d\n", processor_index);
  swapcontext(&_processor[processor_index].scheduler_context, &tc->context);
  LOG(DEBUG, "run_tc: back from user code. proc_index = %d\n", processor_index);

  //return to the scheduler
  return;
}


void* run_processor(void* processor_index) {
  LOG(INFO, "Starting processor %ld\n", (unsigned long)processor_index);
  int pid = (unsigned long)processor_index;
  while (1) {
    pthread_spin_lock((pthread_spinlock_t*)&runnable_queue_locks[pid]);
    while (runnable_count[pid] == 0) {
      pthread_spin_unlock((pthread_spinlock_t*)&runnable_queue_locks[pid]);
      usleep(1000);
      pthread_spin_lock((pthread_spinlock_t*)&runnable_queue_locks[pid]);
    }
    // TODO: implement a proper queue for these and see how to do without locking for
    // extracting
    int tc_index = grab_runnable_tc(pid);
    assert(tc_index != -1);
    tc_t* tc = (tc_t*)TC_START_VM(tc_index); //runnable_tcs[pid][runnable_count[pid] - 1];
    /*
    int i;
    for (i = 0; i < runnable_count[pid] - 1; ++i) {
      runnable_tcs[pid][i] = runnable_tcs[pid][i+1];
    }
    */
    //--runnable_count[pid];
    pthread_spin_unlock((pthread_spinlock_t*)&runnable_queue_locks[pid]);

    // run tc
    LOG(DEBUG, "run_processor: found runnable TC (%d)\t proc index = %d\n", tc->ident.tc_index, processor_index);
    run_tc(pid, tc);
  }
}

/*
 * Map vm for a processor stack. The stacks are located at the end of the runtime's vm, with
 * the stack for the first processor located at lower addresses, then the stack for the 2nd processor,
 * and so on.
 */
void* mmap_processor_stack(int processor_index) {
  void* low_addr = RT_END_VM - (NO_PROCS - processor_index)*PROCESSOR_PTHREAD_STACK_SIZE + 1;
  assert(((unsigned long)low_addr % getpagesize()) == 0);
  void* mapping = mmap(low_addr, PROCESSOR_PTHREAD_STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE|MAP_FIXED|MAP_ANON, -1, 0);
  //TODO: add a guard page between stacks
  if (mapping == MAP_FAILED) handle_error("mmap");
  assert(mapping == low_addr);
  return low_addr;
}


// TODO: find a better way to allocate a stack for this thread
static const int delegation_if_stack_size = 1 << 20;
static char delegation_if_stack[1<<20];


/*
 * Map a stack for the pthread handling the delegation interface.
 */
void* mmap_delegation_interface_stack(size_t* size) {
  *size = delegation_if_stack_size;
  return delegation_if_stack;
}

static int get_no_CPUs() {
  int res = sysconf(_SC_NPROCESSORS_ONLN);
  if (res < 1) {
    LOG(WARNING, "couldn't read the number of CPUs. Running with only 1.\n");
    return 1;
  } else {
    LOG(INFO, "running on %d CPUs\n", res);
  }
  return res;
}

static void init_processors() {
  unsigned long i;
  for (i = 0; i < (unsigned)NO_PROCS; ++i) {
    pthread_attr_init(&threads_attr[i]);
    void* stack_low_addr = mmap_processor_stack(i);
    if (pthread_attr_setstack(&threads_attr[i], stack_low_addr, PROCESSOR_PTHREAD_STACK_SIZE) != 0) {
      perror("attr_setstack"); exit(1);
    }
    if (pthread_create(&threads[i], &threads_attr[i], run_processor, (void*)i)) {
      perror("init_processors:"); exit(1);
    }
  }
}

static void create_tc(int proc_index, int tc_index);

static void rt_init() {
  /* sanity checks */
  assert(VM_PER_NODE >= (NO_TCS_PER_PROC * MAX_PROCS_PER_NODE * VM_PER_TC + VM_PER_TC));
  assert(RT_START_VM >= NODE_START_VM);
  assert(NODE_START_VM + VM_PER_NODE > (void*)TC_START_VM(NO_TCS_PER_PROC * MAX_PROCS_PER_NODE));
  // assert there's enough space for the processor stacks
  unsigned long x = 
      ( ((unsigned long long)(MAX_PROCS_PER_NODE)) * PROCESSOR_PTHREAD_STACK_SIZE);
  assert(((unsigned long long)RT_END_VM - (unsigned long long)RT_START_VM) >= x);
  
  //check that the lock within a padded lock is placed at the beginning of the union. We care, since we
  //cast the union to the lock.
  assert((void*)&allocate_tc_locks[0].lock == (void*)&allocate_tc_locks[0].c[0]);
  assert((void*)&allocate_tc_locks[0].lock == (void*)&allocate_tc_locks[0]);

  // check that the descriptors in the tables in FC's are properly aligned
  assert(((unsigned long)(&fam_contexts[0][0].shared_descs[0])) % 32 == 0);
  assert(((unsigned long)(&fam_contexts[0][1].shared_descs[0])) % 32 == 0);

  //init_network();
  init_mem_comm();

  // init runnable_queue_locks
  unsigned int i, j;
  for (i = 0; i < NO_PROCS; ++i) {
    if (pthread_spin_init((pthread_spinlock_t*)&runnable_queue_locks[i], PTHREAD_PROCESS_PRIVATE) != 0) {
      perror("pthread_spin_init:"); exit(1);
    }
    if (pthread_spin_init((pthread_spinlock_t*)&fam_contexts_locks[i], PTHREAD_PROCESS_PRIVATE) != 0) {
      perror("pthread_spin_init:"); exit(1);
    }
    if (pthread_spin_init(((pthread_spinlock_t*)&allocate_tc_locks[i]), PTHREAD_PROCESS_PRIVATE) != 0) {
      perror("pthread_spin_init:"); exit(1);
    }
  }
 
  /*
  LOG(DEBUG, "this node will skip %d TC's due to addr space holes.\n", no_tc_holes);
  for (int j = 0; j < no_tc_holes; ++j) {
    LOG(DEBUG, "addr space hole: %d\n", j);
  }
  */

  // init TCs
  void* a,*b;
  a = TC_START_VM(0);
  b = TC_START_VM((NO_PROCS * NO_TCS_PER_PROC));
  LOG(DEBUG, "initializing %d TC's in address space from %p to %p\n", 
      NO_PROCS*NO_TCS_PER_PROC, a, b);
//      TC_START_VM(0), TC_START_VM((NO_PROCS * NO_TCS_PER_PROC)));
  for (i = 0; i < NO_PROCS * NO_TCS_PER_PROC; ++i) {
    // check if we should skip this TC due to a hole in the vm
    int skip = 0;
    for (unsigned int j = 0; j < no_tc_holes; ++j) {
      if (tc_holes[j] == i) {
        skip = 1;
        tc_valid[i] = 0;
        LOG(INFO, "skipping creating TC %d because of a hole in the virtual memory\n", i);
        break;
      }
    }
    if (skip) continue;

    // mmap TC control structures
    mmap_tc_ctrl_struct(i);
    if (i == 1024) {
      LOG(DEBUG, "mmaped TC 1024\n");
    }

    // init tc fields
    tc_valid[i] = 1;
    create_tc(i / NO_TCS_PER_PROC, i);
  }

  //init fam_contexts
  int fc_index = 0;
  for (i = 0; i < NO_PROCS; ++i) {
    for (j = 0; j < NO_FAM_CONTEXTS_PER_PROC; ++j) {
      if (i == 1 && j == 5) 
        LOG(DEBUG, "0 initializing fam_contexts[%d][%d] - > %x.\n", i, j, &fam_contexts[i][j]);
      fam_contexts[i][j].empty = 1;
      //LOG(DEBUG, "1 initializing fam_contexts[%d][%d].\n", i, j);
      fam_contexts[i][j].done.state = EMPTY;
      fam_contexts[i][j].index = fc_index++;
      free_fc(i, j);  // insert FC in free list
      if (pthread_spin_init(&fam_contexts[i][j].done.lock, PTHREAD_PROCESS_PRIVATE) != 0) {
        perror("pthread_spin_init:"); exit(1);
      }
      for (int k = 0; k < MAX_ARGS_PER_FAM; ++k) {
        if (pthread_spin_init(&fam_contexts[i][j].shareds[k].lock, PTHREAD_PROCESS_PRIVATE) != 0) 
          handle_error("pthread_init");
      }
    }
  }

  // init processors
  init_processors();

  pthread_spin_lock(&rt_init_done_lock);
  rt_init_done = 1;
  pthread_spin_unlock(&rt_init_done_lock);
}


static inline tc_ident_t get_current_context_ident() {
  // computing the current context based on the stack pointer doesn't work reliably for
  // the network thread, in it's current form (where is that stack allocated?), so I've replaced
  // the mechanism to one based on _cur_tc.
  if (_cur_tc != NULL) {
    return _cur_tc->ident;
  } else {
    tc_ident_t dummy;
    dummy.node_index = NODE_INDEX; dummy.proc_index = -1; dummy.tc_index = -1;
    return dummy;
  }
  /*
  tc_ident_t ident;
  unsigned long sp;
  __asm__ __volatile__ ("movq %%rsp, %0\n" : "=r" (sp));
  unsigned long node_vm_start =
    sp & ((0xffffffffffffffffL >> VM_BITS_PER_NODE) << VM_BITS_PER_NODE);
  ident.node_index = node_vm_start >> VM_BITS_PER_NODE;
  ident.tc = (void*)
    (sp & ((0xffffffffffffffffL >> VM_BITS_PER_TC) << VM_BITS_PER_TC));

  //ident.tc_index = ((unsigned long)ident.tc - node_vm_start) / VM_PER_TC;
  ident.tc_index = ((unsigned long)ident.tc - (unsigned long)TC_START_VM(0)) / VM_PER_TC;

  ident.proc_index = ident.tc_index / NO_TCS_PER_PROC;
  return ident;
  */
}

void idle() {
  while(1) {}
}

stack_t alloc_stack(int tc_index, int size) {
  void* stack_highaddr = (void*)TC_START_VM(tc_index + 1) - 1;  // start just before the next TC
  int num_pages = size / getpagesize();
  if (size % getpagesize()) num_pages++;
  size = num_pages * getpagesize();
  void* stack_lowaddr = stack_highaddr - size + 1;
  void* mapping =
    mmap(stack_lowaddr, size, PROT_READ | PROT_WRITE,
         MAP_PRIVATE|MAP_FIXED|MAP_ANON| MAP_GROWSDOWN
    , -1, 0);
  if (mapping == MAP_FAILED) {
    perror("alloc_stack"); exit(1);
  }
  assert(mapping == stack_lowaddr);
  LOG(DEBUG + 1, "mapped %d pages starting from %p -> %p for a TC stack\n",
      num_pages, stack_lowaddr, stack_highaddr);
  stack_t stack;
  stack.ss_sp = stack_lowaddr;
  stack.ss_flags = 0;
  stack.ss_size = size;

  return stack;
}

heap_t alloc_heap(int tc_index, int size) {
  void* heap_lowaddr = (void*) TC_START_VM(tc_index) + VM_TC_CONTROL_STRUCTURES;
  int num_pages = size / getpagesize();
  if (size % getpagesize()) num_pages++;
  size = num_pages * getpagesize();
  void* mapping =
    mmap(heap_lowaddr, size, 0, MAP_PRIVATE|MAP_FIXED|MAP_ANON, -1, 0);
  if (mapping == MAP_FAILED) {
    perror("alloc_heap"); exit(1);
  }
  assert(mapping == heap_lowaddr);
  heap_t heap;
  heap.lowaddr = heap_lowaddr;
  heap.size = size;
  return heap;
}

void create_tc(int proc_index, int tc_index) {
  tc_t* tc = (tc_t*)TC_START_VM(tc_index);//&(TCS[tc_index];
  //get context
  //ucontext_t contextt;
  //getcontext(&contextt);
  getcontext(&tc->context);

  //allocate stack
  tc->initial_thread_stack = alloc_stack(tc_index, INIT_TC_STACK_SIZE);
  tc->context.uc_stack = tc->initial_thread_stack;
  tc->context.uc_link = NULL;

  //allocate heap
  tc->heap = alloc_heap(tc_index, INIT_TC_HEAP_SIZE);

  //makecontext
  makecontext(&(tc->context), idle, 0);

  //write ->ident
  tc->ident.node_index = NODE_INDEX;
  tc->ident.tc = tc;
  tc->ident.tc_index = tc_index;
  tc->ident.proc_index = tc_index / NO_TCS_PER_PROC;

  // init shared_locks and global_locks
  int i;
  for (i = 0; i < MAX_ARGS_PER_FAM; ++i) {
    for (int j = 0; j < 2; ++j) {
      if (pthread_spin_init(&tc->shareds[j][i].lock, PTHREAD_PROCESS_PRIVATE) != 0) {
        perror("pthread_spin_init:"); exit(1);
      }
    }
    if (pthread_spin_init(&tc->globals[i].lock, PTHREAD_PROCESS_PRIVATE) != 0) {
      perror("pthread_spin_init:"); exit(1);
    }
  }

  // init lock for prev_range_done istruct
  if (pthread_spin_init(&tc->prev_range_done.lock, PTHREAD_PROCESS_PRIVATE) != 0) handle_error("pthread_spin_init:");

  // init PLACE_LOCAL
  tc->place_local.node_index = NODE_INDEX;
  tc->place_local.proc_index = proc_index;
  tc->place_local.tc_index = tc_index;
  tc->place_local.place_local = tc->place_local.place_default = 0;

  //tc->blocked = -1; // empty
  //tc->finished = 1;  // available for reuse
  _free_tc(proc_index, tc_index);
}

sl_place_t resolve_place_default_for_TC(
    default_place_policy_enum default_place_policy,// policy to be used when deciding the 
                                                   // PLACE_DEFAULT to be inheritied by
                                                   // the child
    sl_place_t default_place_parent,     // PLACE_DEFAULT of the parent. Used if 
                                        // default_place_policy == INHERIT_DEFAULT_PLACE
    tc_ident_t tc            // the TC for which we're resolving the place
    ) {

  sl_place_t def_place;
  def_place.place_local = def_place.place_default = 0;
  def_place.node_index = def_place.proc_index = def_place.tc_index = -1;
  switch (default_place_policy) {
    case INHERIT_DEFAULT_PLACE:
      def_place = default_place_parent;
      break;
    case LOCAL_NODE:
      def_place.node_index = NODE_INDEX;
      break;
    case LOCAL_PROC:
      def_place.node_index = NODE_INDEX;
      def_place.proc_index = tc.proc_index;
      break;
    case LOCAL_TC:
      def_place.node_index = NODE_INDEX;
      def_place.proc_index = tc.proc_index;
      def_place.tc_index   = tc.tc_index;
      break;
  }
  restrict_place(&def_place, default_place_parent);  // make sure that the children's PLACE_DEFAULT is capped
                                                     // by the parent's PLACE_DEFAULT
  return def_place;  
}

/* 
 * Recursively prepares every TC in a chain to run parts of a family. The TC's are also scheduled to run.
 */
static void populate_tc(
                 tc_t* tc,
                 thread_func func,

                 unsigned long no_generations, 
                 unsigned long no_threads_per_gen,  // for this TC
                 unsigned long no_threads_per_gen_last_tc,  // for last TC on proc
                 unsigned long no_threads_last_gen,  // for this TC
                 unsigned long no_threads_last_gen_last_tc,  // for last TC on proc

                 long gap_between_generations,
                 long start_index,
                 long start_index_last_generation,
                 
                 long denormalized_fam_start_index,
                 long step,

                 tc_ident_t parent,
                 tc_ident_t prev,  // the TC that is going to run the ranges just before the first TC in this chain
                 tc_ident_t next,  // the TC that is going to run the ranges just after the first TC in this chain
                 bool is_first_proc_on_fam,  // will be used by the first tc on the proc
                 bool is_last_proc_on_fam,   // will be used by the last tc on the proc
                 
                 i_struct* final_shareds, 
                 memdesc_t* final_descs,
                 i_struct* done,
                 const tc_ident_t* current_tc, // the TC of the caller; can be passed a dummy or NULL
                 default_place_policy_enum default_place_policy,// policy to be used when deciding the 
                                                                // PLACE_DEFAULT to be inheritied by
                                                                // the child
                 sl_place_t default_place_parent     // PLACE_DEFAULT of the parent. Used if 
                                                     // default_place_policy == INHERIT_DEFAULT_PLACE
                 ) {

  tc->context.uc_stack = tc->initial_thread_stack;  // TODO: is this necessary? would this have been modified
                                                    // by savecontext() calls?
  tc->context.uc_link = NULL;
  makecontext(&(tc->context), (void (*)())func, 0);

  tc->parent_ident = parent;
  if (tc->is_first_tc_on_proc) {
    tc->prev = prev;
  }
  if (tc->is_last_tc_on_proc) {
    tc->next = next;
  }
  // .prev and .next for TC's which are not the first or the last on their proc were initialized at allocation

  if (tc->is_last_tc_on_proc && is_last_proc_on_fam) {
    tc->is_last_tc_in_fam = true;
    tc->final_shareds = final_shareds;
    tc->final_descs = final_descs;
    tc->done = done;
  } else {
    tc->is_last_tc_in_fam = false;
    tc->final_shareds = NULL;
    tc->final_descs = NULL;
    tc->done = NULL;
  }

  if (tc->is_first_tc_on_proc && is_first_proc_on_fam) {
    tc->is_first_tc_in_fam = true;
    tc->prev_range_done.state = WRITTEN;
    tc->prev_range_done.data = 1;
  } else {
    tc->is_first_tc_in_fam = false;
  }

  tc->no_generations_left = no_generations;
  LOG(DEBUG, "populate_tc: no_generations = %ld\n", no_generations);

  tc->no_threads_per_generation = tc->is_last_tc_on_proc ?
                                  no_threads_per_gen_last_tc : no_threads_per_gen;
  tc->no_threads_per_generation_last = tc->is_last_tc_on_proc ?
                                  no_threads_last_gen_last_tc :
                                  no_threads_last_gen;

  tc->gap_between_generations = gap_between_generations;
  tc->start_index = start_index;
  tc->start_index_last_generation = start_index_last_generation;

  LOG(DEBUG, "populate_tc: tc=%d: Finished setting up this TC: "
      "no_threads_per_gen = %ld, no_threads_per_gen_last = %ld, start_index = %ld, start_index_last_gen = %ld\n",
      tc->ident.tc_index, tc->no_threads_per_generation, tc->no_threads_per_generation_last,
      start_index, start_index_last_generation);

  tc->denormalized_fam_start_index = denormalized_fam_start_index;
  tc->step = step;

  tc->current_generation = 0;
  tc->current_generation_real = 0;

  tc->place_default = resolve_place_default_for_TC(default_place_policy, default_place_parent, tc->ident);

  tc->index_start = no_generations > 1 ? start_index : start_index_last_generation;
  tc->index_stop  = no_generations > 1 ? start_index + tc->no_threads_per_generation - 1
                                       : start_index_last_generation + tc->no_threads_per_generation_last - 1;

  // recursively populate next tc on this proc
  if (!tc->is_last_tc_on_proc) {
    assert(tc->next.tc->is_first_tc_on_proc == false);
    populate_tc(tc->next.tc,
                func,
                no_generations,
                no_threads_per_gen,  // for this TC
                no_threads_per_gen_last_tc,  // for last TC on proc
                no_threads_last_gen,  // for this TC
                no_threads_last_gen_last_tc,  // for last TC on proc
                gap_between_generations,
                start_index + no_threads_per_gen,
                start_index_last_generation + no_threads_last_gen,
                denormalized_fam_start_index,
                step,
                parent, prev, next,
                is_first_proc_on_fam,
                is_last_proc_on_fam,
                final_shareds,
                final_descs,
                done,
                current_tc,
                default_place_policy,
                default_place_parent
                );
  }

  unblock_tc(tc, current_tc != NULL ? test_same_proc(&tc->ident, current_tc) : 0);
}


/* writer and reader are running on the same proc, but on different TCs.
   potentially_blocked_tc is the TC that might be suspended on reading this istruct.
   In the current implementation, there's no reason while the istruct needs to be volatile in this
   case, as it's not read twice.
 */
void write_istruct_same_proc(
    volatile i_struct* istructp,
    long val,
    tc_t* potentially_blocked_tc) {
  assert(istructp->state != WRITTEN);
  
  istructp->data = val;
  int unblock_needed = (istructp->state == SUSPENDED);
  LOG(DEBUG + 1, "write_istruct_same_proc: state of the istruct %p is found to be %d\n",
      istructp, istructp->state);
  istructp->state = WRITTEN;
  if (unblock_needed) {
    LOG(DEBUG + 1, "write_istruct_same_proc: unblock needed\n");
    unblock_tc(potentially_blocked_tc, 1 /*same proc*/);
  } else {
    LOG(DEBUG + 1, "write_istruct_same_proc: unblock not needed\n");
  }
}

/* Reader and writer run on different procs */
extern void write_istruct_different_proc(
    volatile i_struct* istructp,
    long val,
    tc_t* potentially_blocked_tc) {
  istructp->data = val;
  enum istruct_state istruct = ld_acq_istruct(istructp);
  assert(istruct != WRITTEN);
  if (istruct == SUSPENDED) {
    // no locking necessary
    st_rel_istruct(istructp, WRITTEN);
    unblock_tc(potentially_blocked_tc,
               0 /*different processor*/);
  } else {
    // the target tc is either running or blocked on a different istruct or smth else => locking is needed
    pthread_spin_lock(&istructp->lock);
    istruct = ld_acq_istruct(istructp);
    int must_unblock = (istruct == SUSPENDED);
    st_rel_istruct(istructp, WRITTEN);
    pthread_spin_unlock(&istructp->lock);
    if (must_unblock) {
      unblock_tc(potentially_blocked_tc, 0 /*different processor*/);
    }
  }
}

/* 
 * wrapper function for reading an istruct when we don't know whether the reader and the writer
 * are running on the same proc or TC.
 * reading_tc corresponds to the TC that will (or already has) read the istruct
 */
void write_istruct(//i_struct_fat_pointer istruct, 
                   unsigned int node_index,  // destination node, if we're writing a remote istruct
                   volatile i_struct* istructp, 
                   long val, 
                   const tc_ident_t* reading_tc//,
                   //int is_mem
                   ) {
  //LOG(DEBUG, "write_istruct: writing istruct %p (my tc:%d)\n", istructp, _cur_tc->ident.tc_index);
  //if (istruct.node_index == NODE_INDEX) {
  if (node_index == NODE_INDEX) {
    //volatile i_struct* istructp;
    tc_ident_t cur_ident = get_current_context_ident();

    //assert(cur_ident.node_index == _cur_tc->ident.node_index);  //TODO: remove these
    //assert(cur_ident.proc_index == _cur_tc->ident.proc_index);
    //assert(cur_ident.tc_index == _cur_tc->ident.tc_index);

    assert(reading_tc->node_index == cur_ident.node_index);  // assume same node, for now
    tc_t* dest_tc = (tc_t*)reading_tc->tc;
    if (reading_tc->tc_index == cur_ident.tc_index) {  // same thread context
      write_istruct_same_tc((i_struct*)istructp, val);  // cast to strip volatile; same TC, no asynchrony
    } else {
      if (reading_tc->proc_index == cur_ident.proc_index) {  // same proc
        LOG(DEBUG + 1, "write_istruct (%p): same_proc\n", istructp);
        write_istruct_same_proc(istructp, val, dest_tc);
      }
      else {  // different proc
        LOG(DEBUG + 1, "write_istruct: different_proc\n");
        write_istruct_different_proc(istructp, val, dest_tc);
      }
    }
  } else {  // writing to different node
    LOG(DEBUG, "write_istruct: writing a remote istruct on node %d\n", node_index);
    write_remote_istruct(node_index, (i_struct*)istructp, val, reading_tc->tc);  // cast to strip volatile
  }
}

/*
 * Writes a shared arg mem for the next sibling thread or the parent. Besides passing along the stub, it also
 * copies the associated descriptor to a different location and updates the stub to point to this new location. 
 */
void write_argmem(unsigned int node_index,
                  volatile i_struct* istructp,
                  memdesc_stub_t stub,
                  memdesc_t* desc_dest,
                  const tc_ident_t* reading_tc) {
  memdesc_t* orig_desc = get_stub_pointer(stub);
  if (!memdesc_desc_local(stub)) {
    pull_desc(get_stub_pointer(stub), &stub, get_stub_pointer(stub));
  }

  if (desc_dest != get_stub_pointer(stub)) {
    assert(memdesc_desc_local(stub));  // TODO: pull the descriptor if it ain't local and remove this assertion
    if (node_index == NODE_INDEX) {
      *desc_dest  = *get_stub_pointer(stub);
    }
    // if the stub is passed to a different node, the descriptor will be initialized in the new location
    // at the time when the remote istructure is written to
    set_stub_pointer(&stub, desc_dest);
  }
  if (node_index != NODE_INDEX) {
    stub.node = node_index;  // we're copying the descriptor over, so it will be local
    write_remote_istruct_mem(node_index, 
                             (i_struct*)istructp, 
                             stub, 
                             orig_desc,
                             1,   // copy over the descriptor
                             reading_tc->tc);  // cast to strip volatile
  } else {
    write_istruct(node_index, istructp, _stub_2_long(stub), reading_tc);//, 1);
  }
}


/*
 * Write an istruct without worrying about concurrency with the reader. This is for very specific use,
 * namely writing the arguments to the root family.
 */
void write_istruct_no_checks(
  i_struct* istructp, long val) {
  istructp->data = val;
  istructp->state = WRITTEN;
}

/* handles same or different proc*/
long read_istruct_different_tc(volatile i_struct* istruct, int same_proc) {
  suspend_on_istruct(istruct, same_proc);
  assert(istruct->state == WRITTEN);
  //istruct->state = EMPTY;
  return istruct->data;
}

long read_istruct(volatile i_struct* istructp, const tc_ident_t* writing_tc) {
  assert(_cur_tc != NULL);
  //LOG(DEBUG, "read_istruct: reading istruct %p (tc:%d)\n", istructp, _cur_tc->ident.tc_index);
  istruct_state state = ld_acq_istruct(istructp);
  if (state == WRITTEN) {  // fast path: the istruct has been filled already
    //istructp->state = EMPTY;
    return istructp->data;
  }

  //both this function and the function called below implement a fast path for when the istruct
  //has already been written. The called functions are also called by the user code directly, so
  //we don't want to remove this double checking.

  if (!test_same_node(writing_tc, &_cur_tc->ident)) {
    //assert(0);
    return read_istruct_different_tc(istructp, 0); 
  }
  if (test_same_tc(&_cur_tc->ident, writing_tc)) {
    return read_istruct_same_tc((i_struct*)istructp);  // cast to strip volatile
  } else {
    return read_istruct_different_tc(istructp, test_same_proc(writing_tc, &_cur_tc->ident));
  }
}

static inline istruct_state read_istruct_state(volatile i_struct* istructp, int same_proc) {
  if (!same_proc) {
    return ld_acq_istruct(istructp);
  } else {
    return istructp->state;
  }
}

static inline void write_istruct_state(volatile i_struct* istructp, int same_proc, istruct_state value) {
  if (!same_proc) {
    st_rel_istruct(istructp, value);
  } else {
    istructp->state = value;
  }
}

/* Reads an istruct and suspends the TC if it is found to the empty.

   same_proc is a hint; pass 1 if the writer is known to be running on the same proc;
   that allows some optimizations (not taking the lock);
 */
void suspend_on_istruct(volatile i_struct* istructp, int same_proc) {
  // fast path
  istruct_state state = read_istruct_state(istructp, same_proc);
  if (state == WRITTEN) {
    return;
  }
  // slow path
  if (!same_proc) {pthread_spin_lock(&istructp->lock);}
  state = read_istruct_state(istructp, same_proc);
  if (state != WRITTEN) {
    write_istruct_state(istructp, same_proc, SUSPENDED);
    LOG(DEBUG, "suspend_on_istruct: suspending tc %d (proc %d) on istruct %p\n", 
        _cur_tc->ident.tc_index, _cur_tc->ident.proc_index, istructp);
    // we sleep
    block_tc_and_unlock(_cur_tc, same_proc ? NULL : &istructp->lock);
    // ... and we wake up
  } else {
    if (!same_proc) pthread_spin_unlock(&istructp->lock);
  }
}

#define PRINT_TC_IDENT(tc) tc.node_index, tc.proc_index, tc.tc_index
#define PRINT_TC_IDENT_FORMAT "N=%d P=%d TC=%d"

// same_processor = 1, if the tc to be unblocked is handled by the same processor as the caller
void unblock_tc(tc_t* tc, int same_processor) {
  struct timeval t;
  gettimeofday(&t, NULL);
  //pthread_spinlock_t* lock;
  LOG(DEBUG, "unblock_tc: unblocking TC ("PRINT_TC_IDENT_FORMAT"). It was blocked for %ld ms.\n", 
      PRINT_TC_IDENT(tc->ident), timediff(t, tc->blocking_time));

  // insert in queue
  int proc_index = tc->ident.proc_index;
  //runnable_tcs[proc_index][runnable_count[proc_index]++] = tc->ident.tc_index;
  mark_tc_as_runnable(proc_index, tc->ident.tc_index, !same_processor);

}

void yield(tc_t* yielding_tc) {
  LOG(DEBUG + 1, "yielding tc\n");
  //getcontext(&yielding_tc->context);
  //setcontext(&processor[yielding_tc->ident.proc_index].scheduler_context);
  swapcontext(&yielding_tc->context,
               &_processor[yielding_tc->ident.proc_index].scheduler_context);
}

void block_tc_and_unlock(tc_t* tc, pthread_spinlock_t* lock) {
  // assumes tc is the current TC
  gettimeofday(&tc->blocking_time, NULL);
  //tc->blocked = 1;
  if (lock != NULL) pthread_spin_unlock(lock);
  // we sleep
  yield(tc);
  // ... and we wake up
}


/*
 * Dequeue a TC from the free list and return it's index. Return -1 if the queue is empty.
 */
static int grab_available_tc(int proc_id) {
  int rez = -1;
  pthread_spin_lock((pthread_spinlock_t*)&allocate_tc_locks[proc_id]);
 
  if (count_available_tcs[proc_id] > 0) {  // if the queue is not empty
    int* start = &start_available_tcs[proc_id];
    rez = available_tcs[proc_id][*start];
    *start = (*start + 1) % NO_TCS_PER_PROC;  
    count_available_tcs[proc_id]--;
    assert(count_available_tcs[proc_id] >= 0);
  }
  pthread_spin_unlock((pthread_spinlock_t*)&allocate_tc_locks[proc_id]);

  if (rez != -1) {
    tc_t* tc = (tc_t*)TC_START_VM(rez);
    //tc->finished = 0;
    for (int j = 0; j < MAX_ARGS_PER_FAM; ++j) {
      tc->shareds[0][j].state = EMPTY;
      tc->shareds[1][j].state = EMPTY;
      tc->globals[j].state = EMPTY;
    }
    tc->prev_range_done.state = EMPTY;
  }
  
  return rez;
}

/*
 * Dequeue a fam_context from the free list and return it's index. Return -1 if the queue is empty.
 */
static int grab_available_fam_context(int proc_id) {
  int rez = -1;
  pthread_spin_lock((pthread_spinlock_t*)&fam_contexts_locks[proc_id]);
 
  if (count_available_fam_contexts[proc_id] > 0) {  // if the queue is not empty
    int* start = &start_available_fam_contexts[proc_id];
    rez = available_fam_contexts[proc_id][*start];
    *start = (*start + 1) % NO_FAM_CONTEXTS_PER_PROC;  
    count_available_fam_contexts[proc_id]--;
    assert(count_available_fam_contexts[proc_id] >= 0);
  }
  pthread_spin_unlock((pthread_spinlock_t*)&fam_contexts_locks[proc_id]);
    
  return rez;
}

/*
 * Dequeue a runnable TC from the runnable list and return it's index. Return -1 if the queue is empty.
 * Attention: function is not thread safe. It must be called with lock runnable_queue_locks[proc_id] held.
 */
static int grab_runnable_tc(int proc_id) {
  int rez = -1;
  //pthread_spin_lock((pthread_spinlock_t*)&runnable_queue_locks[proc_id]);
 
  if (runnable_count[proc_id] > 0) {  // if the queue is not empty
    int* start = &start_runnable_tcs[proc_id];
    rez = runnable_tcs[proc_id][*start];
    *start = (*start + 1) % NO_TCS_PER_PROC;  
    runnable_count[proc_id]--;
    assert(runnable_count[proc_id] >= 0);
  }
  //pthread_spin_unlock((pthread_spinlock_t*)&runnable_queue_locks[proc_id]);
    
  return rez;
}

/*
 * Insert a TC in the free list. Called by code inserted by the compiler at the end of loop functions.
 */
void mark_tc_as_runnable(int proc_id, int tc_id, int lock_needed) {
  if (lock_needed) {
    pthread_spin_lock((pthread_spinlock_t*)&runnable_queue_locks[proc_id]);
  }

  int* end = &end_runnable_tcs[proc_id];
  runnable_tcs[proc_id][*end] = tc_id;
  *end = (*end + 1) % NO_TCS_PER_PROC;  
  runnable_count[proc_id]++;
  assert(runnable_count[proc_id] <= NO_TCS_PER_PROC);

  if (lock_needed) {
    pthread_spin_unlock((pthread_spinlock_t*)&runnable_queue_locks[proc_id]);
  }
}

/*
 * Insert a TC in the free list. Called by code inserted by the compiler at the end of loop functions.
 */
void _free_tc(int proc_id, int tc_id) {
  pthread_spin_lock((pthread_spinlock_t*)&allocate_tc_locks[proc_id]);

  int* end = &end_available_tcs[proc_id];
  available_tcs[proc_id][*end] = tc_id;
  *end = (*end + 1) % NO_TCS_PER_PROC;  
  count_available_tcs[proc_id]++;
  assert(count_available_tcs[proc_id] <= NO_TCS_PER_PROC);

  pthread_spin_unlock((pthread_spinlock_t*)&allocate_tc_locks[proc_id]);
}

/*
 * Insert a FC in the free list. 
 */
static void free_fc(int proc_id, int fc_id) {
  fam_contexts[proc_id][fc_id].empty = 1;  // mark the FC as reusable
  pthread_spin_lock((pthread_spinlock_t*)&fam_contexts_locks[proc_id]);

  int* end = &end_available_fam_contexts[proc_id];
  available_fam_contexts[proc_id][*end] = fc_id;
  *end = (*end + 1) % NO_FAM_CONTEXTS_PER_PROC;  
  count_available_fam_contexts[proc_id]++;
  assert(count_available_fam_contexts[proc_id] <= NO_FAM_CONTEXTS_PER_PROC);

  pthread_spin_unlock((pthread_spinlock_t*)&fam_contexts_locks[proc_id]);
}

void rt_quit() {
  //TODO:
  //assert(0);
  exit(0);
}


static void sighandler_foo(int sig, siginfo_t *si, void *ucontext);


pthread_mutex_t main_finished_mutex;
pthread_cond_t main_finished_cv;  // TODO: do I need to init this?
int main_finished = 0;

void end_main() {
  LOG(DEBUG, "end_main: entering\n");
  pthread_mutex_lock(&main_finished_mutex);
  main_finished = 1;
  pthread_cond_signal(&main_finished_cv);
  pthread_mutex_unlock(&main_finished_mutex);
  _return_to_scheduler();
}


extern int __program_main(int, char**);  // the user program may supply this. If it doesn't, libslmain will
                                         // provide one.



/*
 * This family will be an ancestor to all other families. The runtime will create it as a family of
 * one thread. The compiler will insert a call to end_main() at the end of this thread.
 */
sl_def(__root_fam, void, sl_glparm(int, argc), sl_glparm(char**, argv))
{
  LOG(VERBOSE, "in root_fam; starting...\n");
  int argc = sl_getp(argc);
  char** argv = sl_getp(argv);
  LOG(DEBUG, "in root_fam; calling __program_main\n");
  __program_main(argc, argv);
  LOG(DEBUG, "in root_fam; back from __program_main\n");
}
sl_enddef

typedef struct {
  unsigned long long l, r;
}mem_range;

mem_range mem_ranges[30000];
int no_mem_ranges = 0;

int compare_mem_ranges(const void* l, const void* r) {
  mem_range* a = (mem_range*)l;
  mem_range* b = (mem_range*)r;
  if (a->l < b->l) return -1;
  if (a->l > b->l) return 1;
  return 0; 
}


void parse_mem_map(char* buf, mem_range* mem_ranges, int* no_mem_ranges) {
  LOG(DEBUG, "parsing mem map: %s\n", buf);

  char* saveptr_range;//, *saveptr2;
  char* range = strtok_r(buf, ";", &saveptr_range);
  while (range) {
    long long l,r;
    int res = sscanf(range, "%LX-%LX", &l, &r);
    //LOG(DEBUG, "parse_mem_map: parsing range \"%s\".\t Got %LX - %LX.\n", range, l, r);
    assert(res = 2);

    if (l > 0x7fffffffffffLL) {  // we ignore ranges above 0x7fffffffffff; those belong to the kernel,
                                 // and we are not concerned with those, as we'll never try to mmap memory there
      assert(r > 0x7fffffffffffLL);
    } else {
      assert(r < 0x7fffffffffffLL);
      mem_ranges[*no_mem_ranges].l = l;
      mem_ranges[*no_mem_ranges].r = r;
      ++(*no_mem_ranges);
    }

    range = strtok_r(NULL, ";", &saveptr_range);
  }
}

void parse_own_memory_map(char* map) {
  map[0] = 0;  // so that strcat will work
  int pid = getpid();
  char file[100];
  sprintf(file, "/proc/%d/maps", pid);
  FILE* f = fopen(file, "rt");
  char buf[500];
  int first = 1;
  while (fgets(buf, 500, f)) {
    char* l = strtok(buf, "-");
    assert(l);
    char* r = strtok(NULL, " ");
    assert(r);
    if (!first) strcat(map, ";");
    first = 0;
    strcat(map, l);
    strcat(map, "-");
    strcat(map, r);
  }
  strcat(map, "!");


  //return map;
}


struct slave_addr {
  char addr[500];
  int port_daemon;
};
typedef struct slave_addr slave_addr;
slave_addr slaves[1000];
int no_slaves = -1;


int am_i_primary() {
  // check is slaves.txt file exists
  FILE* fp = fopen("slaves.txt", "rt");
  if (fp) {
    char* val = getenv("PRIMARY");  // if this is set to 0, ignore the file; I'm not the primary
    if (val != NULL && !strcmp(val, "0")) return 0;
    
    no_slaves = 0;
    char line[500];
    int first = 1;
    // read the file and populate slaves
    // first line is my address and port
    while (fgets(line, 500, fp)) {
      char* addr = strtok(line, ":\n");
      char* port_daemon_s = strtok(NULL, ":\n");
      if (first) {
        assert(addr);
        assert(port_daemon_s);
        LOG(DEBUG, "found own address: %s:%s\n", addr, port_daemon_s);
        strcpy(primary_address, addr);
        primary_port = atoi(port_daemon_s);
        first = 0;
        LOG(INFO, "I'm the master node (%s:%d)\n", primary_address, primary_port);
      } else {
        if (!addr) break;  // got '\n' or something
        strcpy(slaves[no_slaves].addr, addr);
        slaves[no_slaves].port_daemon = atoi(port_daemon_s);
        LOG(DEBUG, "found info about secondary: %s:%d\n", slaves[no_slaves].addr, slaves[no_slaves].port_daemon);
        ++no_slaves;
      }
    }
    return 1;
  } else {
    return 0;
  }
}

int am_i_secondary() {
  char* val = getenv("SECONDARY");
  if (val == NULL) return 0;
  if (!strcpy(val, "0")) return 0;
  LOG(INFO, "I'm a secondary node\n");
  return 1;
}

void get_vm_holes(int node_index, unsigned int* holes, unsigned int* no_holes) {
  int j = 0;
  *no_holes = 0;

  LOG(DEBUG, "get_vm_holes: computing tc holes for %d mem_ranges.\n", no_mem_ranges);

  for (int i = 0; i < NO_TCS_PER_PROC * MAX_PROCS_PER_NODE; ++i) {  // TODO: here i should see how many procs 
                                                  // a node has and just iterate through those, instead of
                                                  // the maximum possible number
    void* start_vm = TC_START_VM_EX(node_index, i);
    void* end_vm = TC_START_VM_EX(node_index, i+1) - 1;
    while (j < no_mem_ranges && mem_ranges[j].r < (unsigned long long)start_vm)
      ++j;
    if (j == no_mem_ranges) return;
    
    void* l = (void*)mem_ranges[j].l;
    void* r = (void*)mem_ranges[j].r;
    
    if ( (l <= start_vm && end_vm <= r) || (start_vm <= l && l <= end_vm ) || (start_vm <= r && r <= end_vm) ) {
      holes[(*no_holes)++] = i;
    }
  }
}


void start_nodes(int port_sctp, int port_tcp) {

  // add ourselves to the array
  strcpy(secondaries[0].addr, primary_address);
  secondaries[0].port_sctp = port_sctp;
  secondaries[0].port_tcp = port_tcp;
  secondaries[0].port_daemon = primary_port;
  secondaries[0].socket = -1;
  secondaries[0].socket_tcp = -1;
  ++no_secondaries;

  // contact slaves, 1 by 1, and ask for their
  int sockets[no_slaves];
  int max_socket = -1;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  fd_set master;
  FD_ZERO(&master);
  LOG(INFO, "attempting connection to %d secondaries\n", no_slaves);
  for (int i = 0; i < no_slaves; ++i) {
    struct addrinfo* addr;
    char sport[10];
    sprintf(sport, "%d", slaves[i].port_daemon);
    if (getaddrinfo(slaves[i].addr, sport, &hints, &addr) < 0) handle_error("getaddrinfo");
    int s = socket(addr->ai_family, SOCK_STREAM, addr->ai_protocol);
    if (s > max_socket) max_socket = s;
    sockets[i] = s;
    if (s == -1) {perror("socket:"); exit(1);}

    long arg;
    // Set non-blocking 
    if( (arg = fcntl(s, F_GETFL, NULL)) < 0) handle_error("fcntl"); 
    arg |= O_NONBLOCK; 
    if( fcntl(s, F_SETFL, arg) < 0) handle_error("fcntl");

    LOG(DEBUG, "attempting connection to secondary %s:%d\n", slaves[i].addr, slaves[i].port_daemon); 
    int res = connect(s, addr->ai_addr, addr->ai_addrlen);
    if (res < 0) {
      if (errno == EINPROGRESS) { 
        FD_SET(s, &master);
      } else { handle_error("connect"); }
    }
  }

  struct timeval tv;
  tv.tv_sec = 10; tv.tv_usec=0;
  int waiting_for = no_slaves;  // number of peers we're waiting for
  while ((waiting_for > 0) && (tv.tv_sec > 0)) {
    if (no_slaves == 0) break;
    fd_set copy_write = master;
    assert(max_socket + 1 < FD_SETSIZE);
    int res = select(max_socket + 1, NULL, &copy_write, NULL, &tv);
    if (res < 0) { handle_error("select"); }

    for (int i = 0; i < no_slaves; ++i) {
      if (FD_ISSET(sockets[i], &copy_write)) {
        --waiting_for;
        FD_CLR(sockets[i], &master);  // remove this socket so that we don't test it in next iterations
        int valopt;
        socklen_t optlen; // = sizeof(int);
        if (getsockopt(sockets[i], SOL_SOCKET, SO_ERROR, (void*)&valopt, &optlen) < 0) handle_error("getsockopt");
        if (valopt) {
          LOG(INFO, "connection to secondary %s:%d didn't succeed\n", slaves[i].addr, slaves[i].port_daemon); 
        } else {
          LOG(INFO, "connection to secondary %s:%d succeeded (socket %d)\n", 
              slaves[i].addr, slaves[i].port_daemon, sockets[i]); 
          // we got here => we have a connected socket for slave i
          
          strcpy(secondaries[no_secondaries].addr, slaves[i].addr);
          secondaries[no_secondaries].port_daemon = slaves[i].port_daemon;
          secondaries[no_secondaries].port_sctp = secondaries[no_secondaries].port_tcp = -1;  
          secondaries[no_secondaries].socket = sockets[i];
          secondaries[no_secondaries].socket_tcp = -1;
          ++no_secondaries;
        }
      }
    }
  }
  LOG(INFO, "done waiting for connections to secondaries to succeed\n"); 

  // close all sockets that haven't been connected yet
  for (int i = 0; i < no_slaves; ++i) {
    if (FD_ISSET(sockets[i], &master)) {
      LOG(INFO, "connection to secondary %s:%d didn't succeed in the timeout\n", slaves[i].addr, slaves[i].port_daemon); 
      if (close(sockets[i])) {
        perror("close:"); exit(1);
      }
    }
  }

  // set sockets to blocking again
  for (unsigned int i = 0; i < no_secondaries; ++i) {
    int arg;
    if (secondaries[i].socket == -1) continue;  // nothing to do for ourselves
    if( (arg = fcntl(secondaries[i].socket, F_GETFL, NULL)) < 0) { 
      fprintf(stderr, "Error fcntl(..., F_GETFL) (%s)\n", strerror(errno)); 
      exit(0); 
    } 
    arg &= (~O_NONBLOCK); 
    if( fcntl(secondaries[i].socket, F_SETFL, arg) < 0) { 
      fprintf(stderr, "Error fcntl(..., F_SETFL) (%s)\n", strerror(errno)); 
      exit(0); 
    } 
  }

  // read from each until we get an '!'
  char buf[5000];
  for (unsigned int i = 0; i < no_secondaries; ++i) {
    if (secondaries[i].socket == -1) continue;  // nothing to do for ourselves
    LOG(INFO, "waiting for memory map from secondary %s\n", secondaries[i].addr); 
    int read_bytes = 0;
    do {
      int res = read(secondaries[i].socket, buf + read_bytes, 5000 - read_bytes);
      if (res < 0) {perror("read from socket"); exit(1);}
      if (buf[read_bytes + res - 1] == '!') {
        buf[read_bytes + res - 1] = 0;
        LOG(DEBUG, "got data from secondary: \"%s\"\n", buf);

        // remove sctp port number - first token
        char* s = strtok(buf, ";");
        assert(s);
        secondaries[i].port_sctp = atoi(s);
        // remove tcp port number - second token
        s = strtok(NULL, ";");
        assert(s);
        secondaries[i].port_tcp = atoi(s);
        // remove number of processors - third token
        s = strtok(NULL, ";");
        assert(s);
        secondaries[i].no_procs = atoi(s);
        LOG(DEBUG, "got info from secondary %s:%d -> %d, %d, %d\n", 
            secondaries[i].addr, secondaries[i].port_daemon, 
            secondaries[i].port_sctp, secondaries[i].port_tcp, secondaries[i].no_procs);

        s = strtok(NULL, "");  // get the rest of the string - the mem map
        parse_mem_map(s, mem_ranges, &no_mem_ranges);
        break;
      }
      read_bytes += res;
    } while (1);
    LOG(INFO, "got port number and memory map from secondary %s:%d\n", 
        secondaries[i].addr, secondaries[i].port_daemon); 
          
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_SEQPACKET;
    //secondaries[i].socket_sctp = socket(AF_INET, SOCK_SEQPACKET, IPPROTO_SCTP);
    // fill in the addr_sctp field
    int res;
    char port[10];
    sprintf(port, "%d", secondaries[i].port_sctp);
    if ((res = getaddrinfo(secondaries[i].addr, 
            port, 
            &hints, 
            &secondaries[i].addr_sctp)) < 0) {
      LOG(CRASH, "getaddrinfo failed: %s\n", gai_strerror(res)); exit(EXIT_FAILURE);
    }

  }
  
  LOG(DEBUG, "Running with nodes:\n");
  for (unsigned int i = 0; i < no_secondaries; ++i) {
    LOG(DEBUG, "%s:%d\n", secondaries[i].addr, secondaries[i].port_daemon);
  }


  // parse own memory map
  parse_own_memory_map(buf);
  parse_mem_map(buf, mem_ranges, &no_mem_ranges);

  // compute address ranges for all secondaries
  qsort(mem_ranges, no_mem_ranges, sizeof(mem_range), &compare_mem_ranges);
  //assign_mem_ranges();
  
  // transmit the index and all other nodes to each node
  LOG(INFO, "transmitting data to %d secondaries...\n", no_secondaries - 1);
  for (unsigned int i = 0; i < no_secondaries; ++i) {
    unsigned int holes[NO_TCS_PER_PROC * MAX_PROCS_PER_NODE];
    unsigned int no_holes;
    get_vm_holes(i, holes, &no_holes);
    if (secondaries[i].socket == -1) continue;  // nothing to do for this node (ourselves)

    sprintf(buf, "%d;", i);  // put the index in the buffer
    // put all the holes in the buffer
    if (no_holes == 0) {
      strcat(buf, "-1;");  // signifies no holes
    } else {
      for (unsigned int j = 0; j < no_holes; j++) {
        char s[no_holes * 10];
        if (j == no_holes - 1)
          sprintf(s, "%d", holes[j]);
        else
          sprintf(s, "%d,", holes[j]);
        strcat(buf, s);
      }
    }

    LOG(DEBUG, "preparing buffer for secondary %s:%d\n", secondaries[i].addr, secondaries[i].port_daemon);

    // put all the addresses in the buffer
    for (unsigned int j = 0; j < no_secondaries; ++j) {
      char s[100];
      sprintf(s, "%s:%d:%d;", secondaries[j].addr, secondaries[j].port_sctp, secondaries[j].port_tcp);
      strcat(buf, s);
    }
    strcat(buf, "!");

    LOG(DEBUG, "sending \"%s\" to secondary %d\n", buf, i); 

    unsigned written = 0;
    do {
      int res = write(secondaries[i].socket, buf + written, strlen(buf) - written);
      LOG(DEBUG, "written %d bytes to secondary %s:%d\n", res, secondaries[i].addr, secondaries[i].port_daemon);
      if (res < 0) {perror("writing range to socket"); exit(1);}
      written += res;
    } while (written < strlen(buf));
    LOG(DEBUG, "done sending data to secondary %s:%d\n", secondaries[i].addr, secondaries[i].port_daemon);
  }
  LOG(INFO, "done transmitting data to secondaries\n");
  
  // close all sockets
  for (unsigned int i = 0; i < no_secondaries; ++i) {
    if (secondaries[i].socket == -1) continue;  // nothing to do for ourselves
    if (close(secondaries[i].socket) < 0) handle_error("close");
  }
  LOG(INFO, "closed all sockets\n");
}

static int start(int argc, char** argv);

static int start_secondary(int argc __attribute__((unused)),
                           char** argv __attribute__((unused))) {

  rt_init();  // FIXME: the delegation interface should check on incoming messages that
              // the runtime initialization has finished
 
  // wait until the delegation interface gets the exit message 
  pthread_mutex_lock(&delegation_if_finished_mutex);
  while (!delegation_if_finished) {
    LOG(INFO, "delegation if: sleeping until the delegation interface gets the quit message\n");
    pthread_cond_wait(&delegation_if_finished_cv, &delegation_if_finished_mutex);
    LOG(INFO, "delegation if: woke up; the delegation interface might have gotten the quit message\n");
  }
  pthread_mutex_unlock(&delegation_if_finished_mutex);
  LOG(INFO,"delegation if: done waiting. exiting.\n");

  rt_quit();
  return 0;
}

static int start(int argc, char** argv) {
  get_vm_holes(0, tc_holes, &no_tc_holes);

  rt_init();  // init the runtime

  struct mapping_decision mapping;
  mapping.should_inline = 0;
  mapping.no_proc_assignments = 1;
  mapping.proc_assignments[0].load_percentage = 100;
  mapping.proc_assignments[0].node_index = NODE_INDEX;
  mapping.proc_assignments[0].proc_index = 0;
  mapping.proc_assignments[0].no_tcs = 1;
  mapping.no_ranges_per_tc = 1;

  fam_context_t* fc = allocate_fam(//&_fam___root_fam, 
                                    //0, 0, 1, NULL, &mapping);
                                    1, NULL, &mapping);
  //fam_context_t* fam = allocate_root_fam(&_fam___root_fam, argc, argv);
  assert(fc);  // allocate shouldn't fail; we're just starting up
  LOG(DEBUG, "creating root_fam\n"); 
  //create_fam(fc, &_fam___root_fam);
  //create_fam(fc, &__root_fam);
  create_fam(fc, &__slFfmta___root_fam_metaloop, 0, 1, INHERIT_DEFAULT_PLACE);

  // transmit argc
  //write_istruct_no_checks(&(fc->ranges[0].dest.tc->globals[0]), argc);
  write_istruct_no_checks(&(fc->distribution.reservations[0].first_tc.tc->globals[0]), argc);
  // transmit argv
  //write_istruct_no_checks(&(fc->ranges[0].dest.tc->globals[1]), (long)argv);
  write_istruct_no_checks(&(fc->distribution.reservations[0].first_tc.tc->globals[1]), (long)argv);

  // wait for root_main to finish
  pthread_mutex_lock(&main_finished_mutex);
  while (!main_finished) {
    LOG(DEBUG, "main: sleeping until root_fam finishes\n");
    pthread_cond_wait(&main_finished_cv, &main_finished_mutex);
    LOG(DEBUG, "main: woke up. root_fam might be finished\n");
  }
  pthread_mutex_unlock(&main_finished_mutex);
  LOG(DEBUG,"main: root_fam finished\n");

  LOG(DEBUG, "allocate requests on proc 0: %d\n", no_allocate_requests_per_proc[0]);
  LOG(DEBUG, "allocate requests on proc 1: %d\n", no_allocate_requests_per_proc[1]);

  send_quit_message_to_secondaries();

  rt_quit();  // tear down the runtime

  return 0;
}

/*
  * main function; sets up the runtime and creates root_fam, with one thread
 */
#undef main
int main(int argc, char** argv) {
  struct sigaction sa;
  sa.sa_sigaction = sighandler_foo;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;

  if (sigaction(SIGSEGV, &sa, NULL) != 0)
  { perror("sigaction"); exit(1); }

  LOG(DEBUG, "starting\n");
  srand(getpid());
  _cur_tc = NULL;
  
  // read the number of CPUs on the system
  NO_PROCS = get_no_CPUs();
  int port_sctp, port_tcp;
    
  if (pthread_spin_init(&rt_init_done_lock, PTHREAD_PROCESS_PRIVATE) != 0)
    handle_error("pthread_spin_init");
  init_network();

  if (am_i_secondary()) {
    LOG(DEBUG, "creating a socket for delegation...\n");
    create_delegation_socket(&port_sctp, &port_tcp);
    LOG(INFO, "bound sockets for the delegation interface; STCP port: %d\t TCP port: %d\n", 
        port_sctp, port_tcp);
    
    sync_with_primary(port_sctp, port_tcp, NO_PROCS, &NODE_INDEX, tc_holes, &no_tc_holes);
    return start_secondary(argc, argv);
  
  } else if (am_i_primary()) {
    LOG(DEBUG, "creating a socket for delegation...\n");
    create_delegation_socket(&port_sctp, &port_tcp);
    LOG(INFO, "bound sockets for the delegation interface; STCP port: %d\t TCP port: %d\n", 
        port_sctp, port_tcp);
    
    start_nodes(port_sctp, port_tcp);
    NODE_INDEX = 0;
    LOG(DEBUG, "main: starting; _cur_tc = %p\n", _cur_tc);
    return start(argc, argv);
    //start_primary(argc, argv);
  
  } else { // I'm running on my own, no peers
    LOG(INFO, "running in standalone mode; no peers\n");
    //return start_standalone(argc, argv);  
    NODE_INDEX = 0;
    return start(argc, argv);
  }
}

/*
 *  Recursively writes a value to a global slot in all TC's from a proc assigned to a family
 */
void write_global_to_chain_of_tcs(tc_t* tc, unsigned int index, long val
                                  //bool is_mem
                                  //bool desc_present, 
                                  //const memdesc_t* desc, 
                                  //int no_ranges, 
                                  //mem_range_t first_range
                                  ) {

  // write the istructure
  write_istruct(tc->ident.node_index, &tc->globals[index], val, &tc->ident);//, is_mem);

  // recurse
  if (!tc->is_last_tc_on_proc) {
    write_global_to_chain_of_tcs(tc->next.tc, index, val);//, is_mem);
  }
}

void write_global(fam_context_t* fc, unsigned int index, long val, bool is_mem) {
  unsigned int i;
  // write the global to every TC that will run part of the family

  LOG(DEBUG, "write_global: entering\n");
  for (i = 0; i < fc->distribution.no_reservations; ++i) {
    if (fc->distribution.reservations[i].first_tc.node_index == (signed)NODE_INDEX) {
      write_global_to_chain_of_tcs(fc->distribution.reservations[i].first_tc.tc, index, val);//, is_mem);
    } else {
      write_global_to_remote_chain(fc->distribution.reservations[i].first_tc.node_index,
                                   fc->distribution.reservations[i].first_tc,
                                   index, val, is_mem, true);
    }
  }
}

/*
 * Returns 0 if the range that is passed overlaps an existing mapping, 1 otherwise
 * TODO: Currently it's unused, because it smashes the stack when called from a sighandler.
 */
static int check_virtual_memory_range(void* range_start, void* range_end) {
  // TODO: this function is here only for debugging, it shouldn't be necessary. Also, it will
  // crash if we have more than 5000 mmapped range
  char c[5000];
  mem_range ranges[5000];
  int no_ranges = 0;
  parse_own_memory_map(c);
  parse_mem_map(c, ranges, &no_ranges);
  for (int i = 0; i < no_ranges; ++i) {
    void* l = (void*)ranges[i].l;
    void* r = (void*)ranges[i].r;
    if ( 
        (l <= range_start && range_end <= r) || 
        (range_start <= l && l <= range_end ) || 
        (range_start <= r && r <= range_end) ) {
      return 0;
    }
  }
  return 1;
}

static void sighandler_foo(int sig, siginfo_t *si, void *ucontext __attribute__((unused)))
{
  LOG(DEBUG, "sigsegv handler: GOT SIGSEGV \n");
  char *page =
    (char*)((unsigned long)si->si_addr
        & ~((unsigned long)getpagesize() - 1L));
  // map a chunk if the address is within the range that the network interface is currently copying over
  if (si->si_addr >= cur_incoming_mem_range_start &&
      si->si_addr < (cur_incoming_mem_range_start + cur_incoming_mem_range_len)) {
    void* range_start = (void*)((unsigned long)si->si_addr
        & ~(MAPPING_CHUNK - 1L));
    void* range_end __attribute__((unused)) = range_start + MAPPING_CHUNK;

    // check that the range we intend to map doesn't overlap any existing range
    // TODO: the call to check_virtual_memory_range crashes because of stack smashing... probably
    // because we're in a signal handler. Either provide a bigger stack for the sig handlers,
    // or reduce the consumption of the function, or drop it all together.
    //assert(check_virtual_memory_range(range_start, range_end));

    void* mapping = mmap(range_start, MAPPING_CHUNK, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_FIXED | MAP_ANON, -1, 0);
    assert(mapping == range_start);
    LOG(DEBUG, "sigsegv handler: mapped a chunk\n");
  } else {
    printf("sig handler for %d, fault %p, page %p\n", sig, si->si_addr, page);
    assert(0); // trigger a core dump
    exit(EXIT_FAILURE);
  }
  return;
}

/*
 * TODO: Temporary; 
 * convert between stubs and longs so stubs can be passed in istructs
 */
memdesc_stub_t _long_2_stub(long x) {
  memdesc_stub_t res = *(memdesc_stub_t*)&x;
  return res;
}

long _stub_2_long(memdesc_stub_t stub) {
  long res = *(long*)&stub;
  return res;
}

