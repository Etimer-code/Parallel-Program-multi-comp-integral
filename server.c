#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <errno.h>
#include <limits.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <signal.h>

#include "parallel_calc.h"

#define MAX_CLIENT 16
#define BACKLOG 16

#define DEFAULT_BEGIN 0
#define DEFAULT_END 5

int newsockfd[MAX_CLIENT];

#define PORT_STR "4321"
#define DEFAULT_PORT 4321

int num_pc = 0;
int num_num = 0;

static void udp_serv_con(int port)
{
	int broadcastOn = 1;
	struct sockaddr_in addr;
	int sockfd = 0;

	int ipsock;
	struct ifreq ifr;

	ipsock = socket(AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "enp2s0", IFNAMSIZ-1);

	ioctl(ipsock, SIOCGIFBRDADDR, &ifr);

	//printf("%s\n", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr =((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;

	addr.sin_port=htons(port);

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		error("listener: socket");

	if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastOn, 4) != 0)
		error("listener: setsockopt");

	//addr.sin_addr.s_addr |= htonl(0x1ff);

	ioctl(ipsock, SIOCGIFADDR, &ifr);
	close(ipsock);

	if (sendto(sockfd, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr, sizeof (long int), 0,(struct sockaddr *) &addr, sizeof(addr)) < 0)
		error("sendto");

	close (sockfd);
}


void check_connect(int signo)
{
	if (num_pc != (num_num-1))
	{
		printf("No clients\n");
		exit(0);
	}
}

static int tcp_serv_con (int port, int num)
{
	int optval = 1;
	int sockfd = 0;
	struct sockaddr_in serv_addr, cli_addr;

	struct sigaction alarm_hdl;

	alarm_hdl.sa_handler = check_connect;
	sigfillset(&alarm_hdl.sa_mask);
	sigaction(SIGALRM, &alarm_hdl, NULL);

	fd_set sfds;
	struct timeval timeout={0, 0};
	int ret;

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);


	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		error("listener: socket");
	/*if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK) < 0)
		error ("fcntl");*/
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval) != 0)
		error("listener: setsockopt");

	int keepcnt = 1;
	int keepidle = 1;
	int keepintvl = 1;
	if (setsockopt (sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof (optval)) != 0)
		error ("setsockopt KEEPALIVE");
	if (setsockopt (sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(int)) != 0)
		error ("setsockopt KEEPCNT");
	if (setsockopt (sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(int)) != 0)
		error ("setsockopt KEEPIDLE");
	if (setsockopt (sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(int)) != 0)
		error ("setsockopt KEEPINTVL");

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0) 
	{
		close(sockfd);
		error("listener: bind");
	}

	alarm(3);
	for (num_pc = 0; num_pc < num; num_pc++) 
	{
		if (listen(sockfd, BACKLOG) == -1)
		{
			close(sockfd);
			error ("ERROR listen");
		}

		size_t addr_size = sizeof(cli_addr);
		FD_ZERO(&sfds);
		FD_SET(sockfd, &sfds);
		ret = select(sockfd + 1, &sfds, NULL, NULL, &timeout);
		if (ret < 0)
			error ("ERROR Accept");

		if ((newsockfd[num_pc] = accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t *)&addr_size)) < 0)
		{
			close (sockfd);
			return -1;
		}
	}
	alarm_hdl.sa_handler = SIG_IGN;
	sigfillset(&alarm_hdl.sa_mask);
	sigaction(SIGALRM, &alarm_hdl, NULL);
	return 0;
}

int server_connect(int port, int num)
{
	double end = DEFAULT_END;
	double begin = DEFAULT_BEGIN;
	char buffer[256];
	int i = 0;
	int n = 0;
	int th_num[MAX_CLIENT];
	int all_th = 0;
	double sc = begin;
	double cur_st = sc;
	double ec = 0;

	fd_set sfds;
	struct timeval timeout={0, 0};
	int ret;

/*--------------------------------------------------------------------*/
/*------------------------- Communication ----------------------------*/
/*--------------------------------------------------------------------*/

	for (i = 0; i < num; i++) 
	{
		FD_ZERO(&sfds);
		FD_SET(newsockfd[i], &sfds);
		ret = select(newsockfd[i] + 1, &sfds, NULL, NULL, &timeout);
		if (ret < 0)
			error ("ERROR read");
		n = read (newsockfd[i], buffer, 255);
		if (n <= 0)
			error ("ERROR reading from socket");
		memcpy (&th_num[i], buffer, sizeof (int));
	}

	for (i = 0; i < num; i++)
		all_th += th_num[i];

	double width_per_serv = (end - begin) / all_th;

	for (i = 0; i < num; i++) 
	{
		sc = cur_st;
		ec = cur_st + width_per_serv * th_num[i];
		cur_st = ec;
		memcpy (buffer, &sc, sizeof (double));
		memcpy (buffer + sizeof (double), &ec, sizeof (double));

		FD_ZERO(&sfds);
		FD_SET(newsockfd[i], &sfds);
		ret = select(newsockfd[i] + 1, NULL, &sfds, NULL, &timeout);
		if (ret < 0)
			error ("ERROR write");
		n = write (newsockfd[i], buffer, 2 * sizeof (double));
		if (n <= 0)
			error ("ERROR writing to socket");
	}

	double tmp = 0;
	double res = 0;
	for (i = 0; i < num; i++) 
	{
		bzero (buffer, 256);

		FD_ZERO(&sfds);
		FD_SET(newsockfd[i], &sfds);
		ret = select(newsockfd[i] + 1, &sfds, NULL, NULL, &timeout);
		if (ret < 0)
			error ("ERROR read");

		n = read (newsockfd[i], buffer, 255);
		if (n <= 0)
			error ("ERROR reading from socket");

		memcpy (&tmp, buffer, n);
		res += tmp;
	}

	for (i = 0; i < num; i++) 
	{
		FD_ZERO(&sfds);
		FD_SET(newsockfd[i], &sfds);
		ret = select(newsockfd[i] + 1, NULL, &sfds, NULL, &timeout);
		if (ret < 0)
			error ("ERROR write");
		n = write (newsockfd[i], buffer, sizeof (double));
		if (n <= 0)
			error ("ERROR writing to socket");
    }

	printf ("result: %lf\n", res);
	return 0;
}

int main(int argc, char* argv[])
{
	int pc_num;
	int port = 4321;
    
	if (argv[2])
	{
		printf("Not enough args\n");
		return 1;
	}
	sscanf(argv[1], "%d", &pc_num);
	num_num = pc_num;

	
    while (1) 
	{
		udp_serv_con (port);
		if (tcp_serv_con (port, pc_num) == 0)
			break;
    }

	server_connect(port, pc_num);
	return 0;
}
