#include "kheap.h"

#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h>
#include <inc/types.h>
#include <inc/mmu.h>
#include <inc/queue.h>
#include "../conc/kspinlock.h"


#define MAX_KHEAP_PAGES  ((kheapPageAllocBreak - kheapPageAllocStart) / PAGE_SIZE)

//==================================================================================//
//============================== GLOBAL VARIABLES ==================================//
//==================================================================================//

// Define global variables (declared as extern in header)
struct kspinlock kheap_spinlock;
struct kmalloc_linkedlist kheap_list;
struct kmalloc_linkedlist kheap_allocated_list;
struct kMetadata_free_list kheap_metadata_free_list;

// Static arrays
//static kheapMetadata kheap_nodes[1<<20];
  // Frame number to VA mapping

static kheapMetadata kheap_nodes[MAX_KHEAP_PAGES_COUNT];

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

void initialize_kheap_list()
{
    static int initialized = 0;
    void initialize_kheap_list()
    {
        if (initialized) return;

        LIST_INIT(&kheap_list);
        LIST_INIT(&kheap_allocated_list);

        kheap_nodes[0].va = kheapPageAllocStart;
        kheap_nodes[0].size = KERNEL_HEAP_MAX - kheapPageAllocStart; // spicify the size to first block size
        kheap_nodes[0].is_used=1;
        for(int i=1;i<MAX_KHEAP_PAGES_COUNT;i++){
        	kheap_nodes[i].va=-1;
        	kheap_nodes[i].size=0;
        	kheap_nodes[i].is_used=0;
        }

        LIST_INSERT_HEAD(&kheap_list, &kheap_nodes[0]);

        initialized = 1;
    }
}

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
void kheap_init()
{
    //==================================================================================
    //DON'T CHANGE THESE LINES==========================================================
    //==================================================================================
    {
        initialize_dynamic_allocator(KERNEL_HEAP_START, KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE);
        set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
        kheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
        kheapPageAllocBreak = kheapPageAllocStart;
        init_kspinlock(&kheap_spinlock, "KernelHeapLock");
        initialize_kheap_list();
    }
    //==================================================================================
    //==================================================================================
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
    int ret = alloc_page(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE), PERM_WRITEABLE, 1);
    if (ret < 0)
        panic("get_page() in kern: failed to allocate page from the kernel");
    return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
    unmap_frame(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE));
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//


kheapMetadata* find_available_node(){
    uint32 max_index = MAX_KHEAP_PAGES_COUNT;
///
    for (uint32 i = 0; i < max_index; i++) {
        if (kheap_nodes[i].is_used == 0) { //use -1 instead of 0 gives page fault even tho its initialized by -1???
            return &kheap_nodes[i];///////////WARNING!!!! WHEN KFREEING OR KMALLOC WE DONT UPDATE THE KHEAP_NODES[]
        }
    }
    return NULL;
}



// insert sorted free region

void insert_sorted_free_region(kheapMetadata *free_node)
{
    kheapMetadata *iter = LIST_FIRST(&kheap_list);

    // If list is empty, just insert
    if (!iter) {
        LIST_INSERT_TAIL(&kheap_list, free_node);
        return;
    }

    // Find the position to insert (sorted by VA)
    while (iter && (uint32)iter->va < (uint32)free_node->va) {
        iter = LIST_NEXT(iter);
    }

    if (iter) {
        LIST_INSERT_BEFORE(&kheap_list, iter, free_node);
    } else {
        LIST_INSERT_TAIL(&kheap_list, free_node);
    }
}


//  merge free blocks

void merge_free_block(kheapMetadata *free_node)
{
    kheapMetadata *prev = LIST_PREV(free_node);
    kheapMetadata *next = LIST_NEXT(free_node);

    if (prev && ((uint32)prev->va + prev->size == (uint32)free_node->va)) {
        prev->size += free_node->size;
        LIST_REMOVE(&kheap_list, free_node);
        free_node->va=-1; //become free again
        free_node = prev;
    }

    next = LIST_NEXT(free_node);
    if (next && ((uint32)free_node->va + free_node->size == (uint32)next->va)) {
        free_node->size += next->size;
        LIST_REMOVE(&kheap_list, next);
        next->va=-1;
    }
}


//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
void* kmalloc(unsigned int size)
{
    acquire_kspinlock(&kheap_spinlock);
  // cprintf("kmalloc called size=%u\n", size);
    //cprintf("kheapPageAllocBreak = %x, KERNEL_HEAP_MAX = %x\n", kheapPageAllocBreak, KERNEL_HEAP_MAX);


    if (size == 0) {
        release_kspinlock(&kheap_spinlock);
        return NULL;
    }

    // Block Allocator
    if (size <= DYN_ALLOC_MAX_BLOCK_SIZE) {
        void* allocated_address = alloc_block(size);
        release_kspinlock(&kheap_spinlock);
        return allocated_address;
    }

    // Page Allocator
    else {
        uint32 size_to_allocate = ROUNDUP(size, PAGE_SIZE);
        kheapMetadata* allocated_metadata = NULL;
        kheapMetadata* best_worst_fit_metadata = NULL;

        // Search in kheap_list
        kheapMetadata* current_metadata = LIST_FIRST(&kheap_list);
        while (current_metadata != NULL) {
            // Exact Fit
            if (current_metadata->size == size_to_allocate) {
                best_worst_fit_metadata = current_metadata;
                break;
            }

            // Worst Fit
            if (current_metadata->size > size_to_allocate) {
                if (best_worst_fit_metadata == NULL ||
                    current_metadata->size > best_worst_fit_metadata->size) {
                    best_worst_fit_metadata = current_metadata;
                }
            }
            current_metadata = LIST_NEXT(current_metadata);
        }

        if (best_worst_fit_metadata != NULL) {
            kheapMetadata* block_to_allocate = best_worst_fit_metadata;
            uint32 allocated_va = block_to_allocate->va;  // Save this FIRST!
            unsigned int original_size = block_to_allocate->size;
            unsigned int remaining_size = original_size - size_to_allocate;

            LIST_REMOVE(&kheap_list, block_to_allocate);

            // Update the allocated block's metadata
            block_to_allocate->size = size_to_allocate;  //  ADD THIS!

            if (remaining_size > 0) {
                kheapMetadata *new_free = find_available_node();

                new_free->va = allocated_va + size_to_allocate;  // Use saved VA
                new_free->size = remaining_size;
                new_free->is_used = 1;
                insert_sorted_free_region(new_free);
                merge_free_block(new_free);
            }

            // Map frames using the SAVED VA and size_to_allocate
            for (uint32 va = allocated_va;
                 va < allocated_va + size_to_allocate;
                 va += PAGE_SIZE) {
                struct FrameInfo *frame;
                int ret = allocate_frame(&frame);
                if (ret != 0) {
                    panic("Failed to allocate frame\n");
                }
                if (frame == NULL) {
                    release_kspinlock(&kheap_spinlock);
                    return NULL;
                }
                uint32 permissions = PERM_PRESENT | PERM_WRITEABLE;
                map_frame(ptr_page_directory, frame, va, permissions);

             //   frame_to_va[to_frame_number(frame)] = va;
            }

            LIST_INSERT_TAIL(&kheap_allocated_list, block_to_allocate);
            allocated_metadata = block_to_allocate;
           // LIST_REMOVE(&kheap_list, block_to_allocate);//////tb2a ahker haga hena
        }

        // Break line extension
							if (allocated_metadata == NULL) {
								//cprintf("size to allocate********",size_to_allocate,"********");
								if (size_to_allocate > KERNEL_HEAP_MAX - kheapPageAllocBreak) {
								    release_kspinlock(&kheap_spinlock);
								    return NULL;
								}
								if (size_to_allocate <= KERNEL_HEAP_MAX - kheapPageAllocBreak) {
							     kheapMetadata* new_metadata = find_available_node();
                                 //cprintf("IMMM HERREEE AT THE ALLLOCCC BREAKKKK");
							 	  if (new_metadata != NULL) {
					               uint32 new_va_start = kheapPageAllocBreak;

												// 1. Configure the new allocated metadata node

								                new_metadata->va =new_va_start;
												new_metadata->size = size_to_allocate;
												new_metadata->is_used = 1;
												kheapPageAllocBreak += size_to_allocate; // Advance break


										for (uint32 va = new_va_start;
											 va < new_va_start + size_to_allocate;
											 va += PAGE_SIZE) {
											struct FrameInfo *frame;
											int ret = allocate_frame(&frame);
											if (ret != 0) {
												panic("Failed to allocate frame\n");
											}

											if (frame == NULL) {
												release_kspinlock(&kheap_spinlock);
												return NULL;
											}
											uint32 permissions = PERM_PRESENT | PERM_WRITEABLE;
											 map_frame(ptr_page_directory, frame, va, permissions);
											 frame_to_va[to_frame_number(frame)] = va;
										}

							LIST_INSERT_TAIL(&kheap_allocated_list, new_metadata);
					allocated_metadata = new_metadata;
								  }
									}

								}


      //if (allocated_metadata) cprintf("allocated va=%x size=%u\n", allocated_metadata->va, allocated_metadata->size);
     //  else cprintf("kmalloc returning NULL (no metadata)\n");
        release_kspinlock(&kheap_spinlock);

            return (void*)allocated_metadata->va;

    }
}


// Find VA size
unsigned int find_va_size(uint32 target_va)
{
    kheapMetadata *element;
    LIST_FOREACH_SAFE(element, &kheap_allocated_list, kheapMetadata) {
        if (element->va == target_va) {
            return element->size;
        }
    }
    return 0;
}

// [2] FREE SPACE FROM KERNEL HEAP:

void kfree(void* virtual_address)
{
	acquire_kspinlock(&kheap_spinlock);
    unsigned int va_size = find_va_size((uint32)virtual_address);

    if (va_size > DYN_ALLOC_MAX_BLOCK_SIZE) {
        // Page
    	kheapMetadata *free_node = NULL;
    	kheapMetadata *element;

    	//find node in alloc list
    	LIST_FOREACH_SAFE(element, &kheap_allocated_list, kheapMetadata) {
    	    if (element->va ==(uint32) virtual_address) {
    	        free_node = element;                // this is the node we will reuse
    	        LIST_REMOVE(&kheap_allocated_list, element);  // remove it
    	        break;
    	    }
    	}

    	if (!free_node) {
    	    panic("Trying to free a virtual address that was not allocated!");
    	}

    	// mark it as free
    	//free_node->is_used = 0;


        // Unmap all pages
        for (uint32 va = (uint32)virtual_address;
             va < (uint32)virtual_address + va_size;
             va += PAGE_SIZE) {
            unsigned int physical_address = kheap_physical_address(va);
            if (physical_address != 0) {
                struct FrameInfo *ptr_to_frame_info = to_frame_info(physical_address);
                free_frame(ptr_to_frame_info);
                frame_to_va[to_frame_number(ptr_to_frame_info)] = -1;
                unmap_frame(ptr_page_directory, va);
            }
        }

        insert_sorted_free_region(free_node);
        merge_free_block(free_node);
        while (1) {
                   kheapMetadata *last_free = NULL;

                   // Find the free block that ends exactly at kheapPageAllocBreak
                   LIST_FOREACH(element, &kheap_list) {
                       if (element->va + element->size == kheapPageAllocBreak) {
                           last_free = element;
                           break;
                       }
                   }

                   if (last_free) {
                       // Move break down
                       kheapPageAllocBreak = last_free->va;

                       // Remove this free block from the list and return the node
                       LIST_REMOVE(&kheap_list, last_free);
                       last_free->va = -1;  // Mark as available
                       last_free->size = 0;
                       last_free->is_used=0;
                   } else {
                       // No more free space at the end
                       break;
                   }
               }

    } else {
        // Block
        free_block(virtual_address);
    }

  /*  // Remove from allocated list
    kheapMetadata *element;
    LIST_FOREACH_SAFE(element, &kheap_allocated_list, kheapMetadata) {
        if (element->va == (uint32)virtual_address) {
            LIST_REMOVE(&kheap_allocated_list, element);
            break;
        }
    }*/
    release_kspinlock(&kheap_spinlock);
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================

unsigned int kheap_virtual_address(unsigned int physical_address)
{
    // Extract the page offset
    uint32 offset = physical_address & 0xFFF;

    // Get the frame base (remove offset to get page-aligned address)
    uint32 frame_base = physical_address & 0xFFFFF000;

    // Get the frame info for this physical address
    struct FrameInfo *ptr_to_frame_info = to_frame_info(frame_base);

    if (ptr_to_frame_info == NULL) {
        return 0;
    }

    uint32 frame_number = to_frame_number(ptr_to_frame_info);

    // Get the virtual address for this frame
    uint32 VA = frame_to_va[frame_number];

    // Check if frame is not mapped in kernel heap (freed or never mapped)
    if (VA == 0 || VA == (uint32)-1) {
        return 0;
    }

    // Return the full virtual address with offset
    return VA + offset;
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
    uint32 PD_index = PDX(virtual_address);
    uint32 PDE = ptr_page_directory[PD_index];

    if ((PDE & 1) == 0)
        return 0;

    uint32 page_table_physical_address = EXTRACT_ADDRESS(PDE);
    uint32 *page_table;

    if (USE_KHEAP && !CHECK_IF_KERNEL_ADDRESS(virtual_address))
        page_table = (uint32 *)kheap_virtual_address(page_table_physical_address);
    else
        page_table = (uint32 *)STATIC_KERNEL_VIRTUAL_ADDRESS(page_table_physical_address);

    uint32 table_index = PTX(virtual_address);
    uint32 PTE = page_table[table_index];

    if ((PTE & 1) == 0)
        return 0;

    uint32 physical_address = EXTRACT_ADDRESS(PTE);
    physical_address = physical_address + PGOFF(virtual_address);

    return physical_address;
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//

//void *krealloc(void *virtual_address, uint32 new_size)
//{
//    // handle trivial cases: NULL and zero-size
//    if (virtual_address == NULL) {
//        return kmalloc(new_size);
//    }
//    if (new_size == 0) {
//        kfree(virtual_address);
//        return NULL;
//    }
//
//    uint32 va = (uint32)virtual_address;
//    uint32 old_size = (uint32) find_va_size(va);
//
//    if (old_size == new_size) return virtual_address;
//
//    // page to blocj or block to page
//    bool old_is_block = (old_size <= DYN_ALLOC_MAX_BLOCK_SIZE);
//    bool new_is_block = (new_size <= DYN_ALLOC_MAX_BLOCK_SIZE);
//
//    if (old_is_block != new_is_block) {
//          kfree(virtual_address);
//        void *n = kmalloc(new_size);
//        if (!n) return NULL;
//        return n;
//    }
//
//    //  both are block
//    if (old_is_block && new_is_block) {
//        // shrinking or expanding within block subsystem:
//        // Simpler approach: do not move memory.
//        // If shrinking: just update metadata & map
//        // If expanding: there is no simple safe in-place expand unless you check/merge adjacent free blocks.
//        // We'll try to check for a free block immediately after this block within the same page only
//        if (new_size < old_size) {
//            // shrink: update size mapping and metadata
//            // update metadata entry in allocated list if exists
//            kheapMetadata *m = NULL;
//            LIST_FOREACH_SAFE(m, &kheap_allocated_list, kheapMetadata) {
//                if (m->va == va) {
//                    m->size = new_size;
//                    break;
//                }
//            }
//            //we should unmap here or something i dont know or call free_block or kfree!!!!!!!1
//            return virtual_address;
//        } else { // enlargement
//            // Attempting in-place expansion inside the same page is complex.
//            // Fallback: allocate new region, free old (no memory move).
//            void *n = kmalloc(new_size);
//            //we should map here also but allocate block or just kmalloc
//            if (!n) return NULL;
//            kfree(virtual_address);
//            return n;
//        }
//    }
//
//    //  both are page
//    if (!old_is_block && !new_is_block) {
//        // both are page-backed
//        uint32 old_pages = ROUNDUP(old_size, PAGE_SIZE) / PAGE_SIZE;
//        uint32 new_pages = ROUNDUP(new_size, PAGE_SIZE) / PAGE_SIZE;
//
//        //same num of pages
//        if (new_pages == old_pages) {
//            // update mapping and metadata
//            kheapMetadata *meta = NULL;
//            LIST_FOREACH_SAFE(meta, &kheap_allocated_list, kheapMetadata) {
//                if (meta->va == va) {
//                    meta->size = new_size;
//                    break;
//                }
//            }
//            return virtual_address;
//        }
//
//        // SHRINKING
//        if (new_pages < old_pages) {
//            uint32 pages_to_free = old_pages - new_pages;
//            uint32 free_start_va = va + new_pages * PAGE_SIZE; // first page to free
//
//             //unmap frames
//            for (uint32 page_va = free_start_va; page_va < va + old_pages * PAGE_SIZE; page_va += PAGE_SIZE) {
//                unsigned int physical_address = kheap_physical_address(page_va);
//                if (physical_address != 0) {
//                    struct FrameInfo *ptr_to_frame_info = to_frame_info(physical_address);
//                    free_frame(ptr_to_frame_info);
//                    frame_to_va[to_frame_number(ptr_to_frame_info)] = -1;
//                    unmap_frame(ptr_page_directory, va);
//            }
//            }
//
//            // update size in allocated lisr
//            kheapMetadata *meta = NULL;
//            LIST_FOREACH_SAFE(meta, &kheap_allocated_list, kheapMetadata) {
//                if (meta->va == va) {
//                    meta->size = new_size;
//                    break;
//                }
//            }
//            //insert in kheap_free list
//                        kheapMetadata *free_node =  find_available_node();
//                        free_node->va=free_start_va;
//                        free_node->size=old_size-new_size;
//                        insert_sorted_free_region(free_node);
//                        merge_free_block(free_node);
//            // Optionally: create a kheapMetadata node for the freed pages and insert into kheap_list
//            // (left out because your kheap free-page management functions were not pasted).
//            return virtual_address;
//        }
//
//        // STRETCHING (new_pages > old_pages)
//        if (new_pages > old_pages) {
//            uint32 need_pages = new_pages - old_pages;
//            uint32 va_to_find = va + old_pages * PAGE_SIZE; // contiguous region right after current allocation
//
//            // Search kheap_list for a free metadata node starting exactly at candidate_va
//            kheapMetadata *element = NULL, *tmp = NULL;
//            LIST_FOREACH_SAFE(element, &kheap_list,kheapMetadata) {
//                if (element->va == va_to_find) {
//                    // element is free page chunk directly after allocation
//                	//check the node's size
//                    if (element->size >= need_pages * PAGE_SIZE) {
//                        // We can try to allocate required frames/pages and map them
//                        uint32 pages_allocated = 0;
//                        struct FrameInfo *frame = NULL;
//                        bool fail =0;
//
//                        for (uint32 page_va = va_to_find; page_va < va_to_find + need_pages * PAGE_SIZE; page_va += PAGE_SIZE) {
//                            int ret = allocate_frame(&frame);
//                            if (ret != 0 || frame == NULL) {
//                                fail = 1;
//                                panic("Failed to allocate frame\n");
//                                break;
//                            }
//                            // map frame into page_va
//                            uint32 permissions = PERM_PRESENT | PERM_WRITEABLE;
//                            map_frame(ptr_page_directory, frame, page_va, permissions);
//                            // ensure reverse map if map_frame didn't do it
//                            frame_to_va[to_frame_number(frame)] = page_va & 0xFFFFF000;
//                            pages_allocated++;
//                        }
////unsure if i have to do it or not !!!!!!! rollback or just panic!!!!!!
//                        if (fail) {
//                            // rollback allocated pages this attempt created
//                            // free frames of allocated pages and unmap them
//                            uint32 rollback_va = va_to_find;
//                            for (uint32 i = 0; i < pages_allocated; ++i, rollback_va += PAGE_SIZE) {
//                                // unmap and free frame
//                               // unmap_frame(rollback_va);
//                            }
//                            // keep original allocation untouched
//                            return virtual_address;
//                        }
//
//                        // Success: we've mapped needed pages, now update metadata
//                        // Reduce the free element's size and/or remove it
//                        element->va += need_pages * PAGE_SIZE;
//                        element->size -= need_pages * PAGE_SIZE;
//                        if (element->size == 0) {
//                            LIST_REMOVE(&kheap_list, element);
//                        }
//
//                        // Update allocated metadata for this VA
//                        kheapMetadata *alloc_meta = NULL;
//                        LIST_FOREACH_SAFE(alloc_meta, &kheap_allocated_list, kheapMetadata) {
//                            if (alloc_meta->va == va) {
//                                alloc_meta->size = new_pages * PAGE_SIZE; // page-aligned size stored
//                                break;//<<<<<<<<<<---------------------------see this again-----------------------------------------
//                            }
//                        }
//                        // update size map
//
//                        return virtual_address;
//                    }
//                    // not enough contiguous pages in this free element: break (can't grow in-place here)
//                    break;
//                }
//            }
//            //allocate new b2a cuz there is no contigous va
//
//            // if we reached here: in-place extension not possible: fallback allocate new and free old
//            void *n = kmalloc(new_size);
//            if (!n) return virtual_address; // preserve old on failure
//            kfree(virtual_address);
//            return n;
//        }
//    }
//
//    // fallback: return original if nothing matched
//    return virtual_address;
//}

/*

void *krealloc(void *virtual_address, uint32 new_size)
{
    // TODO: Implement krealloc
	if(virtual_address==NULL){
		virtual_address=kmalloc(new_size);
		return virtual_address;
	}
	if(new_size==0){
		kfree(virtual_address);
		return NULL;
	}
	int old_size=find_va_size[(uint32)virtual_address];
	if(new_size==old_size)return virtual_address;

		if((old_size<=DYN_ALLOC_MAX_SIZE&&new_size>DYN_ALLOC_MAX_SIZE)||(old_size>DYN_ALLOC_MAX_SIZE&&new_size<DYN_ALLOC_MAX_SIZE)){
		//if block-->page
	    //if page-->block
			kfree(virtual_address);
			return kmalloc(new_size);
		}
		//stretching
			if(new_size>old_size){
			if(new_size>DYN_ALLOC_MAX_SIZE){
				//PAGE
				kheapMetadata *element;
                kheapMetadata *free_node=NULL;
				//see if it can be strectched list
				LIST_FOREACH_SAFE(element, &kheap_list, kheapMetadata) {
				 if (element->va ==(uint32) virtual_address+old_size) {
				   if(element->size>=new_size-old_size){//so its valid
					   uint32 new_va=(uint32)virtual_address+old_size;
					   uint32 size_to_allocate= ROUNDUP(new_size-old_size, PAGE_SIZE);
					   for (uint32 va =new_va;
					 va < (uint32) new_va+size_to_allocate;
					 va += PAGE_SIZE) {
					struct FrameInfo *frame;
					int ret = allocate_frame(&frame);
				if (ret != 0) {
				panic("Failed to allocate frame\n");
				}

			if (frame == NULL) {
		release_kspinlock(&kheap_spinlock);
			return NULL;
				}
			uint32 permissions = PERM_PRESENT | PERM_WRITEABLE;
			map_frame(ptr_page_directory, frame, va, permissions);
			frame_to_va[to_frame_number(frame)] = va;
		    LIST_REMOVE(&kheap_list, element);// remove it
		    LIST_FOREACH_SAFE(element, &kheap_allocated_list, kheapMetadata) {
				   }
		           if(element->va==(uint32)virtual_address){
		        	   element->size=element->size+size_to_allocate;
		        	   break;
		           }
					   }
				   }
				  break;
				 }
				 return virtual_address;
				 //it must be put somewhere else
				 else{
				kfree(virtual_address);
			 return kmalloc(size_to_allocate+old_size);
			}
				}
			}
			else{
				//BLOCK
				//later
			}

	}

    return NULL;
    //panic("krealloc() is not implemented yet...!!");
}
*/
