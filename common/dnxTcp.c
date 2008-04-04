//	dnxTcp.c
//
//	Implements the TCP Tranport Layer
//
//	Copyright (c) 2006-2007 Robert W. Ingraham (dnx-devel@lists.sourceforge.net)
//
//	First Written:   2006-06-19
//	Last Modified:   2007-09-26
//
//	License:
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License version 2 as
//	published by the Free Software Foundation.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>

#include "dnxError.h"
#include "dnxTcp.h"
#include "dnxLogging.h"


//
//	Constants
//


//
//	Structures
//


//
//	Globals
//

static pthread_mutex_t tcpMutex;
static pthread_mutexattr_t tcpMutexAttr;


//
//	Prototypes
//

extern int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int kind);


//----------------------------------------------------------------------------

int dnxTcpInit (void)
{
	// Create protective mutex for non-reentrant functions (gethostbyname)
	pthread_mutexattr_init(&tcpMutexAttr);
#ifdef PTHREAD_MUTEX_ERRORCHECK_NP
	pthread_mutexattr_settype(&tcpMutexAttr, PTHREAD_MUTEX_ERRORCHECK_NP);
#endif
	pthread_mutex_init(&tcpMutex, &tcpMutexAttr);

	return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxTcpDeInit (void)
{
	// Destroy the mutex
	if (pthread_mutex_destroy(&tcpMutex) != 0)
		dnxSyslog(LOG_ERR, "dnxTcpDeInit: Unable to destroy mutex: mutex is in use!");
	pthread_mutexattr_destroy(&tcpMutexAttr);

	return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxTcpNew (dnxChannel **channel, char *url)
{
	char tmpUrl[DNX_MAX_URL+1];
	char *cp, *ep, *lastchar;
	long port;

	// Validate parameters
	if (!channel || !url || !*url || strlen(url) > DNX_MAX_URL)
		return DNX_ERR_INVALID;

	*channel = NULL;

	// Make a working copy of the URL
	strcpy(tmpUrl, url);

	// Look for transport prefix: '[type]://'
	if ((ep = strchr(tmpUrl, ':')) == NULL || *(ep+1) != '/' || *(ep+2) != '/')
		return DNX_ERR_BADURL;
	*ep = '\0';
	cp = ep + 3;	// Set to beginning of destination portion of the URL

	// Search for hostname - port separator
	if ((ep = strchr(cp, ':')) == NULL || ep == cp)
		return DNX_ERR_BADURL;	// No separator found or empty hostname
	*ep++ = '\0';

	// Get the port number
	errno = 0;
	if ((port = strtol(ep, &lastchar, 0)) < 1 || port > 65535 || (*lastchar && *lastchar != '/'))
		return DNX_ERR_BADURL;

	// Allocate a new channel structure
	if ((*channel = (dnxChannel *)malloc(sizeof(dnxChannel))) == NULL)
	{
		dnxSyslog(LOG_ERR, "dnxTcpNew: Out of Memory: malloc(dnxChannel)");
		return DNX_ERR_MEMORY;	// Memory allocation error
	}
	memset(*channel, 0, sizeof(dnxChannel));

	// Save host name and port
	(*channel)->type = DNX_CHAN_TCP;
	(*channel)->name = NULL;
	if (((*channel)->host = strdup(cp)) == NULL)
	{
		dnxSyslog(LOG_ERR, "dnxTcpNew: Out of Memory: strdup(channel->host)");
		free(*channel);
		*channel = NULL;
		return DNX_ERR_MEMORY;	// Memory allocation error
	}
	(*channel)->port = (int)port;
	(*channel)->state = DNX_CHAN_CLOSED;

	// Set I/O methods
	(*channel)->dnxOpen  = dnxTcpOpen;
	(*channel)->dnxClose = dnxTcpClose;
	(*channel)->dnxRead  = dnxTcpRead;
	(*channel)->dnxWrite = dnxTcpWrite;
	(*channel)->txDelete = dnxTcpDelete;

	return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxTcpDelete (dnxChannel *channel)
{
	// Validate parameters
	if (!channel || channel->type != DNX_CHAN_TCP)
		return DNX_ERR_INVALID;

	// Make sure this channel is closed
	if (channel->state == DNX_CHAN_OPEN)
		dnxTcpClose(channel);

	// Release host name string
	if (channel->host) free(channel->host);

	// Release channel memory
	memset(channel, 0, sizeof(dnxChannel));
	free(channel);

	return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxTcpOpen (dnxChannel *channel, dnxChanMode mode)	// 0=Passive, 1=Active
{
	struct hostent *he;
	struct sockaddr_in inaddr;
	int sd;

	// Validate parameters
	if (!channel || channel->type != DNX_CHAN_TCP || channel->port < 1)
		return DNX_ERR_INVALID;

	// Make sure this channel isn't already open
	if (channel->state != DNX_CHAN_CLOSED)
		return DNX_ERR_ALREADY;

	// Setup the socket address structure
	inaddr.sin_family = AF_INET;
	inaddr.sin_port = (in_port_t)channel->port;
	inaddr.sin_port = htons(inaddr.sin_port);

	// See if we are listening on any address
	if (!strcmp(channel->host, "INADDR_ANY") || !strcmp(channel->host, "0.0.0.0") || !strcmp(channel->host, "0"))
	{
		// Make sure that the request is passive
		if (mode != DNX_CHAN_PASSIVE)
			return DNX_ERR_ADDRESS;

		inaddr.sin_addr.s_addr = INADDR_ANY;
	}
	else	// Resolve destination address
	{
		// Acquire the lock
		if (pthread_mutex_lock(&tcpMutex) != 0)
		{
			switch (errno)
			{
			case EINVAL:	// mutex not initialized
				dnxSyslog(LOG_ERR, "dnxTcpOpen: mutex_lock: mutex has not been initialized");
				break;
			case EDEADLK:	// mutex already locked by this thread
				dnxSyslog(LOG_ERR, "dnxTcpOpen: mutex_lock: deadlock condition: mutex already locked by this thread!");
				break;
			default:		// Unknown condition
				dnxSyslog(LOG_ERR, "dnxTcpOpen: mutex_lock: unknown error %d: %s", errno, strerror(errno));
			}
			return DNX_ERR_THREAD;
		}

		// Try to resolve this address
		if ((he = gethostbyname(channel->host)) == NULL)
		{
			pthread_mutex_unlock(&tcpMutex);
			return DNX_ERR_ADDRESS;
		}
		memcpy(&(inaddr.sin_addr.s_addr), he->h_addr_list[0], he->h_length);

		// Release the lock
		if (pthread_mutex_unlock(&tcpMutex) != 0)
		{
			switch (errno)
			{
			case EINVAL:	// mutex not initialized
				dnxSyslog(LOG_ERR, "dnxTcpOpen: mutex_unlock: mutex has not been initialized");
				break;
			case EPERM:		// mutex not locked by this thread
				dnxSyslog(LOG_ERR, "dnxTcpOpen: mutex_unlock: mutex not locked by this thread!");
				break;
			default:		// Unknown condition
				dnxSyslog(LOG_ERR, "dnxTcpOpen: mutex_unlock: unknown error %d: %s", errno, strerror(errno));
			}
			return DNX_ERR_THREAD;
		}
	}

	// Create a socket
	if ((sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		return DNX_ERR_OPEN;

	// Determing whether we are connecting or listening
	if (mode == DNX_CHAN_ACTIVE)
	{
		// Attempt to open the socket connect
		if (connect(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
		{
			close(sd);
			return DNX_ERR_OPEN;
		}
	}
	else	// DNX_CHAN_PASSIVE
	{
		// Bind the socket to a local address and port
		if (bind(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
		{
			close(sd);
			return DNX_ERR_OPEN;
		}

		// Set the listen depth
		listen(sd, DNX_TCP_LISTEN);
	}

	// Mark the channel as open
	channel->chan  = sd;
	channel->state = DNX_CHAN_OPEN;

	return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxTcpClose (dnxChannel *channel)
{
	// Validate parameters
	if (!channel || channel->type != DNX_CHAN_TCP)
		return DNX_ERR_INVALID;

	// Make sure this channel isn't already closed
	if (channel->state != DNX_CHAN_OPEN)
		return DNX_ERR_ALREADY;

	// Shutdown the communication paths on the socket
	shutdown(channel->chan, SHUT_RDWR);

	// Close the socket
	close(channel->chan);

	// Mark the channel as closed
	channel->state = DNX_CHAN_CLOSED;
	channel->chan  = 0;

	return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxTcpRead (dnxChannel *channel, char *buf, int *size, int timeout, char *src)
{
	struct sockaddr_in src_addr;
	socklen_t slen;
	char mbuf[DNX_MAX_MSG];
	unsigned short mlen;
	fd_set fd_rds;
	struct timeval tv;
	int nsd;

	// Validate parameters
	if (!channel || channel->type != DNX_CHAN_TCP || !buf || *size < 1)
		return DNX_ERR_INVALID;

	// Make sure this channel is open
	if (channel->state != DNX_CHAN_OPEN)
		return DNX_ERR_OPEN;

	// Implement timeout logic, if timeout value is > 0
	if (timeout > 0)
	{
		FD_ZERO(&fd_rds);
		FD_SET(channel->chan, &fd_rds);
		tv.tv_sec = (long)timeout;
		tv.tv_usec = 0L;
		if ((nsd = select((channel->chan+1), &fd_rds, NULL, NULL, &tv)) == 0)
			return DNX_ERR_TIMEOUT;
		else if (nsd < 0)
		{
			dnxSyslog(LOG_ERR, "dnxTcpRead: select failure: %s", strerror(errno));
			return DNX_ERR_RECEIVE;
		}
	}

	// First, read the incoming message length
	if (read(channel->chan, &mlen, sizeof(mlen)) != sizeof(mlen))
		return DNX_ERR_RECEIVE;
	mlen = ntohs(mlen);

	// Validate the message length
	if (mlen < 1 || mlen > DNX_MAX_MSG)
		return DNX_ERR_RECEIVE;

	// Check to see if the message fits within the user buffer
	if (*size >= mlen)
	{
		// User buffer is adequate, read directly into it
		if (read(channel->chan, buf, (int)mlen) != (int)mlen)
			return DNX_ERR_RECEIVE;
		*size = (int)mlen;
	}
	else
	{
		// User buffer is inadequate, read whole message and truncate
		if (read(channel->chan, mbuf, (int)mlen) == (int)mlen)
			return DNX_ERR_RECEIVE;
		memcpy(buf, mbuf, *size);	// Copy portion that fits
		// No need to adjust size variable, since we used the all of it
	}

	// Set source IP/port information, if desired
	if (src)
	{
		if (getpeername(channel->chan, (struct sockaddr *)&src_addr, &slen) == 0)
			memcpy(src, &src_addr, sizeof(src_addr));
		else
			*src = 0;	// Set zero-byte to indicate no source address avavilable
	}

	return DNX_OK;
}

//----------------------------------------------------------------------------

int dnxTcpWrite (dnxChannel *channel, char *buf, int size, int timeout, char *dst)
{
	fd_set fd_wrs;
	struct timeval tv;
	int nsd;
	unsigned short mlen;

	// Validate parameters
	if (!channel || channel->type != DNX_CHAN_TCP || !buf)
		return DNX_ERR_INVALID;

	// Validate that the message size is within bounds
	if (size < 1 || size > DNX_MAX_MSG)
		return DNX_ERR_SIZE;

	// Make sure this channel is open
	if (channel->state != DNX_CHAN_OPEN)
		return DNX_ERR_OPEN;

	// Implement timeout logic, if timeout value is > 0
	if (timeout > 0)
	{
		FD_ZERO(&fd_wrs);
		FD_SET(channel->chan, &fd_wrs);
		tv.tv_sec = (long)timeout;
		tv.tv_usec = 0L;
		if ((nsd = select((channel->chan+1), NULL, &fd_wrs, NULL, &tv)) == 0)
			return DNX_ERR_TIMEOUT;
		else if (nsd < 0)
		{
			dnxSyslog(LOG_ERR, "dnxTcpWrite: select failure: %s", strerror(errno));
			return DNX_ERR_SEND;
		}
	}

	// Convert the size into a network ushort
	mlen = (unsigned short)size;
	mlen = htons(mlen);

	// Send the length of the message as a header
	if (write(channel->chan, &mlen, sizeof(mlen)) != sizeof(mlen))
		return DNX_ERR_SEND;

	// Send the message
	if (write(channel->chan, buf, size) != size)
		return DNX_ERR_SEND;
	
	return DNX_OK;
}

//----------------------------------------------------------------------------
