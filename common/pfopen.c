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

/* pfopen.c
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


#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#include "pfopen.h"

#if 0
#define PF_IN(pf)	((pf)->fp[0])
#define PF_OUT(pf)	((pf)->fp[1])
#define PF_ERR(pf)	((pf)->fp[2])


typedef struct _pfile_ {
    FILE *fp[3];
    pid_t pid;
} PFILE;
#endif



PFILE *
pfopen(const char *cmdstring, const char *type)
{
    PFILE  *pfile;
    int     pfd1[2];
    int     pfd2[2];
    int     pid;

    /* only allow "r" or "w" */
    if ((type[0] != 'r' && type[0] != 'w') || type[1] != 0) {
        errno = EINVAL;     /* required by POSIX */
        return(NULL);
    }

    /* Allocate a PFILE structure */
    if ((pfile = (PFILE *)malloc(sizeof(PFILE))) == NULL)
    {
        errno = ENOMEM;
        return(NULL);
    }
    memset(pfile, 0, sizeof(PFILE));

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
        return(NULL);   /* errno set by pipe() */
    if (type[0] == 'r' && pipe(pfd2) < 0)
    {
        close(pfd1[0]);
        close(pfd1[1]);
        return(NULL);   /* errno set by pipe() */
    }

    if ((pid = fork()) < 0) {
        return(NULL);   /* errno set by fork() */
    } else if (pid == 0) {                           /* child */
        setpgid(0, 0);   /* make child its own process group */
        if (*type == 'r') {
            close(pfd1[0]);
            close(pfd2[0]);
            if (pfd1[1] != STDOUT_FILENO) {
                dup2(pfd1[1], STDOUT_FILENO);
                close(pfd1[1]);
            }
            if (pfd2[1] != STDERR_FILENO) {
                dup2(pfd2[1], STDERR_FILENO);
                close(pfd2[1]);
            }
        } else {
            close(pfd1[1]);
            if (pfd1[0] != STDIN_FILENO) {
                dup2(pfd1[0], STDIN_FILENO);
                close(pfd1[0]);
            }
        }

        execl("/bin/sh", "sh", "-c", cmdstring, (char *)0);
        _exit(127);
    }

    /* parent continues... */
    if (*type == 'r') {
        close(pfd1[1]);
        close(pfd2[1]);
        /* this corresponds to child process' STDOUT */
        if ((pfile->fp[1] = fdopen(pfd1[0], type)) == NULL)
            return(NULL);
        /* this corresponds to child process' STDERR */
        if ((pfile->fp[2] = fdopen(pfd2[0], type)) == NULL)
            return(NULL);
    } else {
        close(pfd1[0]);
        /* this corresponds to child process' STDIN */
        if ((pfile->fp[0] = fdopen(pfd1[1], type)) == NULL)
            return(NULL);
    }

    pfile->pid = pid; /* remember child pid for this fd */

    return(pfile);
}


int
pfclose(PFILE *pfile)
{
    int stat;

    if (pfile == NULL) {
        errno = EINVAL;
        return(-1);     /* popen() has never been called */
    }

    if (pfile->fp[0] != NULL)
        fclose(pfile->fp[0]);
    if (pfile->fp[1] != NULL)
        fclose(pfile->fp[1]);
    if (pfile->fp[2] != NULL)
        fclose(pfile->fp[2]);

    while (waitpid(pfile->pid, &stat, 0) < 0)
        if (errno != EINTR)
            return(-1); /* error other than EINTR from waitpid() */

    return(stat);   /* return child's termination status */
}

int pfkill(PFILE *pfile, int sig)
{
    if (pfile == NULL) {
        errno = EINVAL;
        return(-1);     /* popen() has never been called */
    }

    /* send specified signal to child process group */
    return kill(-(pfile->pid), sig);
}

/*--------------------------------------------------------------------------*/

