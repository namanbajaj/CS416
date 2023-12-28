/*
 * Add NetID and names of all project partners
 * Naman Bajaj - nb726
 * Mourya Vulupala - mv638
 * CS416
 * iLab Machine: less.cs.rutgers.edu
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/* Part 1 - Step 1 and 2: Do your tricks here
 * Your goal must be to change the stack frame of caller (main function)
 * such that you get to the line after "r2 = *( (int *) 0 )"
 */
void signal_handle(int signalno)
{
    printf("handling segmentation fault!\n");

    int *stack_ptr = (int *)&signalno;
    stack_ptr += 15;               // this offset is based on the stack layout
    *stack_ptr = *stack_ptr + 0x5; // the offending instruction is 5 bytes long
}

int main(int argc, char *argv[])
{
    int r2 = 0;

    /* Step 1: Register signal handler first*/
    // SIGSEGV --> Signal number for a segmentation fault
    signal(SIGSEGV, signal_handle);

    r2 = *((int *)0); // This will generate segmentation fault

    r2 = r2 + 1 * 30;

    printf("result after handling seg fault %d!\n", r2);

    return 0;
}
