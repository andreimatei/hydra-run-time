#ifndef SL_HRT_H
#define SL_HRT_H

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE /* for ucontext */
#endif
#include <ucontext.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#include <sys/time.h>
#include <svp/delegate.h>

extern int NODE_INDEX;

void exit(int);

struct tc_t;

#define MAX_RANGES_PER_MEM 16

/*
typedef struct {
  int place_local;
  int place_default;
  int node_index;
  int proc_index;
  int tc_index;
} sl_place_t;
*/

typedef struct mem_range {
  void *p, *orig_p;
  int no_elements;
  int sizeof_element;
  //struct mem_range* next;
}mem_range_t;

typedef struct memdesc {
  //enum memdesc_type type;
  //memdesc_stub_t orig_stub;  // valid only if type == RESTRICT
  //unsigned int start_element, no_elements;  // valid only if type == RESTRICT

  mem_range_t ranges[MAX_RANGES_PER_MEM];
  int no_ranges;
}memdesc_t  __attribute__ ((aligned (32)));  // because pointers to these have to use less bits, cause
                                             // we stuff them in memdesc_stub_t's

typedef struct {
  // index of the node where .pointer is valid (the node that has the descriptor)
  unsigned node         :10;
  unsigned S            :1;  // 1 if the stub represents a desc involved in a scatter-gather
                             // if set, implies that the data is always local when any operation needs it
  unsigned have_data    :1;  // valid only if !S and data_provider != NODE_INDEX; 
                             // 1 if data is present (has been pulled or pushed)
                             
  // index of the node that has a consistent view of the data described by this descriptor
  unsigned data_provider: 10;
  // pointer to the memdesc, valid on the node inticated by .node
  long pointer :42;  // the pointer is 42 bits, when we would normally need 47. So memdescs have to
                     // be alligned so that the last 5 bits are 0 => alligned on a 32 byte boundary 
} memdesc_stub_t;

static inline unsigned int get_stub_node(memdesc_stub_t stub) {
  //return stub >> 53;
  return stub.node;
}

static inline memdesc_t* get_stub_pointer(memdesc_stub_t stub) {
  return (memdesc_t*)((long)(stub.pointer) << 5);
}

static inline void set_stub_pointer(memdesc_stub_t* stub, const memdesc_t* desc) {
  // assert that the desc is alligned properly
  assert(((unsigned long)desc & 15) == 0);
  stub->pointer = ((unsigned long)desc >> 5);
}

/*
 * Create a stub for a local descriptor
 * data_provider - [IN] - the index of the node that has the data (probably NODE_INDEX will be passed always)
 * have_data     - [IN] - this will be copied verbatim to the stub, so it has to be valid on the node that will
 *                        use the stub. Thus, if you indent to pass the stub, you probably want to say 0.
 */
static inline memdesc_stub_t _create_memdesc_stub(
    const memdesc_t* desc, 
    int data_provider,
    int have_data,
    int S) {
  memdesc_stub_t stub;
  stub.node = NODE_INDEX;
  stub.have_data = have_data;
  stub.data_provider = data_provider;
  stub.S = S;
  set_stub_pointer(&stub, desc);
  assert(get_stub_pointer(stub) == desc);  // just for debugging
  return stub;
}

struct tc_ident_t {
  int node_index;
  int proc_index;
  int tc_index;
  struct tc_t* tc;
};
typedef struct tc_ident_t tc_ident_t;


/* maximum number of shareds and globals (independenty, not
   in total) that a thread function can take */
#define MAX_ARGS_PER_FAM 32  


enum istruct_state {EMPTY = 0, WRITTEN = 1, SUSPENDED = 2};
typedef enum istruct_state istruct_state;

enum default_place_policy_enum {INHERIT_DEFAULT_PLACE, LOCAL_NODE, LOCAL_PROC, LOCAL_TC};
typedef enum default_place_policy_enum default_place_policy_enum;

/*
 * Represents an I-structure; the decision has been to not mark any fields as volatile;
 * if needed when actually used, the whole struct should be declared as volatile. Sometimes
 * there's no need to do this (e.g. when both the reader and the writer are running on the same
 * TC, there's no asynchrony). 
 */
struct i_struct {
  enum istruct_state state;
  long data;
  pthread_spinlock_t lock;  // used in some cases to guarantee that 
                            // reading and suspending  or  writing and waking up the suspended TC
                            // are atomic operations
};
typedef struct i_struct i_struct;

