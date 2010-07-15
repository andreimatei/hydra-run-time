#ifndef MEM_COMM_H
#define MEM_COMM_H

/*
 *  The range of memory that is currently being received over the network.
 *  The SIGSEGV handler uses this to mmap the range when the network interface tries to write it. 
 */
extern void* cur_incoming_mem_range_start;
extern unsigned long cur_incoming_mem_range_len;


void init_mem_comm();

#endif

