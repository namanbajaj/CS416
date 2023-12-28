#include "my_vm.h"

// List all group member's name: Naman Bajaj, Mourya Vulupala
// username of iLab: nb726, mv638

/*
=================================================================================================
PART 1
=================================================================================================
*/

// Physical memory pointer
void *physical_mem;

// Number of physical pages
unsigned long num_physical_pages;

// Number of virtual pages
unsigned long num_virtual_pages;

// Virtual page bitmap
unsigned char *virtual_page_bitmap;

// Physical page bitmap
unsigned char *physical_page_bitmap;

// Page directory
pde_t *page_directory;

// Number of bits in a page offset
unsigned int num_offset_bits;

// Number of bits in a virtual page number
unsigned int num_page_directory_bits;

// Number of bits in a page table number
unsigned int num_page_table_bits;

// Mask for page offset
unsigned int pdi_mask;

// Mask for page table number
unsigned int pti_mask;

struct tlb tlb_store[TLB_ENTRIES];

int current_tlb_entry; // indicates index in TLB where last entry was added
int tlb_hit_count;
int tlb_miss_count;

pthread_mutex_t physical_mem_lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t bitmap_lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t page_directory_lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tlb_lock = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;

static int exit_handler_registered = 0;

/*
Function responsible for allocating and setting your physical memory
*/
void set_physical_mem()
{
    pthread_mutex_lock(&physical_mem_lock);

    if (physical_mem)
    {
        DEBUG_PRINT("Physical memory already initialized\n");
        pthread_mutex_unlock(&physical_mem_lock);
        return;
    }

    // Allocate physical memory using mmap or malloc; this is the total size of your memory you are simulating
    physical_mem = mmap(NULL, MEMSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!physical_mem)
    {
        perror("physical mem allocation error");
        pthread_mutex_unlock(&physical_mem_lock);
        exit(EXIT_FAILURE);
    }

    // HINT: Also calculate the number of physical and virtual pages and allocate virtual and physical bitmaps and initialize them
    num_physical_pages = MEMSIZE / PGSIZE;
    num_virtual_pages = MAX_MEMSIZE / PGSIZE;
    physical_page_bitmap = (unsigned char *)malloc(num_physical_pages * sizeof(unsigned char));
    virtual_page_bitmap = (unsigned char *)malloc(num_virtual_pages * sizeof(unsigned char));

    if (!physical_page_bitmap || !virtual_page_bitmap)
    {
        perror("bitmaps");
        pthread_mutex_unlock(&physical_mem_lock);
        exit(EXIT_FAILURE);
    }

    size_t num_bytes_physical = (num_physical_pages % 8 == 0) ? (num_physical_pages / 8) : (num_physical_pages / 8) + 1;
    memset(physical_page_bitmap, 0, num_bytes_physical);

    size_t num_bytes_virtual = (num_virtual_pages % 8 == 0) ? (num_virtual_pages / 8) : (num_virtual_pages / 8) + 1;
    memset(virtual_page_bitmap, 0, num_bytes_virtual);

    // number of offset bits
    num_offset_bits = (unsigned int) log2f(PGSIZE);
    // number of bits for first level
    num_page_directory_bits = (ADDRESS_SPACE - num_offset_bits) / 2;
    // number of bits for second level (may be unequal)
    num_page_table_bits = ADDRESS_SPACE - num_offset_bits - num_page_directory_bits;
    
    // Masks for getting page directory index and page table index
    pdi_mask = (1 << num_page_directory_bits) - 1;
    pti_mask = (1 << num_page_table_bits) - 1;

    // Set empty page directory
    page_directory = (pde_t *) physical_mem;
    memset(page_directory, 0, PGSIZE);

    set_bit(virtual_page_bitmap, 0);
    set_bit(physical_page_bitmap, 0);
    // DEBUG_PRINT("Bit at index 0 of virtual page bitmap: %d\n", get_bit(virtual_page_bitmap, 0));
    // DEBUG_PRINT("Bit at index 0 of physical page bitmap: %d\n", get_bit(physical_page_bitmap, 0));

    DEBUG_PRINT("All necessary memory has been allocated!\n");
    // DEBUG_PRINT("Physical mem has address: %x\n", (unsigned int)physical_mem);
    // DEBUG_PRINT("Physical mem has size of: %d\n", (int)MEMSIZE);
    // DEBUG_PR INT("Number of physical pages: %d\n", (int)num_physical_pages);
    // DEBUG_PRINT("Number of virtual pages: %d\n", (int)num_virtual_pages);
    DEBUG_PRINT("Number of bytes for physical bitmap: %d\n", (int)num_bytes_physical);
    DEBUG_PRINT("Number of bytes for virtual bitmap: %d\n", (int)num_bytes_virtual);
    // DEBUG_PRINT("Number of offset bits: %d\n", (int)num_offset_bits);
    // DEBUG_PRINT("Number of page directory bits: %d\n", (int)num_page_directory_bits);
    // DEBUG_PRINT("Number of page table bits: %d\n", (int)num_page_table_bits);
    // DEBUG_PRINT("Page directory index mask: %x\n", (unsigned int)pdi_mask);
    // DEBUG_PRINT("Page table index mask: %x\n", (unsigned int)pti_mask);
    // DEBUG_PRINT("Address of page directory: %x\n", (unsigned int)page_directory);


    /*
        Part 2 - TLB
    */
   init_TLB();
   
   pthread_mutex_unlock(&physical_mem_lock);
}

