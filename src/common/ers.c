/*****************************************************************************\
 *  Copyright (c) Athena Dev Teams - Licensed under GNU GPL                  *
 *  For more information, see LICENCE in the main folder                     *
 *                                                                           *
 *  <H1>Entry Reusage System</H1>                                            *
 *                                                                           *
 *  There are several root entry managers, each with a different entry size. *
 *  Each manager will keep track of how many instances have been 'created'.  *
 *  They will only automatically destroy themselves after the last instance  *
 *  is destroyed.                                                            *
 *                                                                           *
 *  Entries can be allocated from the managers.                              *
 *  If it has reusable entries (freed entry), it uses one.                   *
 *  So no assumption should be made about the data of the entry.             *
 *  Entries should be freed in the manager they where allocated from.        *
 *  Failure to do so can lead to unexpected behaviours.                      *
 *                                                                           *
 *  <H2>Advantages:</H2>                                                     *
 *  - The same manager is used for entries of the same size.                 *
 *    So entries freed in one instance of the manager can be used by other   *
 *    instances of the manager.                                              *
 *  - Much less memory allocation/deallocation - program will be faster.     *
 *  - Avoids memory fragmentaion - program will run better for longer.       *
 *                                                                           *
 *  <H2>Disavantages:</H2>                                                   *
 *  - Unused entries are almost inevitable - memory being wasted.            *
 *  - A  manager will only auto-destroy when all of its instances are        *
 *    destroyed so memory will usually only be recovered near the end.       *
 *  - Always wastes space for entries smaller than a pointer.                *
 *                                                                           *
 *  WARNING: The system is not thread-safe at the moment.                    *
 *                                                                           *
 *  HISTORY:                                                                 *
 *    0.1 - Initial version                                                  *
 *                                                                           *
 * @version 0.1 - Initial version                                            *
 * @author Flavio @ Amazon Project                                           *
 * @encoding US-ASCII                                                        *
 * @see common#ers.h                                                         *
\*****************************************************************************/
#include <stdlib.h>

#include "ers.h"
#include "../common/malloc.h" // CREATE, RECREATE, aMalloc, aFree
#include "../common/showmsg.h" // ShowMessage, ShowError, ShowFatalError, CL_BOLD, CL_NORMAL

#ifndef DISABLE_ERS
/*****************************************************************************\
 *  (1) Private defines, structures and global variables.                    *
 *  ERS_BLOCK_ENTRIES - Number of entries in each block.                     *
 *  ERS_ROOT_SIZE     - Maximum number of root entry managers.               *
 *  ERLinkedList      - Structure of a linked list of reusable entries.      *
 *  ERSystem          - Class of an entry manager.                           *
 *  ers_root          - Array of root entry managers.                        *
 *  ers_num           - Number of root entry managers in the array.          *
\*****************************************************************************/

/**
 * Number of entries in each block.
 * @private
 * @see #ers_obj_alloc_entry(ERInterface eri)
 */
#define ERS_BLOCK_ENTRIES 4096

/**
 * Maximum number of root entry managers.
 * @private
 * @see #ers_root
 * @see #ers_num
 */
#define ERS_ROOT_SIZE 256

/**
 * Linked list of reusable entries.
 * The minimum size of the entries is the size of this structure.
 * @private
 * @see ERSystem#reuse
 */
typedef struct ers_ll {
	struct ers_ll *next;
} *ERLinkedList;

/**
 * Class of the object that manages entries of a certain size.
 * @param eri Public interface of the object
 * @param reuse Linked list of reusable data entries
 * @param blocks Array with blocks of entries
 * @param free Number of unused entries in the last block
 * @param num Number of blocks in the array
 * @param max Current maximum capacity of the array
 * @param destroy Destroy lock
 * @param size Size of the entries of the manager
 * @private
 */
