/**
 * virtmem.c 
 */

#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>



#define TLB_SIZE 16
#define PAGES 1024
#define PAGE_MASK 1023
#define FRAMES 256 /* Page frame number for physical memory */

#define PAGE_SIZE 1024
#define OFFSET_BITS 10
#define OFFSET_MASK 1023


/*re constructed*/
#define MEMORY_SIZE FRAMES * PAGE_SIZE

//total adresses numbers are stored to find the least used element,
int pagetable_addresses[1024];

// Max number of characters per line of input file to read.
#define BUFFER_SIZE 10

int tlbindex = 0;
int checker=0;

struct tlbentry {
  unsigned char logical;
  unsigned char physical;
};


// TLB is kept track of as a circular array, with the oldest element being overwritten once the TLB is full.
struct tlbentry tlb[TLB_SIZE];
// number of inserts into TLB that have been completed. Use as tlbindex % TLB_SIZE for the index of the next TLB line to use.

// pagetable[logical_page] is the physical page number for logical page. Value is -1 if that logical page isn't yet in the table.
int pagetable[PAGES];

signed char main_memory[MEMORY_SIZE];

// Pointer to memory mapped backing file
signed char *backing;


//keeps current time
int current_time;


//Number of pages in the "pagetable" array
int num_pages;
int pageRefTbl;


int last_access_time[256];
int count_pagetable[256];

//finds the corresponding 
void fifo_policy( unsigned char free_page){

for(int i=0;i< PAGES;i++){
if(pagetable[i] ==free_page)
{
pagetable[i] = -1;
break;
}
}
}


int lru_element(int logical_page , int pageFault){
// As page replacement happens when there is not available frame first we compare
// page fault numbers with frame number , if it is smaller or equal decrement 
// page fault as we can enter a new data without page replacement.

  if (pageFault <= FRAMES) {
    pagetable[logical_page] = pageFault - 1;
    return pageFault - 1;
  }

  int i;
  int minElement = count_pagetable[0];
  unsigned char minIndex = 0;

  static int curr_time =0;
  curr_time++;

  last_access_time[logical_page] = curr_time;
  
  // Scan through the page table and find the page 
  // with the oldest last access time

  for (i = 0; i < 256; i++) {
    if (pagetable[i] != -1) {
    if (last_access_time[i] < minElement) {
        minIndex = i;
        minElement = last_access_time[i];
      }
    }
  }

 //Since we have found the least used element in physical pages, 
 //we have to empty it for that we have iterate over the virtual pages.
 for (int i = 0; i < 1024; i++) {
		if(pagetable[i]== minIndex){
			pagetable[i]=-1;
			break;
		}
	}


  // Return the physical page number of the page that was selected for replacement
  return minIndex;

}


int max(int a, int b)
{
  if (a > b)
    return a;
  return b;
}

/* Returns the physical address from TLB or -1 if not present.*/
int search_tlb(unsigned char logical_page) {
    /* TODO */
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].logical == logical_page) {
            return tlb[i].physical;
        }
    }
    return -1;


}

/* Adds the specified mapping to the TLB, replacing the oldest mapping FIFO replacement). */
void add_to_tlb(unsigned char logical, unsigned char physical) {
    /* TODO */
    tlb[tlbindex % TLB_SIZE].logical = logical;
    tlb[tlbindex % TLB_SIZE].physical = physical;
    tlbindex++;
}


int policy_algo = 0;

int main(int argc, const char *argv[])
{
  if (argc != 5) {
    fprintf(stderr, "Usage ./virtmem backingstore input\n");
    exit(1);
  }

  policy_algo = atoi(argv[4]);
  
  const char *backing_filename = argv[1]; 
  int backing_fd = open(backing_filename, O_RDONLY);
  backing = mmap(0, MEMORY_SIZE, PROT_READ, MAP_PRIVATE, backing_fd, 0); 
  
  const char *input_filename = argv[2];
  FILE *input_fp = fopen(input_filename, "r");
  

  printf("choice:%d\n",policy_algo );


  // Fill page table entries with -1 for initially empty table.
  int i;
  for (i = 0; i < PAGES; i++) {
    pagetable[i] = -1;
  }
  
  // Character buffer for reading lines of input file.
  char buffer[BUFFER_SIZE];
  
  // Data we need to keep track of to compute stats at end.
  int total_addresses = 0;
  int tlb_hits = 0;
  int page_faults = 0;

  
  // Number of the next unallocated physical page in main memory
  unsigned char free_page = 0;
 
  
  while (fgets(buffer, BUFFER_SIZE, input_fp) != NULL) {
    total_addresses++;
    int logical_address = atoi(buffer);

    /* TODO 
    / Calculate the page offset and logical page number from 
logical_address */
    int offset = logical_address & OFFSET_MASK;
    int logical_page =(logical_address >> OFFSET_BITS) & PAGE_MASK;
    ///////    

    int physical_page = search_tlb(logical_page);
    // TLB hit
    if (physical_page != -1) {
      tlb_hits++;
      // TLB miss
    } else {
      physical_page = pagetable[logical_page];
      
      // Page fault
      if (physical_page == -1) {
      /* TODO */

      page_faults++;
      if(policy_algo == 1){
      physical_page = lru_element(logical_page, page_faults);      

      } else if (policy_algo == 2){
      fifo_policy(&free_page);
    
      }

      memcpy(main_memory + free_page*PAGE_SIZE, backing + logical_page * PAGE_SIZE, PAGE_SIZE);
      physical_page = free_page;
      pagetable[logical_page] = physical_page;

      }

      add_to_tlb(logical_page, physical_page);
    }

    //Update array for count_pagetable which is aimed to obtain the last
    //access time for that page which corresponds physical_page.Later the
    //page with smallest access time will be chosen as victim.

    count_pagetable[physical_page] = total_addresses;

    // calculating the physical address
    int physical_address = (physical_page << OFFSET_BITS) | offset;
    signed char value = main_memory[(physical_page * PAGE_SIZE)+ offset];
    
  
    
    if(value < 0){
    checker++;
    }  
    
   
    printf("Virtual address: %d Physical address: %d Value: %d\n", 
logical_address, physical_address, value);
  }
  
  printf("Number of Translated Addresses = %d\n", total_addresses);
  printf("Page Faults = %d\n", page_faults);
  printf("Checker = %d\n", checker);
  printf("Page Fault Rate = %.3f\n", page_faults / (1. * 
total_addresses));
  printf("TLB Hits = %d\n", tlb_hits);
  printf("TLB Hit Rate = %.3f\n", tlb_hits / (1. * total_addresses));
  
  
  return 0;
}