/*
The function takes a virtual address and page directories starting address and
performs translation to return the physical address
*/
pte_t *translate(pde_t *pgdir, void *va)
{
    /* Part 1 HINT: Get the Page directory index (1st level) Then get the
     * 2nd-level-page table index using the virtual address.  Using the page
     * directory index and page table index get the physical address.
     *
     * Part 2 HINT: Check the TLB before performing the translation. If
     * translation exists, then you can return physical address from the TLB.
     */

    // First check the TLB
    pte_t *pa = check_TLB(va);
    if (pa) { // TLB Hit
        return pa;
    }

    // If not in TLB, then perform translation
    // Get page directory index
    int pdi = ((int)va >> (num_offset_bits + num_page_table_bits)) & pdi_mask;
    // Get 2nd level page table index using virtual address
    int pti = ((int)va >> num_offset_bits) & pti_mask;

    if(DEBUG) {
        printf("\n==========TRANSLATE FUNCTION==========\n");
        printf("Virtual address: %x\n", (unsigned int)va);
        printf("Page directory index: %d\n", pdi);
        printf("Page table index: %d\n", pti);
        printf("======================================\n\n");
    }

    // Get page table directory entry
    pde_t *pde = (pde_t *) pgdir[pdi];
    if(pde == NULL) return NULL;
    // Get page table from page directory entry
    pte_t *pte = (pte_t *)&pde[pti];
    // Get physical address
    if(pte != NULL) 
    {
        DEBUG_TLB_PRINT("Adding TLB entry (translate) for virtual address: %p, physical address: %p\n", va, pte);
        add_TLB(va, pte);
        // print_TLB();
        return pte;
    }

    // If translation not successful, then return NULL
    return NULL;
}

/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
int page_map(pde_t *pgdir, void *va, void *pa)
{
    pthread_mutex_lock(&page_directory_lock);

    unsigned int pdi = ((unsigned int)va >> (num_offset_bits + num_page_table_bits)) & pdi_mask;
    unsigned int pti = ((unsigned int)va >> num_offset_bits) & pti_mask;

    // Allocate a new page directory if necessary
    if (!pgdir[pdi]) {
        DEBUG_PRINT("[PAGE MAP]:: Page directory index: %d, Page table index: %d\n", pdi, pti);
        void *new_ptable = get_next_avail(1);
        memset(new_ptable, 0, PGSIZE);
        pgdir[pdi] = (pde_t)new_ptable;
        new_ptable = (pte_t *) pgdir[pdi];
    }

    DEBUG_PRINT("[PAGE MAP]:: Page directory index: %d, Page table index: %d\n", pdi, pti);
    pde_t *page_d = &pgdir[pdi];
    pte_t *page_entry = &page_d[pti];
    DEBUG_PRINT("[PAGE MAP]:: Page directory index: %d, Page table index: %d\n", pdi, pti);
    if(page_entry && *page_entry == 0)
        *page_entry = (pte_t) pa;
    else {
        pthread_mutex_unlock(&page_directory_lock);
        return -1;
    }

    pthread_mutex_unlock(&page_directory_lock);
    return 0;
}