typedef struct ers {

	/**
	 * Public interface of the entry manager.
	 * @param alloc Allocate an entry from this manager
	 * @param free Free an entry allocated from this manager
	 * @param entry_size Return the size of the entries of this manager
	 * @param destroy Destroy this instance of the manager
	 * @public
	 * @see #ERSystem
	 * @see common\ers.h#ERInterface
	 */
	struct eri eri;

	/**
	 * Linked list of reusable entries.
	 * @private
	 * @see #ERSystem
	 */
	ERLinkedList reuse;

	/**
	 * Array with blocks of entries.
	 * @private
	 * @see #ERSystem
	 */
	uint8 **blocks;

	/**
	 * Number of unused entries in the last block.
	 * @private
	 * @see #ERSystem
	 */
	uint32 free;

	/**
	 * Number of blocks in the array.
	 * @private
	 * @see #ERSystem
	 */
	uint32 num;

	/**
	 * Current maximum capacity of the array.
	 * @private
	 * @see #ERSystem
	 */
	uint32 max;

	/**
	 * Destroy lock.
	 * @private
	 * @see #ERSystem
	 */
	uint32 destroy;

	/**
	 * Size of the entries of the manager.
	 * @private
	 * @see #ERSystem
	 */
	uint32 size;

} *ERSystem;

/**
 * Root array with entry managers.
 * @private
 * @static
 * @see #ERS_ROOT_SIZE
 * @see #ers_num
 */
static ERSystem ers_root[ERS_ROOT_SIZE];

/**
 * Number of entry managers in the root array.
 * @private
 * @static
 * @see #ERS_ROOT_SIZE
 * @see #ers_root
 */
static uint32 ers_num = 0;

/*****************************************************************************\
 *  (2) Protected functions.                                                 *
 *  ers_obj_alloc_entry - Allocate an entry from the manager.                *
 *  ers_obj_free_entry  - Free an entry allocated from the manager.          *
 *  ers_obj_entry_size  - Return the size of the entries of the manager.     *
 *  ers_obj_destroy     - Destroy the instance of the manager.               *
\*****************************************************************************/

/**
 * Allocate an entry from this entry manager.
 * If there are reusable entries available, it reuses one instead.
 * @param self Interface of the entry manager
 * @return An entry
 * @protected
 * @see #ERS_BLOCK_ENTRIES
 * @see #ERLinkedList
 * @see #ERSystem
 * @see common\ers.h\ERInterface#alloc(ERInterface)
 */
static void *ers_obj_alloc_entry(ERInterface self)
{
	ERSystem obj = (ERSystem)self;
	void *ret;

	if (obj == NULL) {
		ShowError("ers_obj_alloc_entry: NULL object, aborting entry allocation.\n");
		return NULL;
	}

	if (obj->reuse) { // Reusable entry
		ret = obj->reuse;
		obj->reuse = obj->reuse->next;
	} else if (obj->free) { // Unused entry
		obj->free--;
		ret = &obj->blocks[obj->num -1][obj->free*obj->size];
	} else { // allocate a new block
		if (obj->num == obj->max) { // expand the block array
			if (obj->max == UINT32_MAX) { // No more space for blocks
				ShowFatalError("ers_obj_alloc_entry: maximum number of blocks reached, increase ERS_BLOCK_ENTRIES.\n"
						"exiting the program...\n");
				exit(EXIT_FAILURE);
			}
			obj->max = (obj->max<<2) +3; // = obj->max*4 +3; - overflow won't happen
			RECREATE(obj->blocks, uint8 *, obj->max);
		}
		CREATE(obj->blocks[obj->num], uint8, obj->size*ERS_BLOCK_ENTRIES);
		obj->free = ERS_BLOCK_ENTRIES -1;
		ret = &obj->blocks[obj->num][obj->free*obj->size];
		obj->num++;
	}
	return ret;
}

/**
 * Free an entry allocated from this manager.
 * WARNING: Does not check if the entry was allocated by this manager.
 * Freeing such an entry can lead to unexpected behaviour.
 * @param self Interface of the entry manager
 * @param entry Entry to be freed
 * @protected
 * @see #ERLinkedList
 * @see #ERSystem
 * @see ERSystem#reuse
 * @see common\ers.h\ERInterface#free(ERInterface,void *)
 */
static void ers_obj_free_entry(ERInterface self, void *entry)
{
	ERSystem obj = (ERSystem)self;
	ERLinkedList reuse;

	if (obj == NULL) {
		ShowError("ers_obj_free_entry: NULL object, aborting entry freeing.\n");
		return;
	} else if (entry == NULL) {
		ShowError("ers_obj_free_entry: NULL entry, nothing to free.\n");
		return;
	}

	reuse = (ERLinkedList)entry;
	reuse->next = obj->reuse;
	obj->reuse = reuse;
}

/**
 * Return the size of the entries allocated from this manager.
 * @param self Interface of the entry manager
 * @return Size of the entries of this manager in bytes
 * @protected
 * @see #ERSystem
 * @see ERSystem#size
 * @see common\ers.h\ERInterface#enty_size(ERInterface)
 */
