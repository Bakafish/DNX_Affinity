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

/** Types and definitions for work load manager thread.
 * 
 * @file dnxWLM.h
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_CLIENT_IFC
 */

#ifndef _DNXWLM_H_
#define _DNXWLM_H_

typedef struct { int unused; } DnxWLM;

int dnxWLMGetActiveThreads(DnxWLM * wlm);
void dnxWLMSetActiveThreads(DnxWLM * wlm, int value);
int dnxWLMGetActiveJobs(DnxWLM * wlm);
void dnxWLMSetActiveJobs(DnxWLM * wlm, int value);

int dnxWLMCreate(unsigned minsz, unsigned initsz, unsigned maxsz, 
      unsigned incrsz, unsigned term_grace, unsigned * pdebug, DnxWLM ** pwlm);
void dnxWLMDestroy(DnxWLM * wlm);

#endif   /* _DNXWLM_H_ */

