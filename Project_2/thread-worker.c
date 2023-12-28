// File:	thread-worker.c

// List all group member's name: Naman Bajaj, Mourya Vulupala
// username of iLab: nb726, mv638
// iLab Server: cs416f23-18

#include "thread-worker.h"

// Global counter for total context switches and
// average turn around and response time
long tot_cntx_switches = 0;
double avg_turn_time = 0;
double avg_resp_time = 0;

/* Other variables */
run_queue *queue = NULL;		// main queue for program
block_queue *b_queue = NULL;	// queue which will have threads that compete for mutexes
node *completed_threads = NULL; // list of completed threads

tcb *current_running_thread; // currently running thread

ucontext_t *sched_context = NULL; // scheduler context
void *sched_stack = NULL;		  // scheduler stack
int schedule_init = 0;			  // used to run specific code the first time schedule is called

tcb *main_thread_tcb = NULL;	 // main thread tcb
ucontext_t *main_thread_context; // worker create context (used as main thread context)

// Use sigaction to register signal handler
struct sigaction sa;

// Create timer struct
struct itimerval timer;

// Number of completed threads (for average turnaround time)
int completed_thread_count = 0;

// Number of threads that have ran for the first time (for average response time)
int ran_thread_count = 0;

// Number of quantums elapsed
int quantums_elapsed = 0;

int sched = 0;

