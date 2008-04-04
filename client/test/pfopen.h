/* pfopen.h
 *
 * Header file for pfopen.c
 *
 * Alternative to popen(2) that allows reading from both stdout and stderr
 * of the child shell process.  Functionally the same as popen(2) when writing
 * to the stdin of the child shell process.
 *
 * Works similarly to popen(2) except that it returns a PFILE * instead of
 * the standard FILE *.  This allows us to supply multiple I/O streams for
 * reading.  It also contains the pid of the child process, which is used
 * by the complementary pfclose() function for shutting down the pipe.
 *
 * The other difference is that when using the PFILE * with the standard
 * I/O functions (e.g., fgets, fprintf, etc.) you must use the supplied
 * macros in pfopen.h in order to obtain the underlying FILE * for use
 * with the stdio functions.
 *
 * These macros are:
 *
 *  PF_IN(p)  - Retrieves FILE * handle for writing to child process' stdin
 *  PF_OUT(p) - Retrieves FILE * handle for reading from child process' stdout
 *  PF_ERR(p) - Retrieves FILE * handle for reading from child process' stderr
 *
 *	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 *
 *	First Written:   2006-09-05
 *	Last Modified:   2007-03-21
 *
 *	License:
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifndef _PFOPEN_H_
#define _PFOPEN_H_

#define PF_IN(pf)	((pf)->fp[0])
#define PF_OUT(pf)	((pf)->fp[1])
#define PF_ERR(pf)	((pf)->fp[2])


typedef struct _pfile_ {
    FILE *fp[3];
    pid_t pid;
} PFILE;



PFILE *pfopen(const char *cmdstring, const char *type);
int pfclose(PFILE *pfile);
int pfkill(PFILE *pfile, int sig);

#endif
