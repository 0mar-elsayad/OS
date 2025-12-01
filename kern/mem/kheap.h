#ifndef FOS_KERN_KHEAP_H_
#define FOS_KERN_KHEAP_H_

#ifndef FOS_KERNEL
#error "This is a FOS kernel header; user programs should not #include it"
#endif

#include <inc/types.h>
#include <inc/queue.h>

//==================================================================================//
//============================== HEAP METADATA STRUCTURE ===========================//
//==================================================================================//

#define MAX_KHEAP_SIZE (KERNEL_HEAP_MAX - KERNEL_HEAP_START)
#define MAX_KHEAP_PAGES_COUNT (MAX_KHEAP_SIZE / PAGE_SIZE)

typedef struct kheapMetadata {
     uint32 va;                                   // Virtual Address (The Key)
    unsigned int size;
    bool is_used;// Total Block Size (The Value
    LIST_ENTRY(kheapMetadata) prev_next_info;
} kheapMetadata;
// List head declarations
LIST_HEAD(kmalloc_linkedlist, kheapMetadata);
LIST_HEAD(kMetadata_free_list, kheapMetadata);
//==================================================================================//
//============================== PLACEMENT STRATEGIES ==============================//
//==================================================================================//

// Values for heap placement strategy
#define KHP_PLACE_CONTALLOC  0x0
#define KHP_PLACE_FIRSTFIT   0x1
#define KHP_PLACE_BESTFIT    0x2
#define KHP_PLACE_NEXTFIT    0x3
#define KHP_PLACE_WORSTFIT   0x4
#define KHP_PLACE_CUSTOMFIT  0x5

//==================================================================================//
//============================== GLOBAL VARIABLES ==================================//
//==================================================================================//

//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 Page Alloc Limits [GIVEN]
 uint32 kheapPageAllocStart;
 uint32 kheapPageAllocBreak;
 uint32 kheapPlacementStrategy;

// Synchronization
extern struct kspinlock kheap_spinlock;

// Linked lists
extern struct kmalloc_linkedlist kheap_list;
extern struct kmalloc_linkedlist kheap_allocated_list;
extern struct kMetadata_free_list kheap_metadata_free_list;

// Statistics
 int numOfKheapVACalls;

//==================================================================================//
//============================== STRATEGY SETTERS/GETTERS ==========================//
//==================================================================================//

static inline void set_kheap_strategy(uint32 strategy)
{
    kheapPlacementStrategy = strategy;
}

static inline uint32 get_kheap_strategy(void)
{
    return kheapPlacementStrategy;
}

//==================================================================================//
//============================== PUBLIC API ========================================//
//==================================================================================//

void kheap_init(void);
void* kmalloc(unsigned int size);
void kfree(void* virtual_address);
void* krealloc(void* virtual_address, unsigned int new_size);
unsigned int kheap_virtual_address(unsigned int physical_address);
unsigned int kheap_physical_address(unsigned int virtual_address);

#endif // FOS_KERN_KHEAP_H_