void *get_virtual_address(int num_pages) {
    // pthread_mutex_lock(&bitmap_lock);
    // Use virtual address bitmap to find the next free page

    // Find 'num_pages' consecutive free pages
    int start_page = -1;
    int found_pages = 0;

    // Iterate over the bitmap to find consecutive free pages
    for (int i = 0; i < num_virtual_pages; i++){
        if (!get_bit(virtual_page_bitmap, i)){ // Page is free
            if (start_page == -1){
                start_page = i; // Potential start of the sequence
            }

            found_pages++;

            if (found_pages == num_pages){
                DEBUG_PRINT("Found %d pages starting at index: %d\n", num_pages, start_page);
                // Found enough pages, mark them as used
                for (int j = start_page; j < start_page + num_pages; j++){
                    set_bit(virtual_page_bitmap, j); // Mark as used
                }

                // Calculate the virtual address using the page number and page size
                // Assuming that the start of the virtual memory is at a fixed address
                void *start_address = (void *)((start_page << num_offset_bits));
                // pthread_mutex_unlock(&bitmap_lock);
                return start_address;
            }
        }
        else{
            // Reset if a used page is encountered
            start_page = -1;
            found_pages = 0;
        }
    }

    // pthread_mutex_unlock(&bitmap_lock);
    return NULL;
}

/*
Function that gets the next available page
*/
void *get_next_avail(int num_pages)
{
    pthread_mutex_lock(&physical_mem_lock);
    // Use virtual address bitmap to find the next free page

    // Find 'num_pages' consecutive free pages
    int start_page = -1;
    int found_pages = 0;

    // Iterate over the bitmap to find consecutive free pages
    for (int i = 0; i < num_virtual_pages; i++){
        // if (!virtual_page_bitmap[i]){ // Page is free
        if(get_bit(physical_page_bitmap, i) == 0) {
            if (start_page == -1){
                start_page = i; // Potential start of the sequence
            }

            found_pages++;

            if (found_pages == num_pages){
                // Found enough pages, mark them as used
                for (int j = start_page; j < start_page + num_pages; j++){
                    // virtual_page_bitmap[j] = 1; // Mark as used
                    set_bit(physical_page_bitmap, j);
                }

                // Calculate the virtual address using the page number and page size
                // Assuming that the start of the virtual memory is at a fixed address
                void *start_address = (void *)((start_page * PGSIZE) + physical_mem);
                pthread_mutex_unlock(&physical_mem_lock);
                return start_address;
            }
        }
        else{
            // Reset if a used page is encountered
            start_page = -1;
            found_pages = 0;
        }
    }

    pthread_mutex_unlock(&physical_mem_lock);
    return NULL;
}

/* 
Function responsible for allocating pages and used by the benchmark
*/
void *t_malloc(unsigned int num_bytes)
{
    // If the physical memory is not yet initialized, then allocate and initialize.
    if (!physical_mem){
        set_physical_mem();
    }

    usleep(1000);

    DEBUG_PRINT("Allocating %d bytes\n", num_bytes);

    int num_required_pages = (num_bytes + PGSIZE - 1) / PGSIZE;

    DEBUG_PRINT("Number of required pages: %d\n", num_required_pages);

    void *va = get_virtual_address(num_required_pages);
    usleep(1000);
    if(!va)
    {
        return NULL;
    }
    DEBUG_PRINT("Virtual address of the allocation: %p\n", va);

    void *virtual_page = va;

    for(int i = 0; i < num_required_pages; i++, virtual_page += PGSIZE) {
        void *page = get_next_avail(1);
        if(!page) {
            perror("No more pages available");
            return NULL;
        }

        DEBUG_PRINT("t_malloc: Physical address of the allocation: %p\n", page);

        usleep(1000);
        page_map(page_directory, virtual_page, page);

        DEBUG_PRINT("Mapped virtual address: %p to physical address: %p\n", virtual_page, page);
    }

    DEBUG_PRINT("Address of the allocation: %p\n=======================\n", va);
    
    return va;
}

