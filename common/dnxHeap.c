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

/** Implements debug heap management for DNX.
 *
 * @file dnxHeap.c
 * @author John Calcote (jcalcote@users.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "dnxHeap.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#define PICKETSZ  8        /*!< Picket fence size in bytes. */

#define PICKET1   0xAA     /*!< Overhead front picket fence fill value. */
#define PICKET2   0xBB     /*!< Overhead rear picket fence fill value. */
#define PICKET3   0xCC     /*!< User block footer picket fence fill value. */
#define ALIGNED   0xDD     /*!< Rear alignment buffer fill value. */
#define ALLOCED   0xEE     /*!< User space allocated fill value (malloc). */
#define FREED     0xFF     /*!< User space freed memory fill value. */

/** Align a block to a paragraph boundary. 
 * Add 15 and AND the result with 0x...FFFFFFF0 to clear the bottom 4 bits.
 * Works on any word-sized architecture (32, 64, etc). 
 */
#define ALIGNSZ(sz)  (((sz) + 15) & (-1L << 4))

/** Debug heap block bookkeeping header structure. */
typedef struct overhead_
{
   char picket1[PICKETSZ]; /*!< Overhead front picket fence. */
   struct overhead_ * next;/*!< Linked-list link to next alloc'd block. */
   size_t reqsz;           /*!< Requested size of heap block. */
   size_t actualsz;        /*!< Allocated size of debug heap block. */
   char * file;            /*!< File name wherein allocation occurred. */
   int line;               /*!< Line number whereat allocation occurred. */
   char picket2[PICKETSZ]; /*!< Overhead rear picket fence. */
} overhead;

/** The head of the global heap block list. */
static overhead * head = 0;

/** The global heap block list lock. */
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;

/** Link a newly allocated block into the block list. 
 * 
 * @param[in] ohp - a pointer to the block to be linked into the global list.
 */
static void link_block(overhead * ohp)
{
   pthread_mutex_lock(&list_lock);
   ohp->next = head;
   head = ohp;
   pthread_mutex_unlock(&list_lock);
}

/** Locate and remove a block by address from the global block list.
 * 
 * @param[in] ohp - a pointer to the block to be located and removed.
 * 
 * @return Boolean; True (1) if found, False (0) if not found in list.
 */
static int unlink_block(overhead * ohp)
{
   overhead ** cpp;
   pthread_mutex_lock(&list_lock);
   for (cpp = &head; *cpp; cpp = &(*cpp)->next)
      if (*cpp == ohp)
      {
         *cpp = ohp->next;
         break;
      }
   pthread_mutex_unlock(&list_lock);
   return *cpp? 1: 0;
}

/** Format a number of bytes as a hexadecimal ascii dump into a text buffer.
 * 
 * @param[out] buf - a pointer to the text output buffer.
 * @param[in] p - a pointer to the bytes to be formatted into @p buf.
 * @param[in] sz - the number of bytes of @p p to be formatted into @p buf.
 * 
 * @return The number of characters of @p buf that were used, minus the
 * trailing null-terminator; @p buf + this return value is a pointer to the 
 * trailing null-terminator just written.
 */
static size_t hex_dump(char * buf, void * p, size_t sz)
{
   unsigned char * ucp = (unsigned char *)p;
   char * cp = buf;

   while (sz--)
      cp += sprintf(cp, " %02x", *ucp++);

   *cp = 0;

   return cp - buf;
}

/** Dump a memory block with an optional debug(1) message.
 * 
 * @param[in] ohp - a pointer to the block to be displayed.
 * @param[in] msg - a pointer to the message to be displayed.
 */
static void dump_block(overhead * ohp, char * msg)
{
   char buf[512];
   char * p = buf;

   // format heap block
   p +=  sprintf(p, "dnxHeap: Block of size %d allocated at %s(%d)", 
         ohp->reqsz, ohp->file, ohp->line);
   p +=  sprintf(p, "\ndnxHeap: PICKET1: ");
   p += hex_dump(p, ohp->picket1, sizeof ohp->picket1);
   p +=  sprintf(p, "\ndnxHeap: PICKET2: ");
   p += hex_dump(p, ohp->picket2, sizeof ohp->picket2);
   p +=  sprintf(p, "\ndnxHeap: USERBLK: ");
   if (ohp->reqsz <= 32)
      p += hex_dump(p, &ohp[1], ohp->reqsz);
   else  
   {
      p += hex_dump(p, &ohp[1], 16);
      p += sprintf (p, "...");
      p += hex_dump(p, (char *)&ohp[1] + ohp->reqsz - 16, 16);
   }
   p +=  sprintf(p, "\ndnxHeap: PICKET3: ");
   p += hex_dump(p, (char *)&ohp[1] + ohp->reqsz, PICKETSZ);

   // now display optional message and heap block to debug log
   if (msg) dnxDebug(1, "dnxHeap: free(%p) - %s", &ohp[1], msg);
   dnxDebug(1, "%s", buf);
}

/** Scan a range of memory for the first non-recognized character.
 *
 * @param[in] p - a pointer to the buffer to be scanned.
 * @param[in] ch - the expected fill character.
 * @param[in] sz - the number of bytes to be scanned in @p p.
 * 
 * @return Zero on success, or -1 if not all bytes in the first @p sz bytes
 * of @p p are equal to @p ch.
 */
static int memscan(void * p, unsigned char ch, size_t sz)
{
   unsigned char * bp = (unsigned char *)p;
   unsigned char * ep = bp + sz;
   while (bp < ep) if (*bp != ch) return -1;
   return 0;
}

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
void * dnxMalloc(size_t sz, char * file, int line)
{
   overhead * ohp;
   size_t blksz = sizeof *ohp + sz + PICKETSZ;
   size_t alignsz = ALIGNSZ(blksz);

   assert(sz);
   assert(file && line);

   blksz += alignsz;
   ohp = malloc(blksz);
   assert(ohp);
   if (!ohp) return NULL;

   memset(ohp->picket1, PICKET1, sizeof ohp->picket1);
   ohp->next = 0;
   ohp->reqsz = sz;
   ohp->actualsz = blksz;
   ohp->file = strdup(file);
   ohp->line = line;
   memset(ohp->picket2, PICKET2, sizeof ohp->picket2);

   memset(&ohp[1], ALLOCED, sz);
   memset((char *)&ohp[1] + sz, PICKET3, PICKETSZ);
   memset((char *)&ohp[1] + sz + PICKETSZ, ALIGNED, alignsz);

   link_block(ohp);

   dnxDebug(10, "dnxHeap: alloc(%ld) == %p", sz, &ohp[1]);

   return &ohp[1];
}

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
void * dnxCalloc(size_t n, size_t sz, char * file, int line)
{
   size_t blksz = n * sz;
   void * p;

   assert(n && sz);
   assert(file && line);

   p = dnxMalloc(blksz, file, line);

   memset(p, 0, blksz);

   return p;
}

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
void * dnxRealloc(void * p, size_t sz, char * file, int line)
{
   assert(p || sz);
   assert(file && line);

   if (!sz)
      dnxFree(p), p = 0;
   else if (!p)
      p = dnxMalloc(sz, file, line);
   else
   {
      overhead * ohp = &((overhead *)p)[-1];
      void * np = dnxMalloc(sz, file, line);
      memcpy(np, p, sz < ohp->reqsz? sz: ohp->reqsz);
      dnxFree(p);
      p = np;
   }
   return p;
}

/** Duplicate and track a string from the heap.
 * 
 * @param[in] str - a pointer to the zero-terminated string to be duplicated.
 * @param[in] file - the file name from where this function was called.
 * @param[in] line - the line number from where this function was called.
 * 
 * @return A pointer to the new memory block, or NULL on allocation failure.
 */
char * dnxStrdup(char * str, char * file, int line)
{
   char * s;
   size_t allocsz;

   assert(str);
   assert(file && line);

   allocsz = strlen(str) + 1;

   if ((s = (char *)dnxMalloc(allocsz, file, line)) == 0)
      return 0;

   memcpy(s, str, allocsz);
   return s;
}

/** Free and previously allocated (and tracked) heap memory block.
 * 
 * Note that there are serveral good programming-practice reasons to call 
 * free with a @p p argument value of null. This debug routine allows it.
 * 
 * @param[in] p - a pointer to the debug heap block to be freed.
 */
void dnxFree(void * p)
{
   if (p)
   {
      int rc1, rc2, rc3;
      overhead * ohp = &((overhead *)p)[-1];

      if (!unlink_block(ohp))
      {
         dnxDebug(1, "dnxHeap: free(%p) - attempt to free non-heap address", p);
         assert(0);
         return;
      }
      if ((rc1 = memscan(ohp->picket1, PICKET1, sizeof ohp->picket1)) != 0)
         dump_block(ohp, "corrupt pre-header fence");
      if ((rc2 = memscan(ohp->picket2, PICKET2, sizeof ohp->picket2)) != 0)
         dump_block(ohp, "corrupt post-header fence");
      if ((rc3 = memscan(&ohp[1] + ohp->reqsz, PICKET3, PICKETSZ)) != 0)
         dump_block(ohp, "corrupt pre-user fence");

      assert(!rc1 && !rc2 && !rc3);

      free(ohp->file);
      free(ohp);
      dnxDebug(10, "dnxHeap: free(%p)", p);
   }
}

/** Check the heap and display any unfreed blocks on the global list.
 * 
 * @return Zero on success, or a non-zero error code.
 */
int dnxCheckHeap(void)
{
   overhead * cp;
   pthread_mutex_lock(&list_lock);
   for (cp = head; cp; cp = cp->next)
      dump_block(cp, "unfreed memory block");
   pthread_mutex_unlock(&list_lock);
   return head? -1: 0;
}

/*--------------------------------------------------------------------------*/

