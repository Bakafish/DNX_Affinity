#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>


int main (int argc, char **argv)
{
	struct sockaddr_in inaddr, client;
	char msg[512], resp[512];
	socklen_t slen;
	int sd;
	int rc;


	// Create UDP socket
	if ((sd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
	{
		fprintf(stderr, "%s: Failed to open UDP socket: %s\n", argv[0], strerror(errno));
		exit(1);
	}

	// Setup the server socket address structure
    inaddr.sin_family = AF_INET;
	inaddr.sin_addr.s_addr = htonl(0x7f000001);
    inaddr.sin_port = htons(30400);

	// Bind to our local port
	if (bind(sd, (struct sockaddr *)&inaddr, sizeof(inaddr)) != 0)
	{
		close(sd);
		fprintf(stderr, "%s: Failed to bind to local address: %s\n", argv[0], strerror(errno));
		exit(2);
	}

	printf("Awaiting test packet from client on port %u\n", (unsigned)(ntohs(inaddr.sin_port)));
	fflush(stdout);

	// Await a test message
	slen = sizeof(client);
	if ((rc = recvfrom(sd, msg, sizeof(msg), 0, (struct sockaddr *)&client, &slen)) < 0)
	{
		close(sd);
		fprintf(stderr, "%s: Failed to read test packet: %s\n", argv[0], strerror(errno));
		exit(3);
	}

	if (rc < sizeof(msg))
		msg[rc] = '\0';
	else
		msg[sizeof(msg)-1] = '\0';

	printf("Received message from %lu.%lu.%lu.%lu:%u: %s\n",
		(unsigned long) (client.sin_addr.s_addr        & 0xff),
		(unsigned long)((client.sin_addr.s_addr >>  8) & 0xff),
		(unsigned long)((client.sin_addr.s_addr >> 16) & 0xff),
		(unsigned long)((client.sin_addr.s_addr >> 24) & 0xff),
		(unsigned)(ntohs(client.sin_port)),
		msg);
	fflush(stdout);

	// Setup response message
	strcpy(resp, "Never leave for tomorrow that which you are able to do today.");

	// Send response packet to test client
	if ((rc = sendto(sd, resp, strlen(resp), 0, (struct sockaddr *)&client, sizeof(client))) != strlen(resp))
	{
		fprintf(stderr, "%s: Failed to write response packet: %d bytes written (%d)\n", argv[0], rc, errno);
	}
	else
	{
		printf("Sent test packet to client.\n");
		fflush(stdout);
	}

	// Close socket and cleanup
	close(sd);

	return 0;
}
