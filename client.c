#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/tcp.h>
#include <fcntl.h>

#include "parallel_calc.h"

struct sockaddr_in their_addr, cli_addr;

#define DEFAULT_PORT 4321

static void udp_cli_con (int port)
{
	char buffer[256];
	int n = 0;
	int opt = 1;
	int sockfd;

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) 
	{
		error("ERROR socket");
	}

	their_addr.sin_family = AF_INET;
	their_addr.sin_port = htons(port);
	their_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) == -1)
		perror("setsockopt");

	if (bind(sockfd, (struct sockaddr *) &their_addr, sizeof(struct sockaddr)) < 0)
		error("ERROR on binding");

	unsigned int len = sizeof (their_addr);
	bzero (buffer, 256);

	n = recvfrom (sockfd, buffer, 255, 0, (struct sockaddr*) &cli_addr, &len);
	if (n <= 0)
		error ("ERROR reading from socket");

	memcpy((char *)&their_addr.sin_addr.s_addr, buffer, sizeof (long int));
	close (sockfd);
}

static int tcp_cli_con (int port)
{
	int opt = 1;
	int sockfd;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
	{
		error("ERROR socket");
	}

	if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) == -1)
		perror("setsockopt");

	int keepcnt = 1;
	int keepidle = 1;
	int keepintvl = 1;

	if (setsockopt (sockfd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof (opt)) != 0)
		error ("setsockopt KEEPALIVE");
	if (setsockopt (sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof (int)) != 0)
		error ("setsockopt KEEPCNT");
	if (setsockopt (sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof (int)) != 0)
		error ("setsockopt KEEPIDLE");
	if (setsockopt (sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof (int)) != 0)
		error ("setsockopt KEEPINTVL");

	their_addr.sin_family = AF_INET;
	their_addr.sin_port = htons(port);
	if (connect (sockfd, (struct sockaddr *) &their_addr, sizeof (their_addr)) < 0)
		perror("ERROR connecting");

	return sockfd;
}

int client_connect (int port, int th_num)
{
	char buffer[256];
	int n = 0;
	int sockfd;
	double sc = 1000;
	double ec = 0;
	double res = 0;
	fd_set sfds;
	struct timeval timeout={0, 0};
	int ret;

	udp_cli_con (port);
	sockfd = tcp_cli_con (port);
/*--------------------------------------------------------------------*/
/*------------------------- Communication ----------------------------*/
/*--------------------------------------------------------------------*/

	memcpy (buffer, &th_num, sizeof (int));

	bzero (&timeout, sizeof (timeout));
	FD_ZERO(&sfds);
	FD_SET(sockfd, &sfds);
	ret = select(sockfd + 1, NULL, &sfds, NULL, &timeout);
	if (ret < 0)
		error ("ERROR write");

	n = write (sockfd, buffer, sizeof (int));
	if (n <= 0)
		error ("ERROR writing to socket");

	bzero (buffer, 256);

	bzero (&timeout, sizeof (timeout));
	FD_ZERO(&sfds);
	FD_SET(sockfd, &sfds);
	ret = select(sockfd + 1, &sfds, NULL, NULL, &timeout);
	if (ret < 0)
		error ("ERROR read");

	n = read (sockfd, buffer, 255);
	if (n <= 0)
		error ("ERROR reading from socket");

	memcpy (&sc, buffer, sizeof (double));
	memcpy (&ec, buffer + sizeof (double), sizeof (double));

	start_parallel (sc, ec, &res, th_num);

	bzero (buffer, 256);
	memcpy (buffer, &res, sizeof (double));

	bzero (&timeout, sizeof (timeout));
	FD_ZERO(&sfds);
	FD_SET(sockfd, &sfds);
	ret = select(sockfd + 1, NULL, &sfds, NULL, &timeout);
	if (ret < 0 || FD_ISSET(sockfd, &sfds) == 0)
		error ("ERROR write");

	n = write (sockfd, buffer, sizeof (double));
	if (n <= 0)
		error ("ERROR writing to socket");

	bzero (&timeout, sizeof (timeout));
	FD_ZERO(&sfds);
	FD_SET(sockfd, &sfds);
	ret = select(sockfd + 1, &sfds, NULL, NULL, &timeout);
	if (ret < 0)
		error ("ERROR read");

	n = read (sockfd, buffer, 255);
	if (n <= 0)
		error ("ERROR reading from socket");

	close(sockfd);

	return 0;
}

int main(int argc, char* argv[])
{
	int port = DEFAULT_PORT;

	/*if(parse_args(argc, argv, &th_num, &port) == -1)
		return 0;*/
	int th_num = 0;

	if ((port == 0) || (argc != 2))
	{
		printf("Invalid input\n");
		return 1;
	}
	
	sscanf(argv[1], "%d", &th_num);	

	client_connect(port, th_num);

	return 0;
}
