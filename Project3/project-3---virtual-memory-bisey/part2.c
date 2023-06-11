#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define TLB_SIZE 16
#define PAGES 1024
#define FRAMES 256 // limited by the size of the physical memory (256 frames)
#define PAGE_MASK 0xFFC00
#define PAGE_SIZE 1024
#define OFFSET_BITS 10
#define OFFSET_MASK 0x003FF

#define MEMORY_SIZE FRAMES * PAGE_SIZE // 256 frames * 1024 bytes per frame = 262144 bytes
#define BACKING_SIZE FRAMES * PAGE_SIZE // 256 frames * 1024 bytes per frame = 262144 bytes

// Max number of characters per line of input file to read.
#define BUFFER_SIZE 10

struct tlbentry {
    unsigned int logical;
    unsigned int physical;
    unsigned int reference; // Reference bit for second chance
};

int chance[FRAMES]; // Second chance array to keep track of the second chance bit for each frame in the main memory

// The index of the next frame to consider evicting in the second chance algorithm.
int next_frame = 0;

// TLB is kept track of as a circular array, with the oldest element being overwritten once the TLB is full.
struct tlbentry tlb[TLB_SIZE];
// number of inserts into TLB that have been completed. Use as tlbindex % TLB_SIZE for the index of the next TLB line to use.
int tlbindex = 0;

// pagetable[logical_page] is the physical page number for logical page. Value is -1 if that logical page isn't yet in the table.
int pagetable[PAGES]; // to keep track of the physical page number for logical page

signed char main_memory[MEMORY_SIZE];

// Pointer to memory mapped backing file
signed char *backing;

int using_lru = -1;

int lru[FRAMES]; // LRU array to keep track of the least recently used frame in the main memory

int max(int a, int b){
    if (a > b)
        return a;
    return b;
}

int search_tlb(unsigned int logical_page) {
    for (int i = 0; i < TLB_SIZE; i++) { // Iterate through the TLB to find the logical page
        if (tlb[i].logical == logical_page) { // If the page is found in the TLB
            tlb[i].reference = 1; // Set the reference bit to 1 for the page
            return tlb[i].physical; // Return the physical page number for the logical page
        }
    }
    return -1; // Not found in TLB
}

void add_to_tlb(unsigned int logical, unsigned int physical) { // Add a new entry to the TLB
    while (1) {
        if (tlb[tlbindex].reference == 0) { // If the reference bit is 0, we can evict this page
            tlb[tlbindex].logical = logical; // Add the new entry to the TLB at the current index
            tlb[tlbindex].physical = physical; // Add the new entry to the TLB at the current index
            tlb[tlbindex].reference = 1; // Set the reference bit to 1 for the new entry
            tlbindex = (tlbindex + 1) % TLB_SIZE; // Move to next page
            break;  // We're done adding to the TLB
        } else {
            tlb[tlbindex].reference = 0; // Clear reference bit if it's set
            tlbindex = (tlbindex + 1) % TLB_SIZE; // Move to next page
        }
    }
}

