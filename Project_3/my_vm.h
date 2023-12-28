#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// List all group member's name: Naman Bajaj, Mourya Vulupala
// username of iLab: nb726, mv638

#define DEBUG 0
#define DEBUG_PRINT if (DEBUG) printf

#define DEBUG_TLB 0
#define DEBUG_TLB_PRINT if (DEBUG_TLB) printf

#define DEBUG_MULTITHREAD 0
#define DEBUG_MULTITHREAD_PRINT if (DEBUG_MULTITHREAD) printf

//Assume the address space is 32 bits, so the max memory size is 4GB
#define ADDRESS_SPACE 32

//Page size is 4KB
#define PGSIZE 4096

#define TLB_ENTRIES 512

// Maximum size of virtual memory
#define MAX_MEMSIZE 4ULL*1024*1024*1024

// Size of "physical memory"
#define MEMSIZE 1024*1024*1024

//Add any important includes here which you may need
#include <sys/mman.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>

// Represents a page table entry
typedef unsigned long pte_t;

// Represents a page directory entry
typedef unsigned long pde_t;

void set_physical_mem();
pte_t* translate(pde_t *pgdir, void *va);
int page_map(pde_t *pgdir, void *va, void* pa);
void *t_malloc(unsigned int num_bytes);
void t_free(void *va, int size);
int put_value(void *va, void *val, int size);
void get_value(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);
void print_TLB_missrate();

void *get_next_avail(int num_pages);
void *get_virtual_address(int num_pages);

int perform_IO(void *va, void *val, int size, int is_put);

// bit functions
void set_bit(unsigned char *bitmap, int index);
void clear_bit(unsigned char *bitmap, int index);
int get_bit(unsigned char *bitmap, int index);

/* Part 2 - TLB Stuff */
// TLB will follow FIFO replacement policy
// Start from 0 and go to TLB_ENTRIES - 1
// Replacement goes in a circular fashion

//Structure to represents TLB
struct tlb {
    /*Assume your TLB is a direct mapped TLB with number of entries as TLB_ENTRIES
    * Think about the size of each TLB entry that performs virtual to physical
    * address translation.
    */
   void *virtual_address;
   void *physical_address;
   int valid;
};

void init_TLB();
void invalidate_TLB_entry();
void print_TLB();

int check_all_tlb_entries();

int add_TLB(void *va, void *pa);
pte_t *check_TLB(void *va);

#endif
