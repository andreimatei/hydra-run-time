#ifndef MEM_COMM_H
#define MEM_COMM_H

/*
 *  The range of memory that is currently being received over the network.
 *  The SIGSEGV handler uses this to mmap the range when the network interface tries to write it. 
 */
extern void* cur_incoming_mem_range_start;
extern unsigned long cur_incoming_mem_range_len;


void init_mem_comm();
bool memdesc_desc_local(memdesc_stub_t stub);
/*
 * Pull a descriptor from the descriptor provider of stub and updates the stub to point to the local copy.
 * Note that if the descriptor has a single range, no network operation is done, since info about the first 
 * range is provided directly to this function.
 */
void pull_desc(memdesc_t* new_desc, memdesc_stub_t* stub, memdesc_t* orig_desc);

#endif