/*
typedef struct i_struct_fat_pointer {
  int node_index;  // node where the istruct resides (the istruct pointer is valid on that node)
  i_struct* istruct;
}i_struct_fat_pointer;
*/

struct heap_t {
  void* lowaddr;
  int size;
};
typedef struct heap_t heap_t;

typedef struct {
  long index_start, index_end;  // inclusive
  tc_ident_t dest;
}thread_range_t;


typedef struct {
  int empty;  // 1 if the fam_context is available for reuse (nobody is going to sync on it)
  i_struct done;  // written on family termination
  //tc_ident_t* blocked_tc;
  thread_range_t ranges[100];  //TODO: replace this with something else 
  int no_ranges;
  i_struct shareds[MAX_ARGS_PER_FAM];  // shareds written by the last thread in the fam. Conceptually, 
                            //they don't need to be istructs, since they will only be read after the sync,
                            //but we made them istructs anyway to use the infrastructure for writing them
                            //remotely.
  int index;  // the index of this fam_context among all the fam_contexts allocated on it's node

  memdesc_t shared_descs[MAX_ARGS_PER_FAM];  // space for the descriptors corresponding to the stubs written by the
                                             // last thread in the fam. They will be copied here when the shared
                                             // is written to, and the pointer of withing the stub will be modified
                                             // to point here, because otherwise the descriptor would remain on the
                                             // child's stack which can be erased before the parent is done with the
                                             // descriptor.

} fam_context_t;

struct tc_t {
  tc_ident_t ident;  // identity of this TC
  long index_start, index_stop;  // indexes are inclusive
  //int finished;  // 1 if the TC is available for reuse; usually set to 1 by code generated by the compiler
                 // at the end of loop functions
  //int blocked;  // -1 means empty, 0 means running, 1 means blocked (so far on an istruct, but we might want
                // to extend this ?). TODO: is this really used? check and remove
  struct timeval blocking_time;  // if TC is blocked, this is the moment when it was blocked

  ucontext_t context;  // contains stack in .uc_stack
  stack_t initial_thread_stack;  // the stack that will be reused every time when a new family is started
                //TODO: decide to what extent the .size member has any meaning. If it does, we need to trap
                // on stack auto-growth and update it.

  // i-structures for globals and shareds received by the threads running in this TC
  i_struct shareds[MAX_ARGS_PER_FAM];
  i_struct globals[MAX_ARGS_PER_FAM];
  // // space if shareds[i]/globals[i] is a memdesc_stub_t, then shared/global_first_ranges[i]
  // // will hold the first range described by the respective descriptor, and will be accessible after reading
  // // the istruct (the shared or the global)
  // mem_range_t shared_first_ranges[MAX_ARGS_PER_FAM];
  // int shared_no_ranges[MAX_ARGS_PER_FAM];
  // mem_range_t global_first_ranges[MAX_ARGS_PER_FAM];
  // int global_no_ranges[MAX_ARGS_PER_FAM];

  memdesc_t shared_descs[MAX_ARGS_PER_FAM];  // space for the descriptors associated with the shared mem args

  //fam_context_t* fam_context;  // family context of the thread of the family 
                               // currently occupying the TC
  tc_ident_t parent_ident;  // identifier of the TC where the parent is running; will
                            // have .node_index == -1 for families that don't have a parent
                            // (i.e. fam_main and continuation creates)
  tc_ident_t prev, next;  // identifiers of the TCs running the previous and next chunks of
                          // threads; for the first TC and the last TC, prev and next, respectively,
                          // will be the parent
  int is_last_tc;   // 1 if this is the TC where the last chunk of threads in a fam was allocated,
                    // 0 otherwise

  i_struct* done;  // pointer to the done istruct from the FC (this might be on a different node)
  i_struct* final_shareds;  // pointer to the shareds in the FC (this might be on a different node).
                            // used by the last thread in a family to write shareds for the parent

  memdesc_t* final_descs;   // pointer to the descriptor table in the FC (this might be on a different node).
                            // used by the last thread in a family to write the descriptors corresponding to the
                            // shared mem arguments passed back to the parent

  heap_t heap;

  sl_place_t place_default;  // PLACE_DEFAULT, as inherited or set at family creation
  sl_place_t place_local;    // PLACE_LOCAL always represents this TC; Statically initialized when TC is created.

