#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/stat.h>   
#include <sys/time.h>      
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

/* 
    Do whatever it takes to become a deamon process

    A deamon will not be attached to a terminal or a directory and will log it's results to a logfile or to syslog.

    This means that the process will detach itself from the terminal it is started in and continue running even after that user logs out.

*/


void become_deamon () {

   int i, max_files, fd;
   //notify(__FILE__, __LINE__, "Beginning become_deamon");
   /* close all open file descriptors */
   max_files=getdtablesize();
   for (i = 0; i < max_files; i++)
      close(i);

   /* change to log directory */
   /* chdir(conf.defaults.logdir); */
   i = chdir("/");

   /* reset the file access creation mask */
   umask(0);

   /* ignore terminal i/o signals */
   #ifdef SIGTTOU
      signal(SIGTTOU, SIG_IGN);
   #endif
   #ifdef SIGTTIN
      signal(SIGTTIN, SIG_IGN);
   #endif
   #ifdef SIGTSTP
      signal(SIGTSTP, SIG_IGN);
   #endif

   /* run in the background */
   /* disassociate from process group */
   setpgrp();
   /* disassociate from control terminal */
   if ( fork() != 0)
      exit(0); /* parent process */
   setpgrp();
   if ( fork() != 0)
      exit(0);  /* parent process */
   if ( (fd = open("/dev/tty", O_RDWR)) >= 0) {
      ioctl(fd, TIOCNOTTY, (char *) 0); /* lose control tty */
      close(fd);
   }
   /* don't reacquire a control terminal */
}  /* end become_deamon */