/* 
Responsible for releasing one or more memory pages using virtual address (va)
*/
void t_free(void *va, int size)
{
    usleep(1000);
    // pthread_mutex_lock(&bitmap_lock);
    DEBUG_PRINT("Freeing %d bytes at address: %p\n", size, va);
    if(!physical_mem) {
        perror("Physical memory not initialized");
        pthread_mutex_unlock(&bitmap_lock);
        return;
    }

    // Calculate the number of pages to free
    int num_pages = (size + PGSIZE - 1) / PGSIZE;
    void *current_va = va;
    int virtual_page_number = ((unsigned long) current_va >> num_offset_bits);

    for (int i = 0; i < num_pages; i++)
    {
        // Translate the virtual address to its page table entry
        pte_t *pte = translate(page_directory, current_va);
        DEBUG_PRINT("Physical address in t_free: %p\n", pte);
        if (*pte)
        {
            // Mark the page as free in the virtual page bitmap
            int page_number = ((void *)pte - physical_mem) / PGSIZE;
            DEBUG_PRINT("t_free: Page number: %d\n", page_number);
            DEBUG_PRINT("t_free: pte: %p\n", pte);
            DEBUG_PRINT("t_free: *pte: %ld\n", *pte);
            DEBUG_PRINT("t_free: physical_mem: %p\n", physical_mem);
            DEBUG_PRINT("t_free: PGSIZE: %d\n", PGSIZE);

            clear_bit(physical_page_bitmap, page_number);
            clear_bit(virtual_page_bitmap, virtual_page_number++);

            DEBUG_PRINT("t_free: Bits cleared\n");

            // Invalidate the page table entry
            *pte = 0;

            // Invalidate the corresponding TLB entry
            invalidate_TLB_entry(current_va);

            // Move to the next page
            current_va = (void *)((char *)current_va + PGSIZE);
        }
        else
        {
            // Handle the case where the page table entry does not exist
            perror("Page table entry does not exist");
        }
    }
    // pthread_mutex_unlock(&bitmap_lock);
    DEBUG_PRINT("Freed %d bytes at address: %p\n", size, va);

    // print_TLB();
    DEBUG_MULTITHREAD_PRINT("TLB entry check: %d\n", check_all_tlb_entries());

    // All pages have been freed, so check if we can exit
    if(exit_handler_registered) {
        return;
    }
    if(atexit(print_TLB_missrate) == 0) {
        exit_handler_registered = 1;
    }
    else {
        perror("Failed to register exit handler");
    }
}

int perform_IO(void *va, void *val, int size, int is_put) {
    pte_t *pte = translate(page_directory, va);
    if(!pte){
        DEBUG_PRINT("PTE NOT FOUND\n");
        return -1;
    }
    DEBUG_PRINT("Physical address 1 (%s): %p\n", is_put ? "PUT" : "GET", pte);

    void *phys_addr = (void *) pte;
    DEBUG_PRINT("Physical address 2 (%s): %p\n", is_put ? "PUT" : "GET", phys_addr);
    unsigned long int page_index = (phys_addr - physical_mem) / PGSIZE;
    DEBUG_PRINT("Page index: %ld\n", page_index);
    if(page_index++ >= MEMSIZE / PGSIZE){
        DEBUG_PRINT("Page index out of bounds\n");
        return -1;
    }

    void *next_page = (physical_mem + (PGSIZE * page_index)) - 1;
    int page_bytes = next_page - phys_addr;

    int io_bytes = size;
    char *sd = val;
    char *virtual_address = va;

    while(io_bytes > 0) {
        DEBUG_PRINT("IO:: HERE\n");
        if(io_bytes <= page_bytes) {
            is_put && memcpy(phys_addr, sd, io_bytes);
            !is_put && memcpy(sd, phys_addr, io_bytes);
            io_bytes = 0;
        }
        else {
            is_put && memcpy(phys_addr, sd, page_bytes);
            !is_put && memcpy(sd, phys_addr, page_bytes);
            
            sd = sd + page_bytes + 1;
            virtual_address = va + page_bytes + 1;

            pte = translate(page_directory, virtual_address);
            if(!pte) {
                DEBUG_PRINT("PTE NOT FOUND\n");
                return -1;
            }

            phys_addr = (void *) *pte;
            io_bytes -= page_bytes;
            page_bytes = PGSIZE - 1;
        }
    }
    if(is_put == 1){
        DEBUG_PRINT("Putting value %d at virtual address: %p\n", *(int *)val, va);
    }
    else {
        DEBUG_PRINT("Getting value at address: %p, Received value: %d\n", va, *(int *)val);
    }

    return 0;
}