int main(int argc, const char *argv[]){
    if (argc != 5) {
        fprintf(stderr, "Usage ./virtmem backingstore input -p 0/1\n");
        exit(1);
    }

    if (!strcmp(argv[4], "0")){
        using_lru = 0;
    }
    else if (!strcmp(argv[4], "1")){
        using_lru = 1;
    } else{
        fprintf(stderr, "Usage ./virtmem backingstore input -p 0/1\n");
        exit(1);
    }

    const char *backing_filename = argv[1];
    int backing_fd = open(backing_filename, O_RDONLY);
    backing = mmap(0, BACKING_SIZE, PROT_READ, MAP_PRIVATE, backing_fd, 0);

    const char *input_filename = argv[2];
    FILE *input_fp = fopen(input_filename, "r");

    // Fill page table entries with -1 for initially empty table.
    int i;
    for (i = 0; i < PAGES; i++) {
        pagetable[i] = -1;
    }
    // Fill tlb entries with -1 for initially empty tlb
    for (i = 0; i < TLB_SIZE; i++){
        tlb[i].logical = -1;
        tlb[i].physical = -1;
        tlb[i].reference = 0;
    }

    // Character buffer for reading lines of input file.
    char buffer[BUFFER_SIZE];

    // Data we need to keep track of to compute stats at end.
    int total_addresses = 0;
    int tlb_hits = 0;
    int page_faults = 0;
    int lru_on = 0;
    int lrused;

    // Number of the next unallocated physical page in main memory
    unsigned int free_page = 0;

    while (fgets(buffer, BUFFER_SIZE, input_fp) != NULL) {
        total_addresses++;
        int logical_address = atoi(buffer);

        /* TODO
        / Calculate the page offset and logical page number from logical_address */
        int offset = logical_address & OFFSET_MASK; // offset is given at the rightmost 10 bits
        int logical_page = (logical_address & PAGE_MASK) >> OFFSET_BITS; // page number is given at the rightmost 10th to 19th bits, shift to right by OFFSET_BITS to get the actual value
        ///////

        int physical_page = search_tlb(logical_page);
        // TLB hit
        if (physical_page != -1) {
            tlb_hits++;
            // TLB miss
        } else {
            physical_page = pagetable[logical_page];

            if (physical_page == -1) {
              if (free_page >= FRAMES){
                if (!using_lru){
                  while (1) { // Loop until we find a page to evict
                    // If this frame hasn't been referenced, evict it.
                    if (chance[next_frame] == 0) { // If the reference bit is 0, we can evict this page
                      // Remove this frame from the page table.
                      for (int i = 0; i < PAGES; i++) { // Iterate through the page table to find the page to evict
                        if (pagetable[i] == next_frame) { // If the page is found in the page table
                          pagetable[i] = -1; // Set the page to be evicted to -1
                          break;
                        }
                      }
                      // Also remove this frame from the TLB.
                      for (int i = 0; i < TLB_SIZE; i++) { // Iterate through the TLB to find the page to evict
                        if (tlb[i].physical == next_frame) { // If the page is found in the TLB
                          tlb[i].physical = -1; // Set the page to be evicted to -1
                          break; // We're done evicting from the TLB
                        }
                      }
                      break;
                    } else { // If the reference bit is 1, give it a second chance and move on to the next frame
                      chance[next_frame] = 0; // Set the reference bit to 0 for the frame that was given a second chance
                      next_frame = (next_frame + 1) % FRAMES; // Move to the next frame
                    }
                  }
                }
                else {
                  lru_on = 1;
                  int to_remove_logical;
                  int max_uses = -1;
                  lrused = -1;
                  for (int i = 0; i < FRAMES; i++){
                    if (lru[i] >= max_uses){
                      max_uses = lru[i];
                      lrused = i; // frame to be replaces
                    }
                  }
                  for (int i = 0; i < PAGES; i++){ // unavailable at pagetable
                    if ((lrused) == pagetable[i]){ // lrused shows the first address to be put into the memory
                      to_remove_logical = i; // logical page corresponding to lrused is to be removed
                      pagetable[i] = -1; // make it unavailable at the page table
                      break;
                    }
                  }
                  for (int i = 0; i < TLB_SIZE; i++){ // unavailable at tlb
                    if (to_remove_logical == tlb[i].logical){ // search for the logical page to be removed in the tlb
                      tlb[i].physical = -1; // set its physical value to -1 (unavailable)
                      break;
                    }
                  }
                }
              }
              // go to the desired page in the backing store and copy its contents to the main memory using my_page as a temporary variable
              char my_page[PAGE_SIZE];
              FILE *backing_file = fopen(backing_filename, "r");
              fseek(backing_file, logical_page * PAGE_SIZE, SEEK_SET);
              fread(my_page, PAGE_SIZE, sizeof(char), backing_file);
              fclose(backing_file);
              if (!lru_on){
                //strncpy(&main_memory[(free_page % FRAMES) * PAGE_SIZE], my_page, PAGE_SIZE); // commented out since it did not copy after a null character is encountered
                for (int i = 0; i < PAGE_SIZE; i++){
                  main_memory[(free_page % FRAMES) * PAGE_SIZE + i] = my_page[i];
                }
                // save the physical page number at the pagetable by circularly indexing the memory
                pagetable[logical_page] = free_page % FRAMES;
                // assign free_page to physical_page to be accessed later by circularly indexing the memory
                physical_page = free_page % FRAMES;
              }
              else {
                // put the new page in place of the least recently used one
                for (int i = 0; i < PAGE_SIZE; i++){
                  main_memory[(lrused) * PAGE_SIZE + i] = my_page[i];
                }
                // save the physical page number at the pagetable
                pagetable[logical_page] = lrused;
                // assign lrused to physical_page to be accessed later
                physical_page = lrused;
              }
              // increment free_page to replace the next location in the memory next time a page fault occurs
              free_page++;
              // a page fault occured, increment page fault counter
              page_faults++;
            }
            chance[physical_page] = 1; // Set the reference bit to 1 for the page that was just referenced
            add_to_tlb(logical_page, physical_page);
        }

        int physical_address = (physical_page << OFFSET_BITS) | offset;
        signed char value = main_memory[physical_page * PAGE_SIZE + offset];
        // increment the lru value of all addresses to "age" them
        for (int i = 0; i < FRAMES; i++){
            lru[i]++;
        }
        // set the lru value of the most recently used address
        lru[physical_page] = 0;

        printf("Virtual address: %d Physical address: %d Value: %d\n", logical_address, physical_address, value);
    }

    printf("Number of Translated Addresses = %d\n", total_addresses);
    printf("Page Faults = %d\n", page_faults);
    printf("Page Fault Rate = %.3f\n", page_faults / (1. * total_addresses));
    printf("TLB Hits = %d\n", tlb_hits);
    printf("TLB Hit Rate = %.3f\n", tlb_hits / (1. * total_addresses));

    return 0;
}