  int fib;  // FIXME: remove this. Added just for testing the fibonacci program.

};//TCS[NO_TCS];
typedef struct tc_t tc_t;

/* data structure assigned to a processor */
struct processor_t {
  ucontext_t scheduler_context;
};
typedef struct processor_t processor_t;

#define NO_TCS_PER_PROC (1<<10)
#define MAX_PROCS_PER_NODE 16
#define INIT_TC_STACK_SIZE (1L<<13)
#define INIT_TC_HEAP_SIZE (1L<<20)

extern processor_t _processor[MAX_PROCS_PER_NODE];

enum LOG_LEVEL {CRASH = 0, WARNING = 3, INFO = 7, VERBOSE = 10, DEBUG = 15};
typedef enum LOG_LEVEL LOG_LEVEL;

extern LOG_LEVEL _logging_level;

typedef void (*thread_func)();  // type of a thread function

extern __thread tc_t* _cur_tc;  // pointer to the TC currently occupying the accessing processor



// represents an identifier of a concurrency node in the tree constructed by the mapping engine
struct mapping_node_t {
  //TODO
};

struct proc_assignment {
  //unsigned long no_threads;
  unsigned int load_percentage;  // between 1 and 100, the percentage of threads that will run on this
                                 // proc, out of all the threads in the family.
  unsigned int no_tcs;
  int node_index;
  int proc_index;
};
typedef struct proc_assignment proc_assignment;

#define MAX_NO_PROC_ASSIGNMENTS_PER_MAPPING 100

/* Represent a mapping decision from the mapping engine */
struct mapping_decision {
  int should_inline;  // if this is 1, the other fields are irelevant; the
                      // family is supposed to be inlined in the parent
  unsigned int no_proc_assignments;
  struct proc_assignment proc_assignments[MAX_NO_PROC_ASSIGNMENTS_PER_MAPPING];
};
typedef struct mapping_decision mapping_decision;

void _free_tc(int proc_id, int tc_id);

fam_context_t* allocate_fam(
    //thread_func func, 
    //int num_shareds, int num_globals,
    long start_index, 
    long end_index,
    long step,
    struct mapping_node_t* parent_id, 
    const struct mapping_decision* mapping);

mapping_decision map_fam(
    thread_func func,
    long no_threads,
    long block_size,
    //long start_index,
    //long end_index,
    struct mapping_node_t* parent_id//,
    //sl_place_t hint);
    );

//tc_ident_t create_fam(fam_context_t* fc);
tc_ident_t create_fam(fam_context_t* fc, 
                      thread_func func,
                      default_place_policy_enum default_place_policy
                      //int no_threads
                      );

long sync_fam(fam_context_t* fc, /*long* shareds_dest,*/ int no_shareds, ...);
//TODO: release was removed, as it appears there's nothing for it to do. Recheck the
//decision.
//void release_fam(fam_context_t* fc);

void _memdesc(memdesc_t* memdesc, void* p, unsigned int no_elements, unsigned int sizeof_element);

/*
 * Create a new descriptor referring to part of the first range of an existing descriptor.
 * The new descriptor, upon activation, will return the same pointer as the original one.
 */
memdesc_stub_t _memrestrict(
    memdesc_stub_t orig_stub, 
    memdesc_t* new_desc, //memdesc_stub_t* new_stub, 
    //mem_range_t first_range,  // first range from new_desc
    int start_elem, 
    int no_elems);

/*
 * Adds objects to a descriptor (everything that was part of stub_to_copy)
 */
void _memextend(memdesc_stub_t stub, memdesc_stub_t stub_to_copy);

/*
 * Pulls data and descriptor for a stub. Updates orig to make it LL, and also returns the new value.
 */
memdesc_stub_t _memlocalize(memdesc_t* new_desc, //memdesc_stub_t* new_stub,
                            memdesc_stub_t* orig,
                            mem_range_t first_range,  // first range of the original descriptor
                            unsigned int no_ranges  // no ranges from the original descriptor
                            );
/*
 * Create a consistent view upon the object described by stub.
 * Copies the first range of the object to new_desc.
 * Return a pointer to the first range of the descriptor.
 */
void* _memactivate(memdesc_stub_t* stub,
                   //mem_range_t first_range,
                   //unsigned int no_ranges,
                   memdesc_t* new_desc,
                   memdesc_stub_t* new_stub
                   );
/*
 * Propagate local changes back to the data provider.
 */
void _mempropagate(memdesc_stub_t stub);

