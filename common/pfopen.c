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

/** Implements the DNX Client logging functions.
 *
 * @file pfopen.c
 * @author Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
 * @attention Please submit patches to http://dnx.sourceforge.net
 * @ingroup DNX_COMMON_IMPL
 */

#include "pfopen.h"

#include "dnxDebug.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <assert.h>

//----------------------------------------------------------------------------

/** Open a child process whose STDIO is redirected to the parent.
 * 
 * The STDIN, STDOUT and STDERR file handles for the child process are 
 * available to the parent (the calling process) to be used in any 
 * appropriate manner.
 * 
 * The only allowable modes for the pipe are 'read' and 'write' as specified
 * by the @p type parameter in the form of a single 'r' or 'w' character
 * string.
 * 
 * @param[in] cmdstring - the command string to be executed as a child process.
 * @param[in] type - the file type string to use when opening the I/O channel
 *    to the child process. 
 * 
 * @return A newly allocated PFILE object reference. This object may be 
 * queried or closed using the other calls defined in this module. If this
 * routine returns NULL, it indicates an error and errno is set appropriately.
 */
PFILE * pfopen(const char * cmdstring, const char * type)
{
   PFILE * pfile = NULL;
   int pfd1[2];
   int pfd2[2];
   int pid;
   
   /* only allow "r" or "w" */
   assert((type[0] == 'r' || type[0] == 'w') &&  type[1] == 0);
   
//    if(pfile)
//         xfree(pfile); //placed due to valgrind memory leak complaint
//   if ((pfile = (PFILE *)xmalloc(sizeof(PFILE))) == NULL)
//    {
//       if(pfile)
//         xfree(pfile); //placed due to valgrind memory leak complaint
//       errno = ENOMEM;
//       return NULL;
//    }

   /* Allocate a PFILE structure */
   if ((pfile = (PFILE *)xcalloc(1,sizeof(PFILE))) == NULL)
   {
      errno = ENOMEM;
      return NULL;
   }
//    memset(pfile, 0, sizeof(PFILE));
   
   /*
    * Strategy: Use up to two pipes for communication:
    *
    * For read:  pfd1 = stdout
    *            pfd2 = stderr
    *
    * For write: pfd1 = stdin
    *            pfd2 = Not Used
    *
    */
   if (pipe(pfd1) < 0)
      return NULL;      /* errno set by pipe() */

   if (type[0] == 'r' && pipe(pfd2) < 0)
   {
      close(pfd1[0]);
      close(pfd1[1]);
      xfree(pfile);
      return NULL;      /* errno set by pipe() */
   }
   
   if ((pid = fork()) < 0) 
   {
      xfree(pfile);
      return NULL;      /* errno set by fork() */
   }else if (pid == 0)   /* child */
   {
      setpgid(0, 0);    /* make child its own process group */
      if (*type == 'r') 
      {
         close(pfd1[0]);
         close(pfd2[0]);
         if (pfd1[1] != STDOUT_FILENO) 
         {
            dup2(pfd1[1], STDOUT_FILENO);
            close(pfd1[1]);
         }
         if (pfd2[1] != STDERR_FILENO) 
         {
            dup2(pfd2[1], STDERR_FILENO);
            close(pfd2[1]);
         }
      } 
      else 
      {
         close(pfd1[1]);
         if (pfd1[0] != STDIN_FILENO) 
         {
            dup2(pfd1[0], STDIN_FILENO);
            close(pfd1[0]);
         }
      }
   
      execl("/bin/sh", "sh", "-c", cmdstring, (char *)0);
      _exit(127);
   }
   
   /* parent continues... */
   if (*type == 'r') 
   {
      close(pfd1[1]);
      close(pfd2[1]);

      /* this corresponds to child process' STDOUT */
      if ((pfile->fp[1] = fdopen(pfd1[0], type)) == NULL)
      {
	    xfree(pfile);
        return NULL;
      }
      /* this corresponds to child process' STDERR */
      if ((pfile->fp[2] = fdopen(pfd2[0], type)) == NULL)
      {
         xfree(pfile);
         return NULL;
      }
   }
   else 
   {
      close(pfd1[0]);

      /* this corresponds to child process' STDIN */
      if ((pfile->fp[0] = fdopen(pfd1[1], type)) == NULL)
      {
         xfree(pfile);
         return NULL;
      }
   }

   pfile->pid = pid; /* remember child pid for this fd */

   return pfile;
}

//----------------------------------------------------------------------------

/** Close an existing PFILE object.
 * 
 * Closes all open pipe handles, and then waits for the associated child 
 * process to die, returning the status value of the child process.
 * 
 * @param[in] pfile - the process pipe to be closed.
 * 
 * @return The value of the waitpid system call's stat_loc parameter, or the
 * status of the child processed waited for using the waitpid system call.
 * 
 * @note Because the return value of this routine is the stat_loc value of
 * the waitpid system call, any of the W* macros defined for use on this 
 * value by the system may be used to query the status of the associated 
 * child process.
 */
int pfclose(PFILE * pfile)
{
   int stat;
   
   if (pfile == NULL) 
   {
      errno = EINVAL;
      return -1;     /* popen() has never been called */
   }
   
   if (pfile->fp[0] != NULL)
      fclose(pfile->fp[0]);
   if (pfile->fp[1] != NULL)
      fclose(pfile->fp[1]);
   if (pfile->fp[2] != NULL)
      fclose(pfile->fp[2]);
   
   xfree(pfile);

   while (waitpid(pfile->pid, &stat, 0) < 0)
      if (errno != EINTR)
         return -1;  /* error other than EINTR from waitpid() */
   
//    if(pfile)
//     xfree(pfile);
// 
   return stat;      /* return child's termination status */
}

//----------------------------------------------------------------------------

/** Kill a process associated with an existing PFILE object.
 * 
 * @param[in] pfile - the pipe representing the process to be killed.
 * @param[in] sig - the signal to send the process.
 * 
 * @return Zero on success, or -1 on error, and the global errno is set.
 */
int pfkill(PFILE * pfile, int sig)
{
   assert(pfile);
   
   /* send specified signal to child process group */
   return kill(-(pfile->pid), sig);
}

/*--------------------------------------------------------------------------*/