/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
 * The function returns 0 if the put is successfull and -1 otherwise.
 */
int put_value(void *va, void *val, int size)
{
    DEBUG_PRINT("\nWant to put %d bytes at address: %p with value %d\n", size, va, *(int *)val);
    return perform_IO(va, val, size, 1);
}

/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size)
{
    DEBUG_PRINT("\nWant to get %d bytes at address: %p\n", size, va);
    perform_IO(va, val, size, 0);
}

/*
This function receives two matrices mat1 and mat2 as an argument with size
argument representing the number of rows and columns. After performing matrix
multiplication, copy the result to answer.
*/
void mat_mult(void *mat1, void *mat2, int size, void *answer)
{

    /* Hint: You will index as [i * size + j] where  "i, j" are the indices of the
     * matrix accessed. Similar to the code in test.c, you will use get_value() to
     * load each element and perform multiplication. Take a look at test.c! In addition to
     * getting the values from two matrices, you will perform multiplication and
     * store the result to the "answer array"
     */
    int x, y, val_size = sizeof(int);
    int i, j, k;
    for (i = 0; i < size; i++)
    {
        for (j = 0; j < size; j++)
        {
            unsigned int a, b, c = 0;
            for (k = 0; k < size; k++)
            {
                int address_a = (unsigned int)mat1 + ((i * size * sizeof(int))) + (k * sizeof(int));
                int address_b = (unsigned int)mat2 + ((k * size * sizeof(int))) + (j * sizeof(int));
                get_value((void *)address_a, &a, sizeof(int));
                get_value((void *)address_b, &b, sizeof(int));
                // printf("Values at the index: %d, %d, %d, %d, %d\n",
                //     a, b, size, (i * size + k), (k * size + j));
                c += (a * b);
            }
            int address_c = (unsigned int)answer + ((i * size * sizeof(int))) + (j * sizeof(int));
            // printf("This is the c: %d, address: %x!\n", c, address_c);
            put_value((void *)address_c, (void *)&c, sizeof(int));
        }
    }
}

/*
=================================================================================================
PART 2
=================================================================================================
*/

// Function to initialize the TLB
void init_TLB()
{
    DEBUG_TLB_PRINT("Initializing TLB\n");

    for (int i = 0; i < TLB_ENTRIES; ++i){
        tlb_store[i].valid = 0; // Mark all entries as invalid initially
    }
    print_TLB();
    current_tlb_entry = 0; // Start replacing from the first index
    tlb_hit_count = 0;
    tlb_miss_count = 0;
}

// Function to invalidate a TLB entry
void invalidate_TLB_entry(void *va)
{
    pthread_mutex_lock(&tlb_lock);
    for (int i = 0; i < TLB_ENTRIES; ++i) {
        if (tlb_store[i].valid && tlb_store[i].virtual_address == va) {
            tlb_store[i].valid = 0; // Invalidate the entry
            DEBUG_TLB_PRINT("Invalidated TLB entry for virtual address: %p\n", va);
            break;            // Assuming no duplicate entries for the same virtual address
        }
    }
    pthread_mutex_unlock(&tlb_lock);
}

