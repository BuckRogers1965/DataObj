/******************************************************************
 * Thread and epoll example of supporting 10,000 connections on a linux box
 * Author: James M. Rogers
 */

/* Linux with glibc:
 *   _REENTRANT to grab thread-safe libraries
 *   _POSIX_SOURCE to get POSIX semantics
 */
#ifdef __linux__
#  define _REENTRANT
#  define _POSIX_SOURCE
#endif

#include <stdlib.h>
#include <pthread.h>
#include <string.h>		/* for strerror() */
#include <stdio.h>
#include <errno.h>

#include <sys/epoll.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NTHREADS 200

#define errexit(code,str)                          \
  fprintf(stderr,"%s: %s\n",(str),strerror(code)); \
  exit(1);

typedef struct entry
{
  int flag;
  void *data;
} entry;

#include <ctype.h>
#include <sys/time.h>
#include <fcntl.h>

int sock;			/* The socket file descriptor for our "listening"
				   socket */
int connectlist[5];		/* Array of connected sockets so we know who
				   we are talking to */
fd_set socks;			/* Socket file descriptors we want to wake
				   up for, using select() */
int highsock;			/* Highest #'d file descriptor, needed for select() */


void
setnonblocking (sock)
     int sock;
{
  int opts;

  opts = fcntl (sock, F_GETFL);
  if (opts < 0)
    {
      perror ("fcntl(F_GETFL)");
      exit (EXIT_FAILURE);
    }
  opts = (opts | O_NONBLOCK);
  if (fcntl (sock, F_SETFL, opts) < 0)
    {
      perror ("fcntl(F_SETFL)");
      exit (EXIT_FAILURE);
    }
  return;
}



