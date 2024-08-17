#include <pthread.h>
#include <stdio.h>
#include <sys/timeb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>

#define MAX_CLIENT_PER_THREAD 300
#define MAX_THREAD 200
#define PORT 50000
#define MAX_CLIENT_BUFFER 256
/*#define DEBUG*/

int listenfd;

typedef struct {
	pthread_t tid;
	int client_count;
	int clients[MAX_CLIENT_PER_THREAD];
} Thread;

pthread_cond_t new_connection_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t new_connection_mutex = PTHREAD_MUTEX_INITIALIZER;


Thread threads[MAX_THREAD];

void nonblock(int sockfd)
{
	int opts;
	opts = fcntl(sockfd, F_GETFL);
	if(opts < 0)
	{
		perror("fcntl(F_GETFL)\n");
		exit(1);
	}
	opts = (opts | O_NONBLOCK);
	if(fcntl(sockfd, F_SETFL, opts) < 0) 
	{
		perror("fcntl(F_SETFL)\n");
		exit(1);
	}
}

void *thread_init_func(void *arg)
{
	int tid = (int) arg;
	
	int readsocks;
	int i;
	char buffer[MAX_CLIENT_BUFFER];
	char c;
	int n;
#ifdef DEBUG
	printf("thread %d created\n", tid);
	printf("sizeof thread.clients: %d\n", sizeof(threads[tid].clients));
#endif
	memset((int *) &threads[tid].clients, 0, sizeof(threads[tid].clients));
	memset((char *) &buffer, 0, sizeof(buffer));	
	while(1)
	{
#ifdef DEBUG
		printf("thread %d running, client count: %d\n", tid, threads[tid].client_count);
		sleep(3);
#endif
		sleep(1); /* <-- it works ??? :-| */

		for(i = 0; i < MAX_CLIENT_PER_THREAD; i++)
		{
			if(threads[tid].clients[i] != 0)
			{
				n = recv(threads[tid].clients[i], buffer, MAX_CLIENT_BUFFER, 0);
				if(n == 0)
				{
#ifdef DEBUG
					printf("client %d closed connection 0\n", threads[tid].clients[i]);
#endif
					threads[tid].clients[i] = 0;
					threads[tid].client_count--;
					memset((char *) &buffer, 0, strlen(buffer));
				}
				else if(n < 0)
				{
					if(errno == EAGAIN)
					{
#ifdef DEBUG
						printf("errno: EAGAIN\n");
#endif
					}
					else {
#ifdef DEBUG
						printf("errno: %d\n", errno);
#endif
						threads[tid].clients[i] = 0;
						threads[tid].client_count--;
						memset( (char *) &buffer, 0, strlen(buffer));
#ifdef DEBUG
						printf("client %d closed connection -1\n", threads[tid].clients[i]);
#endif
					}
				}
				else {
#ifdef DEBUG
					printf("%d bytes received from %d - %s\n", n, threads[tid].clients[i], buffer);
#endif
					
					send(threads[tid].clients[i], buffer, strlen(buffer), 0);
					memset((char *) &buffer, 0, strlen(buffer));
				}
			}
		}
	}
}

int choose_thread()
{
	int i=MAX_THREAD-1;
	int min = 0;
	while(i > -1)
	{
		if(threads[i].client_count < threads[i-1].client_count)
		{
			min = i;
			break;
		}
		i--;
	}		
	return min;
}

int main()
{
	char c;
	struct sockaddr_in srv, cli;
	int clifd;
	int tid;
	int i;
	int choosen;

	signal(SIGPIPE, SIG_IGN);
	
	if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("sockfd\n");
		exit(1);
	}
	bzero(&srv, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_addr.s_addr = INADDR_ANY;
	srv.sin_port = htons(PORT);

	if( bind(listenfd, (struct sockaddr *) &srv, sizeof(srv)) < 0)
	{
		perror("bind\n");
		exit(1);
	}
	
	listen(listenfd, 1024);

	
	/* create threads  */
	for(i = 0; i < MAX_THREAD; i++)
	{
		pthread_create(&threads[i].tid, NULL, &thread_init_func, (void *) i);
		threads[i].client_count = 0;
	}
	

	for( ; ; )
	{
		clifd = accept(listenfd, NULL, NULL);
		nonblock(clifd);

		pthread_mutex_lock(&new_connection_mutex);
		
		choosen = choose_thread();

		for(i = 0; i < MAX_CLIENT_PER_THREAD; i++)
		{
			if(threads[choosen].clients[i] == 0)
			{
#ifdef DEBUG
				printf("before threads clifd\n");
#endif
				threads[choosen].clients[i] = clifd;
#ifdef DEBUG
				printf("after threads clifd\n");
#endif
				threads[choosen].client_count++;
				break;
			}
		}

#ifdef DEBUG
		printf("choosen: %d\n", choosen);

		for(i = 0; i < MAX_THREAD; i++)
		{
			printf("threads[%d].client_count:%d\n", i, threads[i].client_count);
		}
#endif

		pthread_mutex_unlock(&new_connection_mutex);
	}

	if(errno)
	{
		printf("errno: %d", errno);
	}
	
	return 0;
}