int number_of_times_thread_0 = 0;

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg)
{
	// - create Thread Control Block (TCB)
	struct TCB *t = (struct TCB*) malloc(sizeof(struct TCB));

	t->id = *thread;
	t->status = READY;
	t->priority = 0;
	t->time_elapsed = 0;
	t->context_switches = 0;
	t->been_scheduled = 0;

	// - allocate space of stack for this thread to run
	void *stack = malloc(SIGSTKSZ);
	t->stack = stack;

	// - create and initialize the context of this worker thread
	ucontext_t *uc = (ucontext_t*) malloc(sizeof(ucontext_t));
	if (getcontext(uc) < 0){
		perror("Unable to allocate context (getcontext)");
		exit(EXIT_FAILURE);
	}
	uc->uc_link = NULL;
	uc->uc_stack.ss_sp = stack;
	uc->uc_stack.ss_size = SIGSTKSZ;
	uc->uc_stack.ss_flags = 0;
	makecontext(uc, (void (*)(void)) function, 1, arg);

	t->context = uc;

	/*
	* First time calling worker_create
	* Need to create scheduler context for swapping
	* Once scheduler context created
	* Swap to it to allow scheduler to handle thread execution 
	*/
	if(!sched_context) {
		// Set up timer signals
		memset (&sa, 0, sizeof(sa));
		sa.sa_handler = &timer_handle;
		sigaction (SIGPROF, &sa, NULL);

		set_up_timer();

		timer.it_interval.tv_usec = 0;
		timer.it_interval.tv_sec = 0;
		timer.it_value.tv_usec = QUANTUM;
		timer.it_value.tv_sec = 0;

		/* Create scheduler context */
		sched_context = (ucontext_t*) malloc(sizeof(ucontext_t));
		if (getcontext(sched_context) < 0){
			perror("Unable to allocate scheduler context (getcontext)");
			exit(EXIT_FAILURE);
		}

		sched_stack = malloc(SIGSTKSZ);

		sched_context->uc_link = NULL;
		sched_context->uc_stack.ss_sp = sched_stack;
		sched_context->uc_stack.ss_size = SIGSTKSZ;
		sched_context->uc_stack.ss_flags = 0;

		makecontext(sched_context, (void *)&schedule, 1, arg);

		/* Create main TCB and its context */
		main_thread_tcb = (tcb*) malloc(sizeof(tcb));
		main_thread_tcb->id = 0;
		main_thread_tcb->status = READY;
		main_thread_tcb->priority = 0;
		main_thread_tcb->time_elapsed = 0;

		main_thread_context = (ucontext_t*) malloc(sizeof(ucontext_t));
		if (getcontext(main_thread_context) < 0){
			perror("Unable to allocate scheduler context (getcontext)");
			exit(EXIT_FAILURE);
		}

		main_thread_tcb->context = main_thread_context;

		// Needed for setcontext call back to this function
		if(current_running_thread != NULL && current_running_thread->status == SCHEDULED) {
			return 0;
		}

		enqueue(t);
		enqueue(main_thread_tcb);

		// swapcontext(main_thread_context, sched_context);

		// - after everything is set, push this thread into run queue and make it ready for the execution.

		return 0;
	}

	enqueue(t);	
	// swapcontext(main_thread_context, sched_context);

	if(current_running_thread != NULL && current_running_thread->status == SCHEDULED) {
		return 0;
	}

	setcontext(sched_context);

	return 0;
};

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{
	// Queue doesn't exist or is empty (no other threads to run)
	if(!queue || queue->size == 0) {
		return -1;
	}

	// - change worker thread's state from Running to Ready
	if(current_running_thread){
		current_running_thread->status = READY;
		// - save context of this thread to its thread control block
		if(getcontext(current_running_thread->context) < 0) {
			perror("getcontext error");
			exit(EXIT_FAILURE);
		}
	}



	// if(current_running_thread->status == SCHEDULED) {
	// 	return 0;
	// }

	// push current thread to back
	// remove_thread(current_running_thread->id);
	// print_queue();
	// printf("Queue size is %d\n", queue->size);
	if(current_running_thread){
		current_running_thread->context_switches++;
	}
	enqueue(current_running_thread);
	current_running_thread = NULL;

	// - switch from thread context to scheduler context
	setcontext(sched_context);

	return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr){
	// - de-allocate any dynamic memory created when starting this thread
	// current_running_thread->status = TERMINATED; // not necessary, just here for checking
	// remove_thread(current_running_thread->id);
	// printf("Finished thread: id=%d, time=%d, switches=%d\n", current_running_thread->id, current_running_thread->time_elapsed, ++current_running_thread->context_switches);
	current_running_thread->status = TERMINATED;
	tot_cntx_switches += current_running_thread->context_switches;
	avg_turn_time = (avg_turn_time * (double) completed_thread_count + (double) current_running_thread->time_elapsed) / ((double) (completed_thread_count + 1));
	completed_thread_count++;
	// free(current_running_thread->stack);
	// free(current_running_thread);
	add_completed_thread(current_running_thread);
	if(current_running_thread) {
		current_running_thread = NULL;
	}

	setcontext(sched_context);
};

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr)
{
	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread
	// tcb* desired_thread = find_thread(thread);
	// if(desired_thread != NULL) {
	// 	printf("FOUND THREAD\n");
	// 	if(desired_thread->status != TERMINATED) {
	// 		worker_yield();
	// 	}
	// }

	while(find_thread_in_list(thread) == 0) {
		worker_yield();
	}

	return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
	//- initialize data structures for this mutex
	// mutex = (worker_mutex_t*) malloc(sizeof(mutex));
	mutex->mutex_holder = NULL;
	mutex->is_locked = 0;

	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{
	// - use the built-in test-and-set atomic function to test the mutex
	while(__sync_lock_test_and_set(&(mutex->is_locked), 1)) {
		// - if acquiring mutex fails, push current thread into block list and
		current_running_thread->status = BLOCKED;
		b_enqueue(current_running_thread, mutex);

		// - context switch to the scheduler thread
		setcontext(sched_context);
	}

	// print_b_queue();
	// print_queue();

	// - if the mutex is acquired successfully, enter the critical section
	mutex->mutex_holder = current_running_thread;

	return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
	if(mutex->mutex_holder != current_running_thread) {
		return -1;
	}

	// - release mutex and make it available again.
	__sync_lock_release(&(mutex->is_locked));
	mutex->mutex_holder = NULL;

	// - put threads in block list to run queue
	// - so that they could compete for mutex later.
	tcb* next_block_thread = b_dequeue();
	while(next_block_thread != NULL) {
		enqueue(next_block_thread);
		next_block_thread = b_dequeue();
	}

	return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
	// - de-allocate dynamic memory created in worker_mutex_init
	worker_mutex_unlock(mutex);
	// free(mutex);

	return 0;
};

/* scheduler */
static void schedule()
{
	// printf("HERE2\n");
	// print_queue();

	if (sched == PSJF)
		sched_psjf();
	else if (sched == MLFQ)
		sched_mlfq();

	while (1)
	{
		// swapcontext(sched_context, w_c_context); // do to check if there are any more threads to be added

		if (!queue || (current_running_thread == NULL && queue->size == 0) || (current_running_thread == NULL && queue->size == 1 && queue->head->thread->id == 0))
		{
			// printf("No threads\n");
			// swapcontext(sched_context, w_c_context);
			if (current_running_thread == NULL && queue->size == 1 && queue->head->thread->id == 0)
			{
				// printf("RETURNING TO MAIN\n");
				print_app_stats();
				setcontext(main_thread_context);
			}

			// return;
		}

		if (current_running_thread == NULL)
		{
			current_running_thread = dequeue();
			if (current_running_thread != NULL)
			{
				// if(!current_running_thread->status) {
				// 	return;
				// }
				current_running_thread->status = SCHEDULED;
				if (current_running_thread->been_scheduled == 0)
				{
					avg_resp_time = (avg_resp_time * (double)ran_thread_count + ((double)quantums_elapsed * QUANTUM) / (1000.0)) / ((double)(ran_thread_count + 1));
					current_running_thread->been_scheduled = 1;
					ran_thread_count++;
				}
			}
			else
			{
				return;
			}
		}
		else if (current_running_thread != NULL)
		{
			current_running_thread->status = READY;
			// remove_thread(current_running_thread->id);
			current_running_thread->context_switches++;
			enqueue(current_running_thread);
			current_running_thread = dequeue();
			current_running_thread->status = SCHEDULED;

			if (current_running_thread->been_scheduled == 0)
			{
				avg_resp_time = (avg_resp_time * (double)ran_thread_count + (double)current_running_thread->time_elapsed) / ((double)(ran_thread_count + 1));
				current_running_thread->been_scheduled = 1;
				ran_thread_count++;
			}
		}

		// print_queue();

		// printf("Switching to thread %d\n", current_running_thread->id);
		if(current_running_thread->id==0 && number_of_times_thread_0++ == 100) {
			return;
		}

		// Set the timer up (start the timer)
		set_up_timer();
		setitimer(ITIMER_PROF, &timer, NULL);
		current_running_thread->context_switches++;
		setcontext(current_running_thread->context);
	}

	// - every time a timer interrupt occurs, your worker thread library
	// should be contexted switched from a thread context to this
	// schedule() function

	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ)


// YOUR CODE HERE
// swapcontext(sched_context, w_c_context);

// - schedule policy
#ifndef MLFQ
		// Choose PSJF
#else
		// Choose MLFQ
#endif
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
void sched_psjf()
{
	// Check if there are any threads to schedule
	if (!queue || queue->size == 0)
	{
		// printf("No threads in the run queue to schedule.\n");
		return;
	}

	// Choose the thread with the shortest expected running time that is READY
	node *current = queue->head;
	tcb *selected_thread = NULL;

	while (current)
	{
		if (current->thread->status == READY)
		{
			if (!selected_thread || current->thread->time_elapsed < selected_thread->time_elapsed)
			{
				selected_thread = current->thread;
			}
		}
		current = current->next;
	}

	// If a currently running thread has longer expected running time than the selected thread, preempt it
	if (current_running_thread && current_running_thread->time_elapsed > selected_thread->time_elapsed)
	{
		enqueue(current_running_thread);
		current_running_thread->status = READY;
	}

	// If we found a thread to run, switch context to it
	if (selected_thread)
	{
		selected_thread->status = SCHEDULED;
		current_running_thread = selected_thread;
		setcontext(selected_thread->context);
	}
}

/* Preemptive MLFQ scheduling algorithm */
void sched_mlfq()
{
	// Check if there are any threads to schedule
	if (!queue || queue->size == 0)
	{
		// printf("No threads in the run queue to schedule.\n");
		return;
	}

	// Choose the highest-priority thread that is READY
	node *current = queue->head;
	tcb *selected_thread = NULL;

	while (current)
	{
		if (current->thread->status == READY)
		{
			if (!selected_thread || current->thread->priority < selected_thread->priority)
			{
				selected_thread = current->thread;
			}
		}
		current = current->next;
	}

	// If we found a thread to run, switch context to it
	if (selected_thread)
	{
		selected_thread->status = SCHEDULED;
		current_running_thread = selected_thread;
		setcontext(selected_thread->context);
	}
}

// DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void)
{
	fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
	fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
	fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}

// Feel free to add any other functions you need

/* Run queue implementation*/
void enqueue(tcb *thread)
{
	node *new_thread = (node *)malloc(sizeof(node));
	if (!new_thread)
	{
		perror("Unable to allocate new node");
		exit(EXIT_FAILURE);
	}

	new_thread->thread = thread;
	new_thread->next = NULL;

	// Initialize all necessary data structures (first thread being added)
	if (!queue)
	{
		queue = (run_queue *)malloc(sizeof(run_queue));
		queue->head = NULL;
		queue->tail = NULL;
		queue->size = 0;

		b_queue = (block_queue *)malloc(sizeof(block_queue));
		b_queue->head = NULL;
		b_queue->tail = NULL;
		b_queue->size = 0;

		completed_threads = (node *)malloc(sizeof(node *));
		completed_threads->thread = NULL;
		completed_threads->next = NULL;
	}

	if (queue->tail)
	{
		// Run queue is not empty
		queue->tail->next = new_thread;
	}
	else
	{
		// Run queue is empty
		queue->head = new_thread;
	}

	queue->tail = new_thread;
	queue->size++;
}

tcb *dequeue()
{
	if (queue->size == 0)
	{
		// The queue is empty, nothing to dequeue
		return NULL;
	}

	// Save a reference to the current head of the queue and its thread
	node *head_node = queue->head;
	tcb *dequeued_thread = head_node->thread;

	// Move the head pointer to the next node in the queue
	queue->head = head_node->next;

	// If this was the last node in the queue, update the tail to NULL
	if (queue->head == NULL)
	{
		queue->tail = NULL;
	}

	// Update the size of the queue
	queue->size--;

	// Free the memory of the dequeued node
	free(head_node);

	// Return the thread from the dequeued node
	return dequeued_thread;
}

worker_t remove_thread(worker_t id)
{
	node *current = queue->head;
	node *previous = NULL;

	while (current)
	{
		if (current->thread->id == id)
		{
			// It's not the first node
			if (previous)
			{
				previous->next = current->next;
				// Last node, update tail
				if (!current->next)
				{
					queue->tail = previous;
				}
			}
			// It is the first node
			else
			{
				queue->head = current->next;
				if (!queue->head)
				{
					// List is now empty
					queue->tail = NULL;
				}
			}

			worker_t id = current->thread->id;
			free(current);
			queue->size--;
			return id;
		}
		previous = current;
		current = current->next;
	}

	// Could not find thread with given id
	// exit(EXIT_FAILURE);
}

void print_queue()
{
	// Check if the queue is empty
	if (queue == NULL || queue->head == NULL)
	{
		printf("Queue is empty\n");
		return;
	}

	// Pointer to the first node in the queue
	node *current = queue->head;

	printf("Threads in the queue (size=%d):\n", queue->size);
	printf("==============================================\n");

	// Loop through each node in the queue
	while (current != NULL)
	{
		tcb *thread_block = current->thread;
		if (thread_block != NULL)
		{
			printf("\tThread ID: %d\n", thread_block->id);
			printf("\tStatus: %s\n",
				   thread_block->status == READY ? "READY" : thread_block->status == SCHEDULED ? "SCHEDULED"
														 : thread_block->status == BLOCKED	   ? "BLOCKED"
														 : thread_block->status == TERMINATED  ? "TERMINATED"
																							   : "UNKNOWN");
			printf("\tStack address: %p\n", thread_block->stack);
			printf("\tPriority: %d\n", thread_block->priority);
			printf("\tTime elapsed: %d\n", thread_block->time_elapsed);
			printf("==============================================\n");
		}

		// Move to the next node in the queue
		current = current->next;
	}
}

tcb *find_thread(worker_t id)
{
	run_queue *first = queue;
	while (first != NULL && first->head != NULL)
	{
		// printf("testing %d %d\n", first->head->thread->id, id);
		if (first->head->thread->id == id)
		{
			return first->head->thread;
		}
		first->head = first->head->next;
	}

	return NULL;
}

void timer_handle(int sig_num)
{
	if (!current_running_thread)
	{
		setcontext(sched_context);
	}

	current_running_thread->time_elapsed += QUANTUM / 1000;
	quantums_elapsed++;
	current_running_thread->status = READY;

	if (getcontext(current_running_thread->context) < 0)
	{
		perror("getcontext error");
		exit(EXIT_FAILURE);
	}

	if (current_running_thread && current_running_thread->status == SCHEDULED)
	{
		return;
	}

	// printf("Timer went off, thread %d interrupted\n", current_running_thread->id);
	// remove_thread(current_running_thread->id);
	// current_running_thread->context_switches++;
	enqueue(current_running_thread);
	current_running_thread = NULL;
	setcontext(sched_context);
	// swapcontext(current_running_thread->context, sched_context);
}

void set_up_timer()
{
	// Set up what the timer should reset to after the timer goes off
	timer.it_interval.tv_usec = 0;
	timer.it_interval.tv_sec = 0;

	// Set up the current timer to go off in QUANTUM time
	timer.it_value.tv_usec = QUANTUM;
	timer.it_value.tv_sec = 0;
}

int find_thread_in_list(worker_t id)
{
	node *current = completed_threads; // Start with the head of the list

	while (current != NULL)
	{ // Traverse the list until the end
		// printf("VALID", current->thread);
		// printf("VALID", current->thread->id);
		if (!current->thread)
		{
			break;
		}

		if (current->thread->id == id)
		{
			// printf("Thread with id %d found in list\n", id);
			return 1; // Thread found
		}
		current = current->next; // Move to the next node in the list
	}

	// printf("Thread with id %d not found in list\n", id);
	return 0; // Thread not found after traversing the whole list
}

void print_completed_list()
{
	if (completed_threads->thread == NULL)
	{
		printf("No completed threads.\n");
		return;
	}
	printf("Thread Completion List\n==================================\n");

	node *current = completed_threads;
	int count = 1;
	while (current != NULL)
	{
		if (current->thread)
		{
			printf("Thread %d:\n", count++);
			printf("\tThread ID: %d\n", current->thread->id);
			printf("\tStatus: %s\n",
				   current->thread->status == READY ? "READY" : current->thread->status == SCHEDULED ? "SCHEDULED"
															: current->thread->status == BLOCKED	 ? "BLOCKED"
															: current->thread->status == TERMINATED	 ? "TERMINATED"
																									 : "UNKNOWN");
			printf("\tThread Priority: %d\n", current->thread->priority);
			printf("\tTime Elapsed: %d\n", current->thread->time_elapsed);
			printf("\n");
			current = current->next;
		}
		else
		{
			break;
		}
	}
}

void add_completed_thread(tcb *thread)
{
	node *new_node = (node *)malloc(sizeof(node)); // Corrected to sizeof(node) instead of sizeof(node*)
	if (new_node == NULL)
	{
		perror("Failed to allocate memory for new completed thread node");
		return;
	}

	new_node->thread = thread;
	new_node->next = NULL;

	if (completed_threads->thread == NULL)
	{
		completed_threads->thread = thread;
	}
	else
	{
		node *current = completed_threads;
		while (current->next != NULL)
		{
			current = current->next;
		}
		current->next = new_node;
	}

	// print_completed_list();
}

void b_enqueue(tcb *thread, worker_mutex_t *mutex)
{
	b_node *new_thread = (b_node *)malloc(sizeof(b_node));
	if (!new_thread)
	{
		perror("Unable to allocate new node");
		exit(EXIT_FAILURE);
	}

	new_thread->thread = thread;
	new_thread->next = NULL;
	new_thread->mutex = mutex;

	// Initialize all data structures
	// Should never be true, but just in case
	if (!b_queue)
	{
		b_queue = (block_queue *)malloc(sizeof(block_queue));
		b_queue->head = NULL;
		b_queue->tail = NULL;
		b_queue->size = 0;
	}

	if (b_queue->tail)
	{
		b_queue->tail->next = new_thread;
	}
	else
	{
		b_queue->head = new_thread;
	}

	b_queue->tail = new_thread;
	b_queue->size++;
}

tcb *b_dequeue()
{
	if (b_queue->size == 0)
	{
		// The queue is empty, nothing to dequeue
		return NULL;
	}

	// Save a reference to the current head of the queue and its thread
	b_node *head_node = b_queue->head;
	tcb *dequeued_thread = head_node->thread;

	// Move the head pointer to the next node in the queue
	b_queue->head = head_node->next;

	// If this was the last node in the queue, update the tail to NULL
	if (b_queue->head == NULL)
	{
		b_queue->tail = NULL;
	}

	// Update the size of the queue
	b_queue->size--;

	// Free the memory of the dequeued node
	free(head_node);

	// Return the thread from the dequeued node
	return dequeued_thread;
}

void print_b_queue()
{
	// Check if the queue is empty
	if (b_queue == NULL || b_queue->head == NULL)
	{
		printf("Blocked queue is empty\n");
		return;
	}

	// Pointer to the first node in the queue
	b_node *current = b_queue->head;

	printf("Threads in the b_queue (size=%d):\n", b_queue->size);
	printf("==============================================\n");

	// Loop through each node in the queue
	while (current != NULL)
	{
		tcb *thread_block = current->thread;
		if (thread_block != NULL)
		{
			printf("\tThread ID: %d\n", thread_block->id);
			printf("\tStatus: %s\n",
				   thread_block->status == READY ? "READY" : thread_block->status == SCHEDULED ? "SCHEDULED"
														 : thread_block->status == BLOCKED	   ? "BLOCKED"
														 : thread_block->status == TERMINATED  ? "TERMINATED"
																							   : "UNKNOWN");
			printf("\tMutex held?: %s\n", current->mutex->is_locked ? "Yes" : "No");
			printf("\tStack address: %p\n", thread_block->stack);
			printf("\tPriority: %d\n", thread_block->priority);
			printf("\tTime elapsed: %d\n", thread_block->time_elapsed);
			printf("==============================================\n");
		}

		// Move to the next node in the queue
		current = current->next;
	}
}