/*
 * Scatters the first range of a descriptor so that each thread i in a family gets a consistent view on
 * elements [a*i + a, a*i + b].
 */
void _memscatter_affine(fam_context_t* fc, 
                        memdesc_stub_t stub, 
                        //mem_range_t first_range, 
                        int a, 
                        int b, 
                        int c);

/*
 * Gathers from a descriptor that was scattered with _memscatter_affine(.. a,b,c)
 */
void _memgather_affine(fam_context_t* fc, 
                       memdesc_stub_t stub, 
                       //mem_range_t first_range, 
                       int a, 
                       int b, 
                       int c);

/* Used by a thread func to get it's range of indexes */
static inline long _get_start_index() {
  return _cur_tc->index_start;
}
static inline long _get_end_index() {
  return _cur_tc->index_stop;
}
/*
static inline fam_context_t* _get_fam_context() {
  return _cur_tc->fam_context;
}
*/
static inline const tc_ident_t* _get_parent_ident() {
  return &_cur_tc->parent_ident;
}

static inline const tc_ident_t* _get_prev_ident() {
  return &_cur_tc->prev;
}

static inline const tc_ident_t* _get_next_ident() {
  return &_cur_tc->next;
}

static inline i_struct* _get_final_shareds_pointer() {
  return _cur_tc->final_shareds;
}

static inline memdesc_t* _get_final_descs_pointer() {
  return _cur_tc->final_descs;
}


static inline i_struct* _get_done_pointer() {
  return _cur_tc->done;
}

static inline int _is_last_tc() {
  return _cur_tc->is_last_tc;
}


static inline void _return_to_scheduler() {
  
  //swapcontext(&_cur_tc->scratch_context, &processor[_cur_tc->ident.proc_index].scheduler_context);
  //TODO: check that this call to setcontext is correct; the man says that the context used must be obtained
  //through getcontext() or makecontext(), which is not true in this case; it has been obtained by swapcontext()
  setcontext(&_processor[_cur_tc->ident.proc_index].scheduler_context);
}


//void rt_init();
//void rt_quit();


// called by fam_main at the end, instead of the regular thread function exit code
void end_main();

//typedef void (fam_main_t)(void);  // the type of fam_main()



/* writer and reader are running in the same TC */
static inline void write_istruct_same_tc(
  i_struct* istructp, long val) {
  assert(istructp->state == WRITTEN);  //the istruct must have been written already for the current thread;
  istructp->data = val;
}
void write_istruct_same_proc(
    volatile i_struct* istructp, 
    long val, 
    tc_t* potentially_blocked_tc);
void write_istruct_different_proc(
    volatile i_struct* istructp,
    long val,
    tc_t* potentially_blocked_tc);
void write_istruct(int node_index,
                   volatile i_struct* istructp,
                   long val, 
                   const tc_ident_t* reading_tc,
                   int is_mem);

void write_argmem(int node_index,
                  volatile i_struct* istructp,
                  memdesc_stub_t stub,
                  memdesc_t* desc_dest,
                  const tc_ident_t* reading_tc);

static inline long read_istruct_same_tc(i_struct* istruct) {
  assert(istruct->state == WRITTEN);
  return istruct->data;
}
long read_istruct_different_tc(volatile i_struct* istruct, int same_proc);
long read_istruct(volatile i_struct* istructp, const tc_ident_t* writing_tc);

void write_global(fam_context_t* ctx, int index, long val, int is_mem);

/*
 * Convert between stubs and longs so stubs can be passed in istructs
 */
memdesc_stub_t _long_2_stub(long x);
long _stub_2_long(memdesc_stub_t stub);

/*
 * Generate a stub suitable for passing to a child based on an existing stub.
 */
memdesc_stub_t _stub_2_canonical_stub(memdesc_stub_t stub, int S);

static inline long timediff(struct timeval t1, struct timeval t2) {
  long micros = 1000000 * (t1.tv_sec - t2.tv_sec);
  micros += t1.tv_usec - t2.tv_usec;
  return micros / 1000;
}

static inline long timediff_now(struct timeval t) {
  struct timeval now;
  gettimeofday(&now, NULL);
  long micros = 1000000 * (now.tv_sec - t.tv_sec);
  micros += now.tv_usec - t.tv_usec;
  return micros/1000;
}

void send_ping(int node_index, int identifier, int request_unblock, i_struct* istructp);

#endif