/******** this is the thread code */
void *
elistener (void *arg)
{

  entry *list = (entry *) arg;
//  printf("l %d\n", list[0].flag);


  int epfd, n, i;

  char buffer[1025];


#define MAX_EVENTS 10000

  struct epoll_event ev, events[MAX_EVENTS];
  int listen_sock, conn_sock, nfds, epollfd;
  struct sockaddr *local;
  socklen_t addrlen;

/* Set up listening socket, 'listen_sock' (socket(),
   bind(), listen()) */

  int port;			/* The port number */
  struct sockaddr_in server_address;	/* bind info structure */
  int reuse_addr = 1;		/* Used so we can re-bind to our port
				   while a previous connection is still
				   in TIME_WAIT state. */
  struct timeval timeout;	/* Timeout for select */


  /* Obtain a file descriptor for our "listening" socket */
  listen_sock = socket (AF_INET, SOCK_STREAM, 0);
  if (listen_sock < 0)
    {
      perror ("socket");
      exit (EXIT_FAILURE);
    }
  /* So that we can re-bind to it without TIME_WAIT problems */
  setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr,
	      sizeof (reuse_addr));

  /* Set socket to non-blocking with our setnonblocking routine */
  setnonblocking (listen_sock);

  /* Get the address information, and bind it to the socket */
  port = 50000;			/* Use function from sockhelp to
				   convert to an int */
  memset ((char *) &server_address, 0, sizeof (server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl (INADDR_ANY);
  server_address.sin_port = htons (port);
  if (bind (listen_sock, (struct sockaddr *) &server_address,
	    sizeof (server_address)) < 0)
    {
      perror ("bind");
      close (listen_sock);
      exit (EXIT_FAILURE);
    }

  /* Set up queue for incoming connections. */
  listen (listen_sock, 1000);


  epollfd = epoll_create (MAX_EVENTS);
  if (epollfd == -1)
    {
      perror ("epoll_create");
      exit (EXIT_FAILURE);
    }

  ev.events = EPOLLIN;
  ev.data.fd = listen_sock;
  if (epoll_ctl (epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1)
    {
      perror ("epoll_ctl: listen_sock");
      exit (EXIT_FAILURE);
    }

  for (;;)
    {
      nfds = epoll_wait (epollfd, events, MAX_EVENTS, 0);
      if (nfds == -1)
	{
	  perror ("epoll_pwait");
	  exit (EXIT_FAILURE);

	}

      for (n = 0; n < nfds; n++)
	{
	  if (events[n].data.fd == listen_sock)
	    {
             while ( (conn_sock = accept (listen_sock, NULL, NULL)) > 0 ) {
				  //(struct sockaddr *) &local, &addrlen);
	      if (conn_sock == -1)
		{
		  perror ("accept");
		  exit (EXIT_FAILURE);
		}

              printf("Connect %d!\n", conn_sock);
              //fprintf(conn_sock, "Banner\n");
              //close (conn_sock);
             // sprintf(buffer, "Socket %d\n", conn_sock);
              //send(conn_sock, buffer, strlen(buffer), 0);

	      setnonblocking (conn_sock);
	      ev.events = EPOLLIN;
	      ev.data.fd = conn_sock;
	      if (epoll_ctl (epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1)
		{
		  perror ("epoll_ctl: conn_sock");
		  //exit (EXIT_FAILURE);
		}
                usleep(1000);
              }
	    }
	  else
	    {
	      //do_use_fd(events[n].data.fd);

          printf("Event %d  :  ", n);
                                i = recv(events[n].data.fd, buffer, 1023, 0);
                                if(i == 0)
                                {
//#ifdef DEBUG
                                        printf("%d closed connection\n", events[n].data.fd);
                                        epoll_ctl(epfd, EPOLL_CTL_DEL, events[n].data.fd, NULL);
                                        close(events[n].data.fd);
//#endif
                                }
                                else if(i < 0)
                                {
//#ifdef DEBUG
                                        printf("%d error occured, errno: %d\n",
                                        	events[n].data.fd, errno);
                                        epoll_ctl(epfd, EPOLL_CTL_DEL, events[n].data.fd, NULL);
                                        close(events[n].data.fd);
//#endif
                                }
                                else {
#ifdef DEBUG
                                        printf("%d data received: >%s<\n",
                                                        events[n].data.fd, buffer);
#endif
//                                        send(events[n].data.fd, buffer, strlen(buffer), 0);
//                                        bzero(&buffer, 1024);
                                }
	    }
	}
      fflush(stdout);
      usleep(10);
    }

  return arg;
}

void *
eacceptor (void *arg)
{
  entry *list = (entry *) arg;
  printf ("a %d\n", list[1].flag);
  return arg;
}

void *
ereader (void *arg)
{
  entry *list = (entry *) arg;
  printf ("r %d\n", list[2].flag);
  return arg;
}

void *
ewriter (void *arg)
{
  entry *list = (entry *) arg;
  printf ("w %d\n", list[3].flag);
  return arg;
}

void *
eprocessor (void *arg)
{
  entry *list = (entry *) arg;
  printf ("p %d\n", list[4].flag);
  return arg;
}


/******** this is the main thread's code */
int
main (int argc, char *argv[])
{
  int worker;
  pthread_t listener;		/*  */
  pthread_t acceptor;		/*  */
  pthread_t reader;		/*  */
  pthread_t writer;		/*  */
  pthread_t processor;		/*  */
  int errcode;			/* holds pthread error code */
  int *status;			/* holds return code */
  int i = 0;


  entry list[5];

  for (i; i < 5; i++)
    {
      list[i].flag = i;
      list[i].data = 0;
    }

  /* create the listen thread. */
  if (errcode = pthread_create (&listener, NULL, elistener, list))
    {
      errexit (errcode, "pthread_create");
    }
  pthread_detach (listener);

  /* create the accept thread. */
  if (errcode = pthread_create (&acceptor, NULL, eacceptor, list))
    {
      errexit (errcode, "pthread_create");
    }
  pthread_detach (acceptor);

  /* create the read thread. */
  if (errcode = pthread_create (&reader, NULL, ereader, list))
    {
      errexit (errcode, "pthread_create");
    }
  pthread_detach (reader);

  /* create the write thread. */
  if (errcode = pthread_create (&writer, NULL, ewriter, list))
    {
      errexit (errcode, "pthread_create");
    }
  pthread_detach (writer);

  /* create the process thread. */
  if (errcode = pthread_create (&processor,	/* thread struct             */
				NULL,	/* default thread attributes */
				eprocessor,	/* start routine             */
				list))
    {				/* arg to routine            */
      errexit (errcode, "pthread_create");
    }
  pthread_detach (processor);

  while (1)
    {
      usleep (10000);
    };
  return (0);
}