static uint32 ers_obj_entry_size(ERInterface self)
{
	ERSystem obj = (ERSystem)self;

	if (obj == NULL) {
		ShowError("ers_obj_entry_size: NULL object, returning 0.\n");
		return 0;
	}

	return obj->size;
}

/**
 * Destroy this instance of the manager.
 * The manager is actually only destroyed when all the instances are destroyed.
 * When destroying the manager a warning is shown if the manager has 
 * missing/extra entries.
 * @param self Interface of the entry manager
 * @protected
 * @see #ERLinkedList
 * @see #ERSystem
 * @see common\ers.h\ERInterface#destroy(ERInterface)
 */
static void ers_obj_destroy(ERInterface self)
{
	ERSystem obj = (ERSystem)self;
	ERLinkedList reuse;
	uint32 i, count;

	if (obj == NULL) {
		ShowError("ers_obj_destroy: NULL object, aborting instance destruction.\n");
		return;
	}

	obj->destroy--;
	if (obj->destroy)
		return; // Not last instance

	// Remove manager from root array
	for (i = 0; i < ers_num; i++) {
		if (ers_root[i] == obj) {
			ers_num--;
			if (i < ers_num) // put the last manager in the free slot
				ers_root[i] = ers_root[ers_num];
			break;
		}
	}
	reuse = obj->reuse;
	count = 0;
	// Check for missing/extra entries
	for (i = 0; i < obj->num; i++) {
		if (i == 0) {
			count = ERS_BLOCK_ENTRIES -obj->free;
		} else if (count > UINT32_MAX -ERS_BLOCK_ENTRIES) {
			count = UINT32_MAX;
			break;
		} else {
			count += ERS_BLOCK_ENTRIES;
		}
		while (reuse && count) {
			count--;
			reuse = reuse->next;
		}
	}
	if (count) { // missing entries
		ShowWarning("ers_obj_destroy: %u entries missing, continuing destruction.\n"
				"Manager for entries of size %u.\n",
				count, obj->size);
	} else if (reuse) { // extra entries
		while (reuse && count != UINT32_MAX) {
			count++;
			reuse = reuse->next;
		}
		ShowWarning("ers_obj_destroy: %u extra entries found, continuing destruction.\n"
				"Manager for entries of size %u.\n",
				count, obj->size);
	}
	// destroy the entry manager
	if (obj->max) {
		for (i = 0; i < obj->num; i++)
			aFree(obj->blocks[i]); // release block of entries
		aFree(obj->blocks); // release array of blocks
	}
	aFree(obj); // release manager
}

/*****************************************************************************\
 *  (3) Public functions.                                                    *
 *  ers_new               - Get a new instance of an entry manager.          *
 *  ers_report            - Print a report about the current state.          *
 *  ers_force_destroy_all - Force the destruction of all the managers.       *
\*****************************************************************************/

/**
 * Get a new instance of the manager that handles the specified entry size.
 * Size has to greater than 0.
 * If the specified size is smaller than a pointer, the size of a pointer is 
 * used instead.
 * It's also aligned to ERS_ALIGNED bytes, so the smallest multiple of 
 * ERS_ALIGNED that is greater or equal to size is what's actually used.
 * @param The requested size of the entry in bytes
 * @return Interface of the object
 * @public
 * @see #ERSystem
 * @see #ers_root
 * @see #ers_num
 * @see common\ers.h#ERInterface
 * @see common\ers.h\ERInterface#destroy(ERInterface)
 * @see common\ers.h#ers_new_(uint32)
 */
ERInterface ers_new(uint32 size)
{
	ERSystem obj;
	uint32 i;

	if (size == 0) {
		ShowError("ers_new: invalid size %u, aborting instance creation.\n",
				size);
		return NULL;
	}

	if (size < sizeof(struct ers_ll)) // Minimum size
		size = sizeof(struct ers_ll);
	if (size%ERS_ALIGNED) // Align size
		size += ERS_ALIGNED -size%ERS_ALIGNED;

	for (i = 0; i < ers_num; i++) {
		obj = ers_root[i];
		if (obj->size == size) {
			// found a manager that handles the entry size
			obj->destroy++;
			return &obj->eri;
		}
	}
	// create a new manager to handle the entry size
	if (ers_num == ERS_ROOT_SIZE) {
		ShowFatalError("ers_alloc: too many root objects, increase ERS_ROOT_SIZE.\n"
				"exiting the program...\n");
		exit(EXIT_FAILURE);
	}
	obj = (ERSystem)aMalloc(sizeof(struct ers));
	// Public interface
	obj->eri.alloc      = ers_obj_alloc_entry;
	obj->eri.free       = ers_obj_free_entry;
	obj->eri.entry_size = ers_obj_entry_size;
	obj->eri.destroy    = ers_obj_destroy;
	// Block reusage system
	obj->reuse   = NULL;
	obj->blocks  = NULL;
	obj->free    = 0;
	obj->num     = 0;
	obj->max     = 0;
	obj->destroy = 1;
	// Properties
	obj->size = size;
	ers_root[ers_num++] = obj;
	return &obj->eri;
}

