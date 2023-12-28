// File:	worker_t.h

// List all group member's name: Naman Bajaj, Mourya Vulupala
// username of iLab: nb726, mv638
// iLab Server: cs416f23-18

#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
#define USE_WORKERS 1

#define QUANTUM 10000

#define FALSE 0
#define TRUE 1

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

/* Additional header files */
#include <ucontext.h>
#include <sys/time.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef uint worker_t;

/* Worker status */
#define READY 0
#define SCHEDULED 1 // RUNNING
#define BLOCKED 2
#define TERMINATED 3

#define PSJF 1
#ifndef MLFQ
#define MLFQ 2
#endif

typedef struct TCB {
	// thread Id
	worker_t id;

	// thread status (READY, SCHEDULED (running), BLOCKED)
	int status;

	// thread context
	ucontext_t *context;

	// thread stack
	void *stack;

	// thread priority, 0 is lowest
	int priority;

	// time elapsed since thread initialized and put into queue
	int time_elapsed;

	// context switches
	int context_switches;

	// indicates whether a thread has been scheduled yet
	// used for response time
	int been_scheduled;
} tcb; 

/* mutex struct definition */
typedef struct worker_mutex_t {
	// current thread holding mutex
	tcb* mutex_holder;

	// indicates if mutex is locked, 0 = not locked, 1 = locked
	int is_locked;

} worker_mutex_t;

/* define your data structures here: */

// Node is also used as a linked list of completed threads
typedef struct Node {
	// current thread
	tcb *thread;

	// next thread in queue
	struct Node *next;
} node;

/* Run Queue */
typedef struct Run_Queue {
	// head of the queue
	node *head;

	// tail of the queue
	node *tail;

	// size of run queue
	int size;
} run_queue;

// add new thread at end of run queue
void enqueue(tcb *thread);

// remove thread from head of queue
tcb* dequeue();

// remove specific thread with thread id = id and return id from queue
worker_t remove_thread(worker_t id);

// print queue (debugging)
void print_queue();

// returns pointer to thread in queue with id=id
tcb* find_thread(worker_t id);

/* Completed list functions */

// find thread in node list
int find_thread_in_list(worker_t id);

// add completed thread to completed list of threads
void add_completed_thread(tcb* thread);

// print list of completed threads (for debugging)
void print_completed_list();

/* Timer functions */

// handle the timer (may be removed)
void timer_handle();

// set up/reset timer
void set_up_timer();

/* Blocked queue */
typedef struct Block_Node {
	// thread
	tcb *thread;

	// next thread in queue
	struct Block_Node *next;

	// mutex
	worker_mutex_t *mutex;
} b_node;


typedef struct Block_Queue {
	// head of the queue
	b_node *head;

	// tail of the queue
	b_node *tail;

	// size of block queue
	int size;
} block_queue;

// Redundant, might make modular
void b_enqueue(tcb *thread, worker_mutex_t *mutex);

// Redundant, might make modular
tcb* b_dequeue();

// Redundant, might make modular
void print_b_queue();

/* Function Declarations: */

static void schedule();

static void sched_psjf();

static void sched_mlfq();

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
    *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);

/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);

#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif
