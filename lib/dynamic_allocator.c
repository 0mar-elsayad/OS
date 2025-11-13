/*
 * dynamic_allocator.c
 *
 *  Created on: Sep 21, 2023
 *      Author: HP
 */
#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==================================
//==================================
// [1] GET PAGE VA:
//==================================
__inline__ uint32 to_page_va(struct PageInfoElement *ptrPageInfo)
{
	if (ptrPageInfo < &pageBlockInfoArr[0] || ptrPageInfo >= &pageBlockInfoArr[DYN_ALLOC_MAX_SIZE/PAGE_SIZE])
			panic("to_page_va called with invalid pageInfoPtr");
	//Get start VA of the 	page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================
// [2] GET PAGE INFO OF PAGE VA:
//==================================
__inline__ struct PageInfoElement * to_page_info(uint32 va)
{
	int idxInPageInfoArr = (va - dynAllocStart) >> PGSHIFT;
	if (idxInPageInfoArr < 0 || idxInPageInfoArr >= DYN_ALLOC_MAX_SIZE/PAGE_SIZE)
		panic("to_page_info called with invalid pa");
	return &pageBlockInfoArr[idxInPageInfoArr];
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #1 initialize_dynamic_allocator
	//Your code is here
	//Comment the following line
	//panic("initialize_dynamic_allocator() Not implemented yet");
	/*
	uint32 dynAllocStart = KERNEL_BASE;
	uint32 dynAllocEnd = dynAllocStart + DYN_ALLOC_MAX_SIZE ;

	daStart = dynAllocStart;
	daEnd = dynAllocEnd;
	*/

		 dynAllocStart = daStart ;
		 dynAllocEnd = daEnd ;

		 //round down?
		 uint32 num_pages = ((daEnd- daStart)/PAGE_SIZE);


		for(int i=0;i<num_pages;i++){
			pageBlockInfoArr[i].block_size= 0;
			pageBlockInfoArr[i].num_of_free_blocks= 0;
			pageBlockInfoArr[i].prev_next_info.le_next=NULL;
			pageBlockInfoArr[i].prev_next_info.le_prev=NULL;
		}

		LIST_INIT(&freePagesList);
		 for(int i=0; i<num_pages; i++)
		{
			 // not list insert head??
		LIST_INSERT_TAIL(&freePagesList, &pageBlockInfoArr[i]);
		}




		for(int i=0; i < (LOG2_MAX_SIZE - LOG2_MIN_SIZE + 1); i++ )
		{
			/* struct BlockElement_List x;
			x.___ptr_next=NULL;
			x.lh_first=NULL;
			x.lh_last=NULL;
			x.size=0;
			freeBlockLists[i] = x ;*/

			LIST_INIT(&freeBlockLists[i]);
		}




}

//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size
	//Your code is here
	//Comment the following line
	//panic("get_block_size() Not implemented yet");
	int array_size = DYN_ALLOC_MAX_SIZE/PAGE_SIZE;
	for(int i=0;i<array_size;i++)
	{

		if(pageBlockInfoArr[i].prev_next_info.le_next==va)
			return pageBlockInfoArr[i+1].block_size;
		else if (pageBlockInfoArr[i].prev_next_info.le_prev==va)
				return pageBlockInfoArr[i-1].block_size;
	}
	return 0;


}

//===========================
// 3) ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 size)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block
	//Your code is here
	//Comment the following line
	//panic("alloc_block() Not implemented yet");


    //setting size of block and free block lists array entry

	uint32 arr_index;
	if(size ==0)
		return NULL ;
	else if (size > DYN_ALLOC_MAX_BLOCK_SIZE){
		panic("size out of lower/upper limit");
		return NULL;
	}
	else{ // rounding size
		for ( int i=3; i<=11; i++)
			{
				uint32 power =1<<i;

				if (size <= power)
				{
					size = power;
					arr_index = i-3;
					break;
				}
			}
	}


	if(size<8 || size>(1 << 11))
		panic("Size Out of Stock!!!");


		struct BlockElement *found_block;
		struct PageInfoElement *found_page;
		struct PageInfoElement *block_info_entry;

//looping in freeBlockLists to find suitable block size

		// CASE 1
		if(LIST_SIZE(&freeBlockLists[arr_index])!=0)
		{
			found_block = LIST_FIRST(&freeBlockLists[arr_index]);
			LIST_REMOVE(&freeBlockLists[arr_index],found_block );
			/*
			for( int j=0; j< DYN_ALLOC_MAX_SIZE/PAGE_SIZE; j++ )
			{
				if(pageBlockInfoArr[j].block_size==size && pageBlockInfoArr[j].num_of_free_blocks > 0)
					pageBlockInfoArr[j].num_of_free_blocks--;
				else
					continue;
			}
			 */
			// Update the correct page info
			uint32 block_va = (uint32)found_block;
			uint32 page_start = ROUNDDOWN(block_va, PAGE_SIZE);
			block_info_entry = to_page_info(page_start);

			if(block_info_entry != NULL) {
				block_info_entry->num_of_free_blocks--;
			}

			return (void*)found_block;
		}
        // CASE 2 /*
		else if (LIST_SIZE(&freePagesList)!=0 )
		{
			/* has7ab page gededa fe page variable 3andy
			 * ashelha men el free pages list
			 * ashawer 3ala awel block feeha
			 */
			block_info_entry = LIST_FIRST(&freePagesList);
			LIST_REMOVE(&freePagesList,block_info_entry);
			// ALLOCATE PHYSICAL MEMORY FIRST!
			uint32 start = to_page_va(block_info_entry);
			if(get_page((void*)start) != 0) {
				//panic("page failed!");
				// If get_page fails, return the page to freePagesList
				LIST_INSERT_HEAD(&freePagesList, block_info_entry);
				return NULL;
			}
			block_info_entry->block_size= size;
			uint32 free_blocks = PAGE_SIZE / size;
			block_info_entry->num_of_free_blocks= free_blocks -1 ;
			//uint32 start = to_page_va(block_info_entry);
			for (int i=1; i< free_blocks ; i++ )
			{
				found_block= (struct BlockElement *) (start + i*size);
				LIST_INSERT_TAIL(&freeBlockLists[arr_index], found_block);
			}
			return (void*)start;
		}
		//CASE 3
		else
		{
			for(int i = arr_index + 1; i < (LOG2_MAX_SIZE - LOG2_MIN_SIZE + 1); i++){
				if(LIST_SIZE(&freeBlockLists[i]) != 0)
				{
					// Found a bigger block, allocate it as-is
					found_block = LIST_FIRST(&freeBlockLists[i]);
					LIST_REMOVE(&freeBlockLists[i], found_block);
					// Update page info
					uint32 block_va = (uint32)found_block;
					uint32 page_start = ROUNDDOWN(block_va, PAGE_SIZE);
					block_info_entry = to_page_info(page_start);
					if(block_info_entry != NULL) {
						block_info_entry->num_of_free_blocks--;
						}
					return (void*)found_block;
					}
				}
			//case 4
			panic("case 4");
			}
}



	//TODO: [PROJECT'25.BONUS#1] DYNAMIC ALLOCATOR - block if no free block


//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================

	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #4 free_block
	//Your code is here
	//Comment the following line
	panic("free_block() Not implemented yet");
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
	//Your code is here
	//Comment the following line
	panic("realloc_block() Not implemented yet");
}
