
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef S_SPLINT_S

/*
 * cleanup 
 *
 * This function exercises and demos all the functionality of the state library
 *
 */

StateFunc (cleanup){

  struct s_pData *local_pointer;



  local_pointer = (struct s_pData *)fData;
  local_pointer->Running=FALSE;

 // Close all the open connections

  printf("Ending the state machine\n");
  NextState(NULL);
}

/*
 * new_conn_attempt
 *
 * This function exercises and demos all the functionality of the state library
 *
 */

StateFunc (new_conn_attempt){


  struct s_pData *local_pointer;

  fd_set fds;
  struct sockaddr_in sin;
  int           alen,
                new_socket;

  struct timeval time_value;


  FD_ZERO(&fds);

  local_pointer = (struct s_pData *)fData;
  new_socket=local_pointer->socket;
  FD_SET(new_socket, &fds);

  memset ((char *)&time_value, 0, sizeof(time_value));
  time_value.tv_sec  = 1;
  time_value.tv_usec = 0;


  if(select(local_pointer->nfds, &fds, (fd_set *)0, (fd_set *)0, &time_value) == ERROR) {
    
    printf("FATAL ERROR : select failed against primary socket.\n");
    local_pointer->Running=FALSE;
    NextState(NULL);
  }

  if (FD_ISSET(local_pointer->socket, &fds)){
    alen=sizeof(sin);
    if( ( new_socket = accept(local_pointer->socket, &sin, &alen)) == ERROR) {
      printf("FATAL ERROR : accept failed against primary socket.\n");
      local_pointer->Running=FALSE;
      NextState(NULL);
    }

    write(new_socket, "Thanks for playing!!!\n", sizeof("Thanks for playing!!!\n"));
    close (new_socket);

    printf("A\n");
    NextState(new_conn_attempt);
    

  }

    printf("B\n");
//  SetTaskSleep(local_pointer->delay*ONESECOND);
//    NextState(cleanup);
    NextState(new_conn_attempt);

}

/*
 * start_state_machine
 *
 * This function exercises and demos all the functionality of the state library
 *
 */

StateFunc (start_state_machine){

  // Open a server socket as a server
   
  struct s_pData *local_pointer;

  struct sockaddr_in sin;

  local_pointer = (struct s_pData *)fData;
  memset ((char *)&sin, 0, sizeof(sin));

  sin.sin_family = AF_INET;
  sin.sin_port   = htons(PRIMARY_TCP_PORT);

  local_pointer->socket=socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (local_pointer->socket == ERROR){
    printf("FATAL ERROR : Can't create primary socket.\n");
    local_pointer->Running=FALSE;
    NextState(NULL);
  }

  if(bind(local_pointer->socket, &sin, sizeof(sin)) == ERROR){
    printf("FATAL ERROR : Can't bind primary socket.\n");
    local_pointer->Running=FALSE;
    NextState(NULL);
  }

  if(listen(local_pointer->socket, 25) == ERROR){
    printf("FATAL ERROR : Can't listen primary socket.\n");
    local_pointer->Running=FALSE;
    NextState(NULL);
  }

  local_pointer->connections=-1;
  local_pointer->nfds=getdtablesize();
  fcntl(local_pointer->socket, F_SETFL, O_NONBLOCK);

  printf("Begining the state machine\n");

//  SetTaskSleep(local_pointer->delay*ONESECOND);
  NextState(new_conn_attempt);
}


/*
 * OnInitialize
 *
 * This function exercises and demos all the functionality of the state library
 *
 */

int OnInitialize(){

     pData.delay=2;
     pData.task = ActivateFunc(start_state_machine, (RefPtr)&pData, NULL, NULL);
//     SetTaskDebugNameFor(pData.task, "testharness");

}

/***********************************************************************************
* 
* state.c state.h
*
* This function exercises and demos all the functionality of the state library
*
***********************************************************************************/

void state(){

     initStateEngine();

     OnInitialize();

     pData.Running=TRUE;
     while (pData.Running){
       DoNextTask();
     }
     DoTaskWith((RefPtr)kTerminate, pData.task);
     EndAllTasks();

}

SchedTest(){


 printf ("State Run\n");
  state ();
	printf("\n");
}

   # endif
