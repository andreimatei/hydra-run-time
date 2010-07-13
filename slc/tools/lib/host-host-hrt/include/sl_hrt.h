#ifndef SL_HRT_H
#define SL_HRT_H

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE /* for ucontext */
#endif
#include <ucontext.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>

extern int NODE_INDEX;

void exit(int);

struct tc_t;

#define MAX_RANGES_PER_MEM 16

typedef struct mem_range {
  void *p, *orig_p;
  int no_elements;
  int sizeof_element;
  //struct mem_range* next;
}mem_range_t;

//--------------------------------------------------
// enum memdesc_type {
//                    REGULAR = 0, 
//                    RESTRICT  /* a descriptor which is the result of a memrestrict operation */
//                   };
//-------------------------------------------------- 

typedef struct memdesc {
  //enum memdesc_type type;
  //memdesc_stub_t orig_stub;  // valid only if type == RESTRICT
  //unsigned int start_element, no_elements;  // valid only if type == RESTRICT

  mem_range_t ranges[MAX_RANGES_PER_MEM];
  int no_ranges;
}memdesc_t;

/*
typedef struct memdesc_stub {
  short node_index;
  short slot_index;
  int fc_index;
}memdesc_stub_t;
*/

/*
 * 11 bits for node
 * 1 bit for S
 * 52 bits for pointer
 */
//typedef unsigned long memdesc_stub_t;

typedef struct {
  // inde of the node where .pointer is valid (the node that has the descriptor)
  unsigned node :10;
  unsigned have_data :1;  // valid only if data_provider != NODE_INDEX
  // index of the node that has a consistent view of the data described by this descriptor
  unsigned data_provider: 10;
  // pointer to the memdesc, valid on the node inticated by .node
  long pointer :43;  // the pointer is 43 bits, when we would normally need 47. So memdescs have to
                     // be alligned so that the last 4 bits are 0 => alligned on a 16 byte boundary 
} memdesc_stub_t;

static inline unsigned int get_stub_node(memdesc_stub_t stub) {
  //return stub >> 53;
  return stub.node;
}

//--------------------------------------------------
// static inline int get_stub_S(memdesc_stub_t stub) {
//   //return (stub & 0010000000000000L) > 0;
//   return stub.S;
// }
//-------------------------------------------------- 

static inline memdesc_t* get_stub_pointer(memdesc_stub_t stub) {
  //return (memdesc_t*)(stub & 0x000FFFFFFFFFFFFFL); 
  return (memdesc_t*)(long)(stub.pointer << 4);
}

static inline void set_stub_pointer(memdesc_stub_t* stub, const memdesc_t* desc) {
  // assert that the desc is alligned properly
  assert(((unsigned long)desc & 0xF) == 0);
  stub->pointer = ((unsigned long)desc >> 4);
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


enum istruct_state {EMPTY = 0, WRITTEN, SUSPENDED};
typedef enum istruct_state istruct_state;

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
  i_struct shareds[MAX_ARGS_PER_FAM];  // shareds written by the last thread in the fam. Technically, 
                            //they don't need to be istructs, since they will only be read after the sync,
                            //but we made them istructs anyway to use the infrastructure for writing them
                            //remotely.
  int index;  // the index of this fam_context among all the fam_contexts allocated on it's node

  memdesc_t mems[MAX_ARGS_PER_FAM];  // FIXME: allign these

  //long shareds[MAX_ARGS_PER_FAM];  // shareds written by the last thread in the fam. They don't need to
                                   // be i-structs since they will only be read after the sync
} fam_context_t;

struct tc_t {
  tc_ident_t ident;  // identity of this TC
  long index_start, index_stop;  // indexes are inclusive
  int finished;  // 1 if the TC is available for reuse; usually set to 1 by code generated by the compiler
                 // at the end of loop functions
  int blocked;  // -1 means empty, 0 means running, 1 means blocked (so far on an istruct, but we might want
                // to extend this ?). TODO: is this really used? check and remove

  ucontext_t context;  // contains stack in .uc_stack
  stack_t initial_thread_stack;  // the stack that will be reused every time when a new family is started
                //TODO: decide to what extent the .size member has any meaning. If it does, we need to trap
                // on stack auto-growth and update it.

  i_struct shareds[MAX_ARGS_PER_FAM];
  i_struct globals[MAX_ARGS_PER_FAM];
  fam_context_t* fam_context;  // family context of the thread of the family 
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