/*
 * Part 2: Add a virtual to physical page translation to the TLB.
 * Feel free to extend the function arguments or return type.
 * This function will use a FIFO replacement policy if the TLB is full.
 */
int add_TLB(void *va, void *pa)
{
    pthread_mutex_lock(&tlb_lock);
    DEBUG_TLB_PRINT("Adding TLB entry (add_TLB) for virtual address: %p, physical address: %p\n", va, pa);

    // Check if the entry is already in the TLB
    for (int i = 0; i < TLB_ENTRIES; i++)
    {
        if (tlb_store[i].valid && tlb_store[i].virtual_address == va)
        {
            // Entry is already in the TLB, update the physical address
            tlb_store[i].physical_address = pa;
            pthread_mutex_unlock(&tlb_lock);
            return 0; // Success
        }
    }

    // If it's not in the TLB, use the next replaceable entry
    tlb_store[current_tlb_entry].virtual_address = va;
    tlb_store[current_tlb_entry].physical_address = pa;
    tlb_store[current_tlb_entry].valid = 1; // Mark the entry as valid

    // Update the next replaceable entry index using FIFO policy
    current_tlb_entry = (current_tlb_entry + 1) % TLB_ENTRIES;
    
    pthread_mutex_unlock(&tlb_lock);

    return 0; // Success
}

/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
pte_t *check_TLB(void *va)
{
    pthread_mutex_lock(&tlb_lock);
    DEBUG_TLB_PRINT("Checking TLB for virtual address: %p\n", va);

    for (int i = 0; i < TLB_ENTRIES; i++){
        if (tlb_store[i].valid && tlb_store[i].virtual_address == va){
            // TLB Hit
            DEBUG_TLB_PRINT("TLB hit for virtual address: %p\n", va);
            tlb_hit_count++;
            pthread_mutex_unlock(&tlb_lock);
            return (pte_t *)tlb_store[i].physical_address;
        }
    }

    // TLB Miss
    DEBUG_TLB_PRINT("TLB miss for virtual address: %p\n", va);
    tlb_miss_count++;
    pthread_mutex_unlock(&tlb_lock);
    return NULL;
}

/*
 * Part 2: Print TLB miss rate.
 * Feel free to extend the function arguments or return type.
 */
void print_TLB_missrate()
{
    // Ensure we don't divide by zero
    if (tlb_hit_count + tlb_miss_count == 0)
    {
        fprintf(stderr, "No TLB accesses have been made.\n");
        return;
    }

    double miss_rate = (double)tlb_miss_count / (tlb_hit_count + tlb_miss_count);

    fprintf(stderr, "TLB miss rate: %lf \n", miss_rate);
}

void print_TLB() {
    DEBUG_TLB_PRINT("======================Printing TLB======================\n");
    for(int i = 0; i < TLB_ENTRIES; i++) {
        DEBUG_TLB_PRINT("TLB entry %d: Virtual address: %p, Physical address: %p, Is Valid: %d\n", 
        i, 
        tlb_store[i].virtual_address, 
        tlb_store[i].physical_address,
        tlb_store[i].valid);
    }
}

int check_all_tlb_entries() {
    for(int i = 0; i < TLB_ENTRIES; i++) {
        if(tlb_store[i].valid) {
            return 1;
        }
    }
    return 0;
}

// Bit functions
void set_bit(unsigned char *bitmap, int index)
{
    pthread_mutex_lock(&bitmap_lock);
    bitmap[index / 8] |= (1 << (index % 8));
    pthread_mutex_unlock(&bitmap_lock);
}

void clear_bit(unsigned char *bitmap, int index)
{
    pthread_mutex_lock(&bitmap_lock);
    bitmap[index / 8] &= ~(1 << (index % 8));
    pthread_mutex_unlock(&bitmap_lock);
}

int get_bit(unsigned char *bitmap, int index)
{
    return (bitmap[index / 8] & (1 << (index % 8))) != 0;
}