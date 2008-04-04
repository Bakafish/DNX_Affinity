/*--------------------------------------------------------------------------
 
   Copyright (c) 2006-2007, Intellectual Reserve, Inc. All rights reserved.
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as 
   published by the Free Software Foundation.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 
  --------------------------------------------------------------------------*/

/** Prototypes and type definitions for debug heap management for DNX.
 *
 * @file dnxHeap.h
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IFC
 */

#ifndef _DNXHEAP_H_
#define _DNXHEAP_H_

#include <stddef.h>

/** Allocate and track a new heap memory block.
 * 
 * malloc allows sz to be zero, in which case, it returns NULL. But there's 
 * really no reason to call malloc with a zero value unless the programmer
 * made a mistake, so this routine asserts that @p sz is non-zero.
 * 
 * @param[in] sz - the size in bytes of the block to be allocated.
 * @param[in] file - the file name from where this function was called.
 * @param[in] line - the line number from where this function was called.
 * 
 * @return A pointer to the new memory block, or NULL on allocation failure.
 */
void * dnxMalloc(size_t sz, char * file, int line);

/** Allocate, zero and track a new heap memory block.
 * 
 * Calloc is sort of a strange bird - it @em should be the same as @em malloc
 * with the addition of zeroing the block before returning, but K&R must have
 * decided that the X x Y block allocation strategry had some value for heap
 * blocks that need to be cleared...
 * 
 * @param[in] n - the number of blocks of @p sz bytes to be allocated.
 * @param[in] sz - the size of each of the @p n blocks to be allocated.
 * @param[in] file - the file name from where this function was called.
 * @param[in] line - the line number from where this function was called.
 * 
 * @return A pointer to the new memory block, or NULL on allocation failure.
 */
void * dnxCalloc(size_t n, size_t sz, char * file, int line);

/** Reallocate (and track) an existing heap memory block.
 * 
 * Realloc - the all-in-one heap management function. If @p p is NULL, 
 * realloc acts like malloc, if @p sz is zero, realloc acts like free.
 * 
 * Since there's no reason except programmer error that would have both @p p
 * and @p sz be zero at the same time, this routine asserts one or the other
 * is non-zero.
 * 
 * @param[in] p - a pointer to the block to be reallocated/resized.
 * @param[in] sz - the new size of the block.
 * @param[in] file - the file name from where this function was called.
 * @param[in] line - the line number from where this function was called.
 * 
 * @return A pointer to the new memory block, or NULL on allocation failure.
 */
void * dnxRealloc(void * p, size_t sz, char * file, int line);

/** Duplicate and track a string from the heap.
 * 
 * @param[in] str - a pointer to the zero-terminated string to be duplicated.
 * @param[in] file - the file name from where this function was called.
 * @param[in] line - the line number from where this function was called.
 * 
 * @return A pointer to the new memory block, or NULL on allocation failure.
 */
char * dnxStrdup(char * str, char * file, int line);

/** Free and previously allocated (and tracked) heap memory block.
 * 
 * Note that there are serveral good programming-practice reasons to call 
 * free with a @p p argument value of null. This debug routine allows it.
 * 
 * @param[in] p - a pointer to the debug heap block to be freed.
 */
void dnxFree(void * p);

/** Check the heap and display any unfreed blocks on the global list.
 * 
 * @return Zero on success, or a non-zero error code.
 */
int dnxCheckHeap(void);

#endif   /* _DNXHEAP_H_ */