/**
 * Print a report about the current state of the Entry Reusage System.
 * Shows information about the global system and each entry manager.
 * The number of entries are checked and a warning is shown if extra reusable 
 * entries are found.
 * The extra entries are included in the count of reusable entries.
 * @public
 * @see #ERLinkedList
 * @see #ERSystem
 * @see #ers_root
 * @see #ers_num
 * @see common\ers.h#ers_report(void)
 */
void ers_report(void)
{
	uint32 i, j, used, reusable, extra;
	ERLinkedList reuse;
	ERSystem obj;

	// Root system report
	ShowMessage(CL_BOLD"Entry Reusage System report:\n"CL_NORMAL);
	ShowMessage("root array size     : %u\n", ERS_ROOT_SIZE);
	ShowMessage("root entry managers : %u\n", ers_num);
	ShowMessage("entries per block   : %u\n", ERS_BLOCK_ENTRIES);
	for (i = 0; i < ers_num; i++) {
		obj = ers_root[i];
		reuse = obj->reuse;
		used = 0;
		reusable = 0;
		// Count used and reusable entries
		for (j = 0; j < obj->num; j++) {
			if (j == 0) { // take into acount the free entries
				used = ERS_BLOCK_ENTRIES -obj->free;
			} else if (reuse) { // counting reusable entries
				used = ERS_BLOCK_ENTRIES;
			} else { // no more reusable entries, count remaining used entries
				for (; j < obj->num; j++) {
					if (used > UINT32_MAX -ERS_BLOCK_ENTRIES) { // overflow
						used = UINT32_MAX;
						break;
					}
					used += ERS_BLOCK_ENTRIES;
				}
				break;
			}
			while (used && reuse) { // count reusable entries
				used--;
				if (reusable != UINT32_MAX)
					reusable++;
				reuse = reuse->next;
			}
		}
		// Count extra reusable entries
		extra = 0;
		while (reuse && extra != UINT32_MAX) {
			extra++;
			reuse = reuse->next;
		}
		// Entry manager report
		ShowMessage(CL_BOLD"[Entry manager #%u report]\n"CL_NORMAL, i);
		ShowMessage("\tinstances          : %u\n", obj->destroy);
		ShowMessage("\tentry size         : %u\n", obj->size);
		ShowMessage("\tblock array size   : %u\n", obj->max);
		ShowMessage("\tallocated blocks   : %u\n", obj->num);
		ShowMessage("\tentries being used : %u\n", used);
		ShowMessage("\tunused entries     : %u\n", obj->free);
		ShowMessage("\treusable entries   : %u\n", reusable);
		if (extra)
			ShowMessage("\tWARNING - %u extra reusable entries were found.\n", extra);
	}
	ShowMessage("End of report\n");
}

/**
 * Forcibly destroy all the entry managers, checking for nothing.
 * The system is left as if no instances or entries had ever been allocated.
 * All previous entries and instances of the managers become invalid.
 * The use of this is NOT recommended.
 * It should only be used in extreme situations to make shure all the memory 
 * allocated by this system is released.
 * @public
 * @see #ERSystem
 * @see #ers_root
 * @see #ers_num
 * @see common\ers.h#ers_force_destroy_all(void)
 */
void ers_force_destroy_all(void)
{
	uint32 i, j;
	ERSystem obj;

	for (i = 0; i < ers_num; i++) {
		obj = ers_root[i];
		if (obj->max) {
			for (j = 0; j < obj->num; j++)
				aFree(obj->blocks[j]); // block of entries
			aFree(obj->blocks); // array of blocks
		}
		aFree(obj); // entry manager object
	}
	ers_num = 0;
}
#endif /* not DISABLE_ERS */