  heap_t heap;
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
  unsigned int load_percentage;
  unsigned int no_tcs;
  int node_index;
  int proc_index;
};
typedef struct proc_assignment proc_assignment;

/* Represent a mapping decision from the mapping engine */
struct mapping_decision {
  int should_inline;  // if this is 1, the other fields are irelevant; the
                      // family is supposed to be inlined in the parent
  unsigned int no_proc_assignments;
  struct proc_assignment proc_assignments[100];  // TODO: replace this with smth else
};
typedef struct mapping_decision mapping_decision;

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
    //long start_index,
    //long end_index,
    struct mapping_node_t* parent_id);

//tc_ident_t create_fam(fam_context_t* fc);
tc_ident_t create_fam(fam_context_t* fc, 
                      thread_func func
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
memdesc_stub_t _memrestrict(memdesc_stub_t orig_stub, memdesc_t* new_desc, //memdesc_stub_t* new_stub, 
                  mem_range_t first_range,  // first range from new_desc
                  int start_elem, int no_elems);

/*
 * Adds objects to a descriptor (everything that was part of stub_to_copy)
 * no_ranges[OUT] -> will be set to the new number of ranges in stub.
 */
void _memextend(memdesc_stub_t stub, memdesc_stub_t stub_to_copy, int* no_ranges);

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
 * Return a pointer to the first range of the descriptor.
 */
void* _memactivate(memdesc_stub_t* stub, mem_range_t first_range, unsigned int no_ranges);

/*
 * Propagate local changes back to the data provider.
 */
void _mempropagate(memdesc_stub_t stub);

/*
 * Propagate local changes to the parent (as opposed to the data provider)
 */
void _mempropagate_up(memdesc_stub_t stub, int parent_node);

/*
 * Scatters the first range of a descriptor so that each thread i in a family gets a consistent view on
 * elements [a*i + a, a*i + b].
 */
void _memscatter_affine(fam_context_t* fc, memdesc_stub_t stub, int a, int b, int c);

/*
 * Gathers from a descriptor that was scattered with _memscatter_affine(.. a,b,c)
 */
void _gathermem_affine(fam_context_t* fc, memdesc_stub_t stub, int a, int b, int c);

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
/*
void write_istruct(volatile i_struct* istructp, 
                   long val, 
                   const tc_ident_t* reading_tc);
*/
void write_istruct(//volatile i_struct_fat_pointer istructp, 
                   int node_index,
                   volatile i_struct* istructp,
                   long val, 
                   const tc_ident_t* reading_tc);

//void* _activate(mem_pointer_t p);

/*
 * Create a stub for a local descriptor
 */
static inline memdesc_stub_t _create_memdesc_stub(const memdesc_t* desc,
                                                  int data_provider, // node that has the data
                                                  int have_data);    // value for the .have_data member of the stub

//--------------------------------------------------
// static inline void* _activate_from_istruct(long x) {
//   mem_pointer_t* p = (mem_pointer_t*)&x;
//   // FIXME: pull from remote node
//   memdesc_t mem;
//   return mem.ranges[0].p;
// }
//-------------------------------------------------- 

//--------------------------------------------------
// /*
//  * Create a descriptor for a memory range
//  * len: in bytes
//  */
// static inline memdesc_t create_memdesc(void* p, int len) {
//   memdesc_t r; r.no_ranges = 1; r.ranges[0].p = p; r.ranges[0].len = len;
//   return r;
// }
//-------------------------------------------------- 


static inline long read_istruct_same_tc(i_struct* istruct) {
  assert(istruct->state == WRITTEN);
  //istruct->state = EMPTY;
  return istruct->data;
}
long read_istruct_different_tc(volatile i_struct* istruct, int same_proc);
long read_istruct(volatile i_struct* istructp, const tc_ident_t* writing_tc);

void write_global(fam_context_t* ctx, int index, long val);

//--------------------------------------------------
// /* read a shared from a child family 
//  * TODO: maybe optimize for when the read is done after sync. Then the istruct can
//  * be read directly 
//  */
// static inline long read_shared_from_child(
//     fam_context_t* child_fam, 
//     int shared_index) {
//   assert(_cur_tc != NULL);  // this should only fail for the parent of fam_main,
//                            // but fam_main doesn't have any shareds
//   return read_istruct(&child_fam->shareds[shared_index],
//                       &child_fam->ranges[child_fam->no_ranges].dest);
// }
// 
//-------------------------------------------------- 

#endif
