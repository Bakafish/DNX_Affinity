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

void * dnxMalloc(size_t sz, char * file, int line);
void * dnxCalloc(size_t n, size_t sz, char * file, int line);
void * dnxRealloc(void * p, size_t sz, char * file, int line);
char * dnxStrdup(char * str, char * file, int line);
void dnxFree(void * p);
int dnxCheckHeap(void);

#endif   /* _DNXHEAP_H_ */

