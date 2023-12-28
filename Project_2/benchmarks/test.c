#include <stdio.h>
#include <unistd.h>
#include "../thread-worker.h"

/* General testing for threads */

// void *testFunc(int *threadnum)
// {
// 	for(int i = 0; i < 100000000; i++) {}
// 	printf("Thread %d complete\n", *threadnum);
// 	worker_exit(NULL);
// }

// #define NUM_THREADS 15

// /* A scratch program template on which to call and
//  * test thread-worker library functions as you implement
//  * them.
//  *
//  * You can modify and use this program as much as possible.
//  * This will not be graded.
//  */
// int main(int argc, char **argv) {
//     int thread_ids[NUM_THREADS] = {
//         1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
//     };
    
//     for(int i = 0; i < NUM_THREADS; i++) {
//         worker_create(&thread_ids[i], NULL, testFunc, &i);
//     }

//     for(int i = 0; i < NUM_THREADS; i++) {
//         worker_join(thread_ids[i], NULL);
//     }
    
//     for(int i = 0; i < NUM_THREADS; i++) {
//         worker_exit(thread_ids[i]);
//     }

//     print_app_stats();

// 	return 0;
// }


/* Mutex Testing */
#define NUM_THREADS 5
worker_mutex_t test_mutex;
int shared_resource = 0;

void *testFunc(int *threadnum) {
    worker_mutex_lock(&test_mutex);
    
    int local = shared_resource;
    local++; 
    for(int i = 0; i < 100000000; i++) {}
    shared_resource = local;
    printf("Thread %d running: shared_resource = %d\n", *threadnum, shared_resource); 
    worker_mutex_unlock(&test_mutex);
    worker_exit(NULL);
}

int main() {
    worker_mutex_init(&test_mutex, NULL);

    worker_t thread_ids[NUM_THREADS] = {
        1,2,3,4,5,
    };

    for (long i = 0; i < NUM_THREADS; i++) {
        worker_create(&thread_ids[i], NULL, testFunc, &i);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        worker_join(thread_ids[i], NULL);
    }

    printf("Final shared resource value: %d\n", shared_resource);
    print_app_stats();

    worker_mutex_destroy(&test_mutex);

    return 0;
}