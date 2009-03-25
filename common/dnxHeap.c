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

#define PICKETSZ  8        //!< Picket fence size in bytes.

#define PICKET1   0xAA     //!< Overhead front picket fence fill value.
#define PICKET2   0xBB     //!< Overhead rear picket fence fill value.
#define PICKET3   0xCC     //!< User block footer picket fence fill value.
#define ALIGNED   0xDD     //!< Rear alignment buffer fill value.
#define ALLOCED   0xEE     //!< User space allocated fill value (malloc).
#define FREED     0xFF     //!< User space freed memory fill value.

/** Align a block to a paragraph boundary. 
 * Add 15 and AND the result with 0x...FFFFFFF0 to clear the bottom 4 bits.
 * Works on any word-sized architecture (32, 64, etc). 
 */
#define ALIGNSZ(sz)  (((sz) + 15) & (-1L << 4))

/** Debug heap block bookkeeping header structure. */
typedef struct overhead_
{
   char picket1[PICKETSZ]; //!< Overhead front picket fence.
   struct overhead_ * next;//!< Linked-list link to next alloc'd block.
   size_t reqsz;           //!< Requested size of heap block.
   size_t actualsz;        //!< Allocated size of debug heap block.
   char * file;            //!< File name wherein allocation occurred.
   int line;               //!< Line number whereat allocation occurred.
   char picket2[PICKETSZ]; //!< Overhead rear picket fence.
} overhead;

static overhead * head = 0;//!< The head of the global heap block list.
static int blkcnt = 0;     //!< The number of blocks in the global list.
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
                           //!< The global heap block list lock.

/*--------------------------------------------------------------------------
                              IMPLEMENTATION
  --------------------------------------------------------------------------*/

/** Link a newly allocated block into the block list. 
 * 
 * @param[in] ohp - a pointer to the block to be linked into the global list.
 */
static void link_block(overhead * ohp)
{
   pthread_mutex_lock(&list_lock);
   ohp->next = head;
   head = ohp;
   blkcnt++;
   pthread_mutex_unlock(&list_lock);
}

//----------------------------------------------------------------------------
 
/** Locate and remove a block by address from the global block list.
 * 
 * @param[in] ohp - a pointer to the block to be located and removed.
 * 
 * @return Boolean; True (1) if found, False (0) if not found in list.
 */
static int unlink_block(overhead * ohp)
{
   int found = 0;
   overhead ** cpp;
   pthread_mutex_lock(&list_lock);
   for (cpp = &head; *cpp; cpp = &(*cpp)->next)
      if (*cpp == ohp)
      {
         *cpp = ohp->next;
         blkcnt--;
         found = 1;
         break;
      }
   pthread_mutex_unlock(&list_lock);
   return found;
}

//----------------------------------------------------------------------------
 
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

//----------------------------------------------------------------------------
 
/** Dump a memory block with an optional debug(1) message.
 * 
 * @param[in] ohp - a pointer to the block to be displayed.
 * @param[in] msg - a pointer to the message to be displayed.
 */
static void dump_block(overhead * ohp, char * msg)
{
   char buf[512];
   char * p = buf;

   // format heap block...
   p +=  sprintf(p, "dnxHeap:");
   if (msg) p += sprintf(p, " %s -", msg);
   p+= sprintf(p, " %d bytes allocated at %s(%d): ", 
         ohp->reqsz, ohp->file, ohp->line);
   p += hex_dump(p, ohp->picket1, sizeof ohp->picket1);
   p +=  sprintf(p, " |");
   p += hex_dump(p, ohp->picket2, sizeof ohp->picket2);
   p +=  sprintf(p, " |");
   if (ohp->reqsz <= 16)
      p += hex_dump(p, &ohp[1], ohp->reqsz);
   else  // for blocks > 16 bytes, display only first and last 8 bytes
   {
      p += hex_dump(p, &ohp[1], 8);
      p += sprintf (p, " ...");
      p += hex_dump(p, (char *)&ohp[1] + ohp->reqsz - 8, 8);
   }
   p +=  sprintf(p, " |");
   p += hex_dump(p, (char *)&ohp[1] + ohp->reqsz, PICKETSZ);

   // ...and display it
   dnxDebug(1, "%s", buf);
}

//----------------------------------------------------------------------------
 
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
   while (bp < ep) if (*bp++ != ch) return -1;
   return 0;
}

//----------------------------------------------------------------------------
 
/** Check the integrity of a memory block.
 * 
 * @param[in] ohp - a pointer to the block to be checked.
 * 
 * @return Boolean: true (1) if block is okay, false (0) if not.
 */
static int check_block(overhead * ohp)
{
   int rc1, rc2, rc3;

   if ((rc1 = memscan(ohp->picket1, PICKET1, sizeof ohp->picket1)) != 0)
      dump_block(ohp, "corrupt pre-header fence");
   if ((rc2 = memscan(ohp->picket2, PICKET2, sizeof ohp->picket2)) != 0)
      dump_block(ohp, "corrupt post-header fence");
   if ((rc3 = memscan((char *)&ohp[1] + ohp->reqsz, PICKET3, PICKETSZ)) != 0)
      dump_block(ohp, "corrupt post-user fence");

   assert(!rc1 && !rc2 && !rc3);

   return !rc1 && !rc2 && !rc3 ? 1 : 0;
}

/*--------------------------------------------------------------------------
                                 INTERFACE
  --------------------------------------------------------------------------*/

void * dnxMalloc(size_t sz, char * file, int line)
{
   overhead * ohp;
   size_t blksz = ALIGNSZ(sizeof *ohp + sz + PICKETSZ);

   assert(sz);
   assert(file && line);

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
   memset((char *)&ohp[1] + sz + PICKETSZ, ALIGNED, 
         blksz - (sizeof *ohp + sz + PICKETSZ));

   link_block(ohp);

   dnxDebug(10, "dnxHeap: alloc(%ld) == %p.", sz, &ohp[1]);

   return &ohp[1];
}

//----------------------------------------------------------------------------
 
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

//----------------------------------------------------------------------------
 
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

//----------------------------------------------------------------------------
 
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

//----------------------------------------------------------------------------
 
void dnxFree(void * p)
{
   if (p)
   {
      overhead * ohp = &((overhead *)p)[-1];
      if (!unlink_block(ohp))
      {
         dnxDebug(1, "dnxHeap: free(%p) - attempt to free non-heap address.", p);
         assert(0);
         return;
      }
      if (!check_block(ohp))
         return;
      free(ohp->file);
      free(ohp);
      ohp = NULL;
      dnxDebug(10, "dnxHeap: free(%p).", p);
      p = NULL;
   }
}

//----------------------------------------------------------------------------
 
int dnxCheckHeap(void)
{
   overhead * cp;
   pthread_mutex_lock(&list_lock);
   dnxDebug(1, "dnxCheckHeap: %d unfreed blocks remaining...", blkcnt);
   for (cp = head; cp; cp = cp->next)
   {
      dump_block(cp, "unfreed memory block");
      if (!check_block(cp))
         break;
   }
   pthread_mutex_unlock(&list_lock);
   return blkcnt? -1: 0;
}

/*--------------------------------------------------------------------------*/

