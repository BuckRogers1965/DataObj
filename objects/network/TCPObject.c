/*****************************************************************/
// Module:  TCPObject.c

// include compiler libraries

#include "public.h"
#include "panelglu.h"
#include "dynstr.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define MAX_HOST_NAME 1024

#ifdef V_WINSYS

#include <io.h>
#include <winsock.h>

#endif

#if defined V_LINSYS || defined V_MACSYS

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

#define SOCKET_ERROR -1
#define closesocket close

#endif

#ifdef V_WINSYS
#include <openssl/ssl.h>
#include <openssl/rsa.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#else
//#include <ssl.h>
//#include <rsa.h>
//#include <crypto.h>
//#include <x509.h>
//#include <pem.h>
//#include <err.h>
#endif

#include "dynstr.h"
#include "wg10.h"

#define TCP_BUFFER_SIZE 16384

#define NUM_POLL_ENTRIES 8

#include "TCPObject.h"

/*****************************************************************/
// local #define macros
/*****************************************************************/

/*****************************************************************/
// local struct definitions
/*****************************************************************/


typedef struct tagTCPObj {
	VOBJ	obj;	// or the appropriate superclass struct
					// e.g. use a ViewCtxt to subclass "User View"
                    
    void * pRing;
    
	LONG	local_port,
		remote_port;
        
    int		sockfd;
	int		serverfd;

	char	remote_host[MAX_HOST_NAME];
    
	int		state;
	
    int		mode;
	
	int		CurrentConnection;
	
	int		ConnectionCount;

	int		connection_map[MAX_CONNECTS];
	
//	pDynstr SendBuffer;

	struct sockaddr_in      server_addr,
				client_addr;

    LPVOBJ	owner;
    LONG	msgID;

	// local instance data goes here
	} TCPObj, *pTCPObj;


/*****************************************************************/
// local (not exported) function prototypes
/*****************************************************************/

MsgFunc(ObjectMessageFunc);
MsgFunc(ObjectClassMsgFunc);
ST_FUNC VNOS_CALLBACK tcpClientEvents(RefPtr, RefPtr);
ST_FUNC VNOS_CALLBACK tcpServerEvents(RefPtr, RefPtr);
ST_FUNC VNOS_CALLBACK tcpConnectingEvents(RefPtr, RefPtr);
void SetNonBlocking(unsigned int);
void SetNotInheritable(unsigned int);

/*****************************************************************/
// "private" globals
/*****************************************************************/

enum {OFF, ON};

LPVCLASS pTcpClass = NULL;

TASK tcpClientTask = 0;
TASK tcpServerTask = 0;
TASK tcpConnectingTask = 0;

short tcpNumActiveClients = 0;
short tcpNumActiveServers = 0;
short tcpNumActiveConnecting = 0;

pTCPObj tcpClientRing = NULL;
pTCPObj tcpServerRing = NULL;
pTCPObj tcpConnectingRing = NULL;

void tcpClientRegister(pTCPObj);
void tcpClientUnregister(pTCPObj);
void tcpServerRegister(pTCPObj);
void tcpServerUnregister(pTCPObj);
void tcpConnectingRegister(pTCPObj);
void tcpConnectingUnregister(pTCPObj);

int tcppnfds;					/* the number of file descriptors allowed on the system */

fd_set tcpp_r_fds,				/* file descriptor set to see who has stuff to read from the socket */
 tcpp_server_r_fds,				/* file descriptor set to see if a server has a connection to accept */
 tcpp_w_fds,					/* file descriptor set to see when a connection has finally connected */
 tcpp_e_fds,					/* file descriptor set to see when a connection has an error */
 
 tcpp_a_r_fds,					/* active read file descriptor set */
 tcpp_a_server_r_fds,			/* active read file descriptor set */
 tcpp_a_w_fds,					/* active write file descriptor set */
 tcpp_a_e_fds;					/* active error file descriptor set */

/*****************************************************************/
/*****************************************************************/
/****************		Tcp Message Func	   ******************/
/*****************************************************************/

#if defined( V_WINSYS) && defined( PANEL_SIDE )
__declspec(dllexport) MsgFunc(ObjectMessageFunc)
#else
MsgFunc(ObjectMessageFunc)
#endif
{
	pTCPObj dev = (pTCPObj)obj;  // convenience assignment
										   // saves having to cast all the time
										   
	int result;

	// parse the class of message
	if (Class(obj) == vnos->classes)
		return ObjectClassMsgFunc(obj, msg, wParam, lParam);

   // parse instance messages
	switch (msg) {

	  case INITIALIZE_MSG:   // lParam = the vnos object that owns the tcp object
							 // wParam = the control in the vnos object to send messages to.
		// new instance just created

		// initialize superclass instance data first
		DefaultMessage(pTcpClass, obj, msg, wParam, lParam);
 
		// now do our local instance initializations
		dev->local_port=0;
		dev->remote_port=0;
		dev->remote_host[0]=0;
		dev->state=0;
		dev->serverfd=0;

		dev->owner = (LPVOBJ) lParam;
		dev->msgID = (LONG) wParam;
		
		bzero((char *) &dev->server_addr, sizeof(dev->server_addr));
		bzero((char *) &dev->client_addr, sizeof(dev->client_addr));

		dev->server_addr.sin_family		= AF_INET;
		dev->client_addr.sin_family		= AF_INET;
		  
		// return our Self, or 0 if initialization failed
		return (LONG)obj;
		 
	case DESTROY_MSG:
		  // release any instance resources here
		  
		tcpClientUnregister(dev);
		tcpServerUnregister(dev);
		tcpConnectingUnregister(dev);

		shutdown(dev->sockfd, 2);
		  // finally return the default processing.
		  // NB: obj is not valid after DefaultMessage returns this time
		return DefaultMessage(pTcpClass, obj, msg, wParam, lParam);
		
	case TCP_SEND_DATA_MSG: {
	
		// First check to see if we are connected and send back false if not, or if the send fails.
		if (dev->state == OFF)
			return FALSE;
		char * string= (char *) lParam;
		result = sendto(dev->sockfd, string, wParam, 0, (struct sockaddr *)&dev->client_addr, sizeof(dev->client_addr));
		if (result == -1)
			return FALSE;
		return TRUE;
		}
	
	case TCP_START_MSG:
	
		if (dev->state == ON)
			return FALSE;

		dev->server_addr.sin_addr.s_addr	= htonl(INADDR_ANY);

		if ((dev->sockfd=socket(PF_INET, SOCK_STREAM, IPPROTO_TCP))<0)
			return FALSE;

		if (bind (dev->sockfd, (struct sockaddr *) &dev->server_addr, sizeof(dev->server_addr)) < 0){
			closesocket (dev->sockfd);
			return FALSE;
		}
			
 		//SetNonBlocking(dev->sockfd);
		//SetNotInheritable(dev->sockfd);
		if (dev->mode == TCP_CLIENT) {
			if (connect (dev->sockfd, (struct sockaddr *) &dev->server_addr, sizeof(dev->server_addr)) < 0)
			{
#if defined V_LINSYS || defined V_MACSYS
//				switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppPortIdling", "connect", errno)) {
				switch (errno) {
				
				case EINPROGRESS:
#else
				switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppPortIdling", "connect", WSAGetLastError())) {

				case WSAEWOULDBLOCK:
#endif
					// once we allow this to be non blocking, we will need to check for this in it's own loop 
					// this is expected and normal
					// Now we have to add the socket to the writable entry of the file descriptor check
//					FD_SET(pData->open, &tcpp_a_w_fds);
					tcpConnectingRegister(dev);
					return CONNECTION_PENDING;
					break;
					
				default:
					// the connection failed, continue.
					closesocket (dev->sockfd);
					return FALSE;
				}		// switch			  
			}		// if
			
			SetNotInheritable(dev->sockfd);			
			SetNonBlocking(dev->sockfd);
			
			// the following two lines allow the local port selected to be discovered.
			result = sizeof (dev->server_addr);
			getsockname(dev->sockfd, (struct sockaddr *) &dev->server_addr, &result) ;
			
		    dev->serverfd = 0;
			
			tcpClientRegister(dev);

		} else {
			if ((listen(dev->sockfd, 25)) == SOCKET_ERROR)
			{
#if defined V_LINSYS || defined V_MACSYS
				//tcppPrintError(pData, __FILE__, __LINE__, "tcppPortIdling", "listen", errno);
#else
				//tcppPrintError(pData, __FILE__, __LINE__, "tcppPortIdling", "listen", WSAGetLastError());
#endif
				closesocket (dev->sockfd);
				return FALSE;
			}					// if
			
			SetNotInheritable(dev->sockfd);			
			SetNonBlocking(dev->sockfd);
			
			dev->serverfd = dev->sockfd;
			dev->sockfd = 0;
			
			tcpServerRegister(dev);
		}

		dev->state = ON;
		return TRUE;
				
	case TCP_STOP_MSG:
	
		if (dev->state == OFF)
			return FALSE;
	
		close(dev->sockfd);
		tcpClientUnregister(dev);
		tcpServerUnregister(dev);
		tcpConnectingUnregister(dev);
	dev->state = OFF;
		return TRUE;
		
	case TCP_CLOSE_CONNECTION_MSG:
		return TRUE;

	case SETVARIABLE_MSG:
		switch( wParam ) {
		case TCP_REMOTE_HOST_VAR:
		{
			
			char * HostName = (char *)lParam;
			
			if (isalpha(HostName[0]))
			{
				struct hostent *host;
			
                /*  Get the ip number from the hostname */
                if ((host = gethostbyname(HostName)) == NULL)
                {
					return FALSE;
                }                               // if by name

                /* copy network order ip number to sinaddr data structure */
                memcpy(&dev->client_addr.sin_addr.s_addr, host->h_addr_list[0], host->h_length);

			} else if (isdigit(HostName[0])) {

                /* if the hostname begins with a digit, then it is an IP number in dot code */
                dev->client_addr.sin_addr.s_addr = inet_addr(HostName);

			} else {
                /* otherwise we did not resolve the hostname. */
                return FALSE;
			}

			return TRUE;
		}
			
		case TCP_REMOTE_PORT_VAR:
			if (lParam < 0 || lParam > 65535)
				return FALSE;
			dev->client_addr.sin_port = htons(lParam);
			return TRUE;
			
		case TCP_LOCAL_PORT_VAR:
 			if (lParam < 0 || lParam > 65535)
				return FALSE;
			dev->server_addr.sin_port = htons(lParam);
			return TRUE;
			
		case TCP_CONNECTION_MODE_VAR:	
 			if (!dev->state || lParam < TCP_CLIENT || lParam > TCP_SERVER_MULTI_CONNECTS)
				return FALSE;
			dev->mode = lParam;
			return TRUE;
			
		case TCP_CURRENT_CONNECTION_VAR:
 			if (dev->state || lParam < 1 ||  lParam > MAX_CONNECTS || -1 == dev->connection_map[lParam] )
				return FALSE;
			dev->CurrentConnection = lParam;
			return TRUE;
			
		default:
			return DefaultMessage(vnos->netdevs, obj, msg, wParam, lParam);
		}

	  case GETVARIABLE_MSG:
		switch( wParam ) {
		case TCP_REMOTE_HOST_VAR:
			return (int)inet_ntoa(dev->client_addr.sin_addr);

		case TCP_REMOTE_PORT_VAR:
			return ntohs(dev->client_addr.sin_port);

		case TCP_LOCAL_PORT_VAR:
			return ntohs(dev->client_addr.sin_port);

		case TCP_CONNECTION_MODE_VAR:	
 			return (dev->mode);

		case TCP_CURRENT_CONNECTION_VAR:	
 			return (dev->CurrentConnection);

		case TCP_CONNECTION_COUNT_VAR:	
 			return (dev->ConnectionCount);

		default:
			return DefaultMessage(vnos->netdevs, obj, msg, wParam, lParam);
		}
		
	  default:
		 return DefaultMessage(pTcpClass, obj, msg, wParam, lParam);
	  }
   return 0L;
}

/*****************************************************************/
/*************	Tcp Class Message Func	******************/
/*****************************************************************/
MsgFunc(ObjectClassMsgFunc)
{
	// parse class messages
	switch (msg) {
		case INITCLASS_MSG:
			// Called when this Class is initially installed
			// do not create any instances at this time - wait for STARTUP_MSG

			pTcpClass = (LPVCLASS)obj;

			// first: InitVnosLib MUST be called like this
			// it establishes all the glue we need
			InitVnosLib(pTcpClass, wParam, lParam);

			// now fill in significant class information
			// check the definition of OIMDATA for others you may need
			// these are required
			pTcpClass->data.version = V_VERSION;
			pTcpClass->clsdata->dbHdrSize = 0; // size of custom struct in hDb
			pTcpClass->clsdata->dbRecSize = sizeof(TCPObj); // or your own larger struct
			lstrcpy(pTcpClass->clsdata->szMnfName, "Singlestep Technologies");   // Manufacturer's Name
			lstrcpy(pTcpClass->clsdata->szModName, "TCP");   // Device Model Name
			lstrcpy(pTcpClass->clsdata->szModNum, "1.0");   // Device Model Number
			
			return INITCLASS_OK; // FALSE would indicate an unsuccessful load

		case STARTUP_MSG:
			// called after installation of all dropins is complete
			// this call gives us our first opportunity to do work
			
			// If we want to make ourselves subclass to something other
			// than Object, do this (sub the relevant name string):
			// ChangeSuperclass(GetNamedClass("The Parent Class"));

			// do this to ensure implicit dependancies are served
//			StartSuper(pTcpClass);
			// code beyond this point only executed the first time this
			// message is received

			// This is the place to create any initial instances we need
			// If there are any other Classes we depend upon explicitly,
			// then test their presence using this syntax
			//  (sub the relevant name string):
			// StartUp(GetNamedClass("The Class"));

			// now acquire session level resources if needed

#if defined V_LINSYS || defined V_MACSYS
	{
	// setup the signal handler data structures
	struct sigaction handle;
	handle.sa_flags = 0;

	// ignore the SIGPIPE signal
	handle.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &handle, NULL);
	}
	
	/* get the maximum number of allowed file descriptors */
	tcppnfds = getdtablesize();

#else
	/* initialize the windows socket code */
{
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;
	
	tcppnfds = 0;
	wVersionRequested = MAKEWORD(2, 2);
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		/* Tell the user that we could not find a usable WinSock DLL. */
	}
}
#endif

//	SSL_load_error_strings();
//	SSLeay_add_ssl_algorithms();

	/* clear the file descriptor sets */
	FD_ZERO(&tcpp_a_r_fds);
	FD_ZERO(&tcpp_a_server_r_fds);
	FD_ZERO(&tcpp_server_r_fds);
	FD_ZERO(&tcpp_a_w_fds);
	FD_ZERO(&tcpp_a_e_fds);
	FD_ZERO(&tcpp_r_fds);
	FD_ZERO(&tcpp_w_fds);
	FD_ZERO(&tcpp_e_fds);


			tcpClientTask = ActivateFunc(tcpClientEvents, NULL, NULL, NULL);
			tcpServerTask = ActivateFunc(tcpServerEvents, NULL, NULL, NULL);
			tcpConnectingTask = ActivateFunc(tcpConnectingEvents, NULL, NULL, NULL);
			
			// finally done, return FALSE.
			return FALSE;

		case END_MSG:
			// called as the last message when this Class is unloaded
			// release any session class level resources here
			if (tcpClientTask) {
				DoTaskWith((RefPtr) kTerminate, tcpClientTask);
				DoTaskWith((RefPtr) kTerminate, tcpServerTask);
				DoTaskWith((RefPtr) kTerminate, tcpConnectingTask);
				}

			break;

		default:
			return DefaultMessage(vnos->classes, obj, msg, wParam, lParam);
	}
	return 0L;
}

/****************************************************************/
ST_FUNC VNOS_CALLBACK tcpClientEvents(RefPtr pDirect, RefPtr fData)
{


	pTCPObj dev = tcpClientRing;
	int count;
	struct timeval tv;
	
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (pDirect == (RefPtr) kTerminate)
		NextState(NULL);

	if (pDirect == (RefPtr) kActivate)
		SetTaskSleep(ST_WAKE);

	/* if there are no active sockets sleep */
	if (NULL == dev) {
		SetTaskSleep(ST_SLEEP);
		NextState(tcpClientEvents);
	}

	/* copy the active file descriptor set to the working file descriptor set */
	memcpy((char *) &tcpp_r_fds, (char *) &tcpp_a_r_fds, sizeof(tcpp_r_fds));

	/* look for a new connection */
	if ((count = select(tcppnfds, &tcpp_r_fds, (fd_set *)0, (fd_set *)0, &tv)) == SOCKET_ERROR)
	{
//#if defined V_LINSYS || defined V_MACSYS
//				tcppPrintError(pData, __FILE__, __LINE__, "tcppPollEvents", "select", errno);
//#else
//				tcppPrintError(pData, __FILE__, __LINE__, "tcppPollEvents", "select", WSAGetLastError());
//#endif
			SetTaskSleep(ST_SLEEP);
			NextState(tcpClientEvents);
	}							// if select

	/* if there is nothing waiting to be processed */
	if (count == 0) {
		SetTaskSleep(1 * ONESECOND / 100);
		NextState(tcpClientEvents);
	}
	// remember the begining of the ring
	tcpClientRing = dev;

	// process ring entries until we get to the first ring entry again
	while (dev != NULL )
	{
		if (FD_ISSET(dev->sockfd, &tcpp_r_fds))
		{
			int bytesReceived;
			static char tcpBuffer[TCP_BUFFER_SIZE];
	
			bytesReceived = recv(dev->sockfd, tcpBuffer, TCP_BUFFER_SIZE - 1, 0);

			SendOMessage(dev->owner,  dev->msgID+TCP_RECEIVED_DATA_CALLBACK, bytesReceived, (LONG)tcpBuffer);
		}
		
		// stop once we have gotten to the front of the ring again, or the ring disapears.
		if (dev->pRing == tcpClientRing  || dev == NULL)
			break;
			
		dev = dev->pRing;
	}

	if (!tcpNumActiveClients)
		SetTaskSleep(ST_SLEEP);

	NextState(tcpClientEvents);

}

/****************************************************************/
ST_FUNC VNOS_CALLBACK tcpServerEvents(RefPtr pDirect, RefPtr fData)
{
	pTCPObj dev = tcpServerRing;
	int count;
	struct timeval tv;
	
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (pDirect == (RefPtr) kTerminate)
		NextState(NULL);

	if (pDirect == (RefPtr) kActivate)
		SetTaskSleep(ST_WAKE);

	/* if there are no active sockets sleep */
	if (NULL == dev) {
		SetTaskSleep(ST_SLEEP);
		NextState(tcpServerEvents);
	}

	/* copy the active file descriptor set to the working file descriptor set */
	memcpy((char *) &tcpp_server_r_fds, (char *) &tcpp_a_server_r_fds, sizeof(tcpp_server_r_fds));

	/* look for a new connection */
	if ((count = select(tcppnfds, &tcpp_server_r_fds, (fd_set *)0, (fd_set *)0, &tv)) == SOCKET_ERROR)
	{
//#if defined V_LINSYS || defined V_MACSYS
//				tcppPrintError(pData, __FILE__, __LINE__, "tcppPollEvents", "select", errno);
//#else
//				tcppPrintError(pData, __FILE__, __LINE__, "tcppPollEvents", "select", WSAGetLastError());
//#endif
			SetTaskSleep(ST_SLEEP);
			NextState(tcpServerEvents);
	}							// if select

	/* if there is nothing waiting to be processed */
	if (count == 0) {
		SetTaskSleep(1 * ONESECOND / 100);
		NextState(tcpServerEvents);
	}
	// remember the begining of the ring
	tcpServerRing = dev;

	// process ring entries until we get to the first ring entry again
	while (dev != NULL )
	{
		if ((FD_ISSET(dev->serverfd, &tcpp_server_r_fds)))
		{
			int alen = sizeof(dev->server_addr);
			if ((dev->sockfd = accept(dev->serverfd, (struct sockaddr *) &dev->server_addr, &alen)) == SOCKET_ERROR)
			{					
#if defined V_LINSYS || defined V_MACSYS
//				switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppPortListening", "accept", errno)) {
				switch (errno) {
				
				case EAGAIN:
#else
				switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppPortListening", "accept", WSAGetLastError())) {
				
				case WSAEWOULDBLOCK:
#endif
//					NextState(tcppPortListening);
					break;
					
				default:
				;
//					tcppUnregisterActiveSocket(dev);
//					closesocket(pData->sockfd);
//					NextState(tcppPortIdling);
				}
			} else {	
			
				int result;
			
				// tell the owner that there is a new connection.
				SendOMessage(dev->owner,  dev->msgID+TCP_NEW_CONNECTION_CALLBACK, 0, 1);
				
				// the following two lines allow the local port selected to be discovered.
				result = sizeof (dev->server_addr);
				getsockname(dev->sockfd, (struct sockaddr *) &dev->server_addr, &result) ;

				tcpServerUnregister(dev);  // take the server off the list, for now we can only service one connection at a time.
				tcpClientRegister(dev);
				
			}  // if accept
		}
		
		// stop once we have gotten to the front of the ring again, or the ring disapears.
		if (dev->pRing == tcpConnectingRing  || dev == NULL)
			break;
			
		dev = dev->pRing;
	}

	if (!tcpNumActiveServers)
		SetTaskSleep(ST_SLEEP);

	NextState(tcpServerEvents);
    
}

/***************************************************************
tcpConnectingEvents will look for opening connections to either
open or time out.

If the connection opens, then a callback is made to the owner of the
TCPObject telling it that the connection was made and which connection that was.

At this point, only one client connection will be open at a time, so this will always be 1.
But that will change in future revisions of the TCPObject.

The connection will also need to be removed from the tcpConnectingRing and added to the tcpClientRing.

if the connection times out, then a callback is made to the owner of the
TCPObject telling it that the connection closed and which connection that was.

Also need to figure out how to do the time out... 

Thinking that getting a 64bit timestamp from our time code and having a user configurable timeout would be good.

*/
ST_FUNC VNOS_CALLBACK tcpConnectingEvents(RefPtr pDirect, RefPtr fData)
{


	pTCPObj dev = tcpConnectingRing;
	int count;
	struct timeval tv;
	
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (pDirect == (RefPtr) kTerminate)
		NextState(NULL);

	if (pDirect == (RefPtr) kActivate)
		SetTaskSleep(ST_WAKE);

	/* if there are no active sockets sleep */
	if (NULL == dev) {
		SetTaskSleep(ST_SLEEP);
		NextState(tcpConnectingEvents);
	}

	/* copy the active file descriptor set to the working file descriptor set */
	memcpy((char *) &tcpp_w_fds, (char *) &tcpp_a_w_fds, sizeof(tcpp_w_fds));

	/* look for a new connection */
	if ((count = select(tcppnfds, (fd_set *)0, &tcpp_w_fds, (fd_set *)0, &tv)) == SOCKET_ERROR)
	{
//#if defined V_LINSYS || defined V_MACSYS
//				tcppPrintError(pData, __FILE__, __LINE__, "tcppPollEvents", "select", errno);
//#else
//				tcppPrintError(pData, __FILE__, __LINE__, "tcppPollEvents", "select", WSAGetLastError());
//#endif
			SetTaskSleep(ST_SLEEP);
			NextState(tcpConnectingEvents);
	}							// if select

	/* if there is nothing waiting to be processed */
	if (count == 0) {
		SetTaskSleep(1 * ONESECOND / 100);
		NextState(tcpConnectingEvents);
	}
	// remember the begining of the ring
	tcpConnectingRing = dev;

	// process ring entries until we get to the first ring entry again
	while (dev != NULL )
	{
		if ((FD_ISSET(dev->sockfd, &tcpp_w_fds)))
		{
		
			SendOMessage(dev->owner,  dev->msgID+TCP_NEW_CONNECTION_CALLBACK, 0, 1);
			tcpConnectingUnregister(dev);
			tcpClientRegister(dev);
		}
		
		// stop once we have gotten to the front of the ring again, or the ring disapears.
		if (dev->pRing == tcpConnectingRing  || dev == NULL)
			break;
			
		dev = dev->pRing;
	}

	if (!tcpNumActiveConnecting)
		SetTaskSleep(ST_SLEEP);

	NextState(tcpConnectingEvents);

}


/*****************************************************************/
void tcpConnectingRegister(pTCPObj dev){

	if (dev->pRing)
	{
//		tcppDisplayStrNum(pData, Exception, "tcppRegisterActiveSocket: object already in ring", 0);
		return;
	}

	tcpNumActiveConnecting++;

	if (NULL == tcpConnectingRing)
		dev->pRing = dev;
	else
	{
		dev->pRing = tcpConnectingRing->pRing;
		tcpConnectingRing->pRing = dev;
	}
	tcpServerRing = dev;
	FD_SET(dev->sockfd, &tcpp_a_w_fds);
	
	if (tcpNumActiveConnecting == 1)
		SetTaskSleepFor(tcpConnectingTask, ST_WAKE);

}

/*****************************************************************/
void tcpConnectingUnregister(pTCPObj dev){

	pTCPObj pRing = tcpConnectingRing;

	if (!dev->pRing) {		// this condition is OK - may be called from disable as well as idling
		return;
	}

	if (!pRing) {
//		tcppDisplayStrNum(pData, Exception, "tcppUnregisterActiveSocket: ring was empty", 0);
		return;
	}

	while (pRing->pRing != dev) {
		pRing = pRing->pRing;
		if (pRing == tcpConnectingRing)
			break;
	}

	if (pRing->pRing != dev)
	{
//		tcppDisplayStrNum(pData, Exception, "tcppUnregisterActiveSocket: object not in ring", 0);
		return;
	}
	
	pRing->pRing = dev->pRing;
	if (pRing == dev)
	{
		pRing = NULL;
		tcpConnectingRing = NULL;
	}
	else
	{
		if (tcpConnectingRing == dev)
			tcpConnectingRing = dev->pRing;
	}

	dev->pRing = NULL;
	FD_CLR(dev->sockfd, &tcpp_a_w_fds);

	if (tcpNumActiveConnecting > 0)
		tcpNumActiveConnecting--;
        
}

/*****************************************************************/
void tcpServerRegister(pTCPObj dev){

	if (dev->pRing)
	{
//		tcppDisplayStrNum(pData, Exception, "tcppRegisterActiveSocket: object already in ring", 0);
		return;
	}

	tcpNumActiveServers++;

	if (NULL == tcpServerRing)
		dev->pRing = dev;
	else
	{
		dev->pRing = tcpServerRing->pRing;
		tcpServerRing->pRing = dev;
	}
	tcpServerRing = dev;
	FD_SET(dev->serverfd, &tcpp_a_server_r_fds);
	
	if (tcpNumActiveServers == 1)
		SetTaskSleepFor(tcpServerTask, ST_WAKE);

}

/*****************************************************************/
void tcpServerUnregister(pTCPObj dev){

	pTCPObj pRing = tcpServerRing;

	if (!dev->pRing) {		// this condition is OK - may be called from disable as well as idling
		return;
	}

	if (!pRing) {
//		tcppDisplayStrNum(pData, Exception, "tcppUnregisterActiveSocket: ring was empty", 0);
		return;
	}

	while (pRing->pRing != dev) {
		pRing = pRing->pRing;
		if (pRing == tcpServerRing)
			break;
	}

	if (pRing->pRing != dev)
	{
//		tcppDisplayStrNum(pData, Exception, "tcppUnregisterActiveSocket: object not in ring", 0);
		return;
	}
	
	pRing->pRing = dev->pRing;
	if (pRing == dev)
	{
		pRing = NULL;
		tcpServerRing = NULL;
	}
	else
	{
		if (tcpServerRing == dev)
			tcpServerRing = dev->pRing;
	}

	dev->pRing = NULL;
	FD_CLR(dev->serverfd, &tcpp_a_server_r_fds);

	if (tcpNumActiveServers > 0)
		tcpNumActiveServers--;
        
}


/*****************************************************************/
void tcpClientRegister(pTCPObj dev){

	if (dev->pRing)
	{
//		tcppDisplayStrNum(pData, Exception, "tcppRegisterActiveSocket: object already in ring", 0);
		return;
	}

	tcpNumActiveClients++;

	if (NULL == tcpClientRing)
		dev->pRing = dev;
	else
	{
		dev->pRing = tcpClientRing->pRing;
		tcpClientRing->pRing = dev;
	}
	tcpClientRing = dev;
	FD_SET(dev->sockfd, &tcpp_a_r_fds);
	
	if (tcpNumActiveClients == 1)
		SetTaskSleepFor(tcpClientTask, ST_WAKE);

}

/*****************************************************************/
void tcpClientUnregister(pTCPObj dev){

	pTCPObj pRing = tcpClientRing;

	if (!dev->pRing) {		// this condition is OK - may be called from disable as well as idling
		return;
	}

	if (!pRing) {
//		tcppDisplayStrNum(pData, Exception, "tcppUnregisterActiveSocket: ring was empty", 0);
		return;
	}

	while (pRing->pRing != dev) {
		pRing = pRing->pRing;
		if (pRing == tcpClientRing)
			break;
	}

	if (pRing->pRing != dev)
	{
//		tcppDisplayStrNum(pData, Exception, "tcppUnregisterActiveSocket: object not in ring", 0);
		return;
	}
	
	pRing->pRing = dev->pRing;
	if (pRing == dev)
	{
		pRing = NULL;
		tcpClientRing = NULL;
	}
	else
	{
		if (tcpClientRing == dev)
			tcpClientRing = dev->pRing;
	}

	dev->pRing = NULL;
	FD_CLR(dev->sockfd, &tcpp_a_r_fds);

	if (tcpNumActiveClients > 0)
		tcpNumActiveClients--;
        
}


/*****************************************************/
void SetNonBlocking(unsigned int socket)
{   
#if defined V_LINSYS || defined V_MACSYS
	fcntl(socket, F_SETFL, O_NONBLOCK);
#else   
	long argp = 1;
	ioctlsocket(socket, FIONBIO, &argp);
#endif  
}	   

/*****************************************************/

void SetNotInheritable(unsigned int socket)
{
#if defined V_LINSYS || defined V_MACSYS
	fcntl(socket, F_SETFD, 1);
#else
	SetHandleInformation(
		(HANDLE) socket,  // handle to object
		HANDLE_FLAG_INHERIT,
		0
		);
#endif
}


/*****************************************************************/




#pragma mark

/****************************************************************/
void tcppProcessRx(PDEV_DATA pData)
{
	int bytesReceived;
	static int retry_errors = 0;
	pDynStr vstr = NULL;
	
	pData->already_active = TRUE;
	
	if (NumberAt(pData, sslStatus)==LEDon){
		if(pData->meth <= 0 || pData->ctx <= 0 || pData->ssl <= 0 )
			return;
		bytesReceived = SSL_read (pData->ssl, tcp_buffer, TCP_BUFFER_SIZE - 1);
	}
	else
		bytesReceived = recv(pData->open, tcp_buffer, TCP_BUFFER_SIZE - 1, 0);

	if (bytesReceived  > 0) {
		 
		retry_errors=0;
		tcp_buffer[bytesReceived] = '\0';
		tcppDisplayStrNum (pData, TraceRx, tcp_buffer, bytesReceived);

		//* if the binary is off and the crlf is on 
		if (!NumberAt(pData, OptBinaryRx) && NumberAt(pData, OptCrLf2Cr))
			DynStrNormalizeLineEnds(&vstr, tcp_buffer, ApiFileLf, &pData->rxLastLineEnd);
		else
			DynStrCopyN(&vstr, tcp_buffer, bytesReceived);

		if (NumberAt(pData, OptAccumulateRx))
		{
			if (NumberAt(pData, OptBinaryRx))
			{
				BYTE *newData;
				size_t newSize;
				pComplexEnvelope	 pValue;

				pValue = (pComplexEnvelope)GetObjectValue(pData->pDev, GlobalId(PropRxData), SB_COMPLEX);
				newSize = pValue->cxSize + bytesReceived;
				newData = malloc(newSize + 1);
				if (newData)
				{
					if (pValue->cxSize)
						memcpy(newData, pValue->cxValue, pValue->cxSize);
					memcpy(&newData[pValue->cxSize], (void*)(tcp_buffer), bytesReceived);
					newData[newSize] = 0;
					ChangeComplexValue (PropRxData, newData, newSize);
					free(newData);
				}
				else
				{
//							  TraceErrf("TCP Port Rx: out of RAM!", NULL, 0, NULL);
				}
			}
			else
			{
				if (vstr && vstr->data)
					ChangeStringValueAppend(PropRxData, vstr->data);
			}
		}
		else
		{
			if(bytesReceived > 0)
			{
				ChangeNumericValue(DspButesReady, 0);
				if (NumberAt(pData, OptBinaryRx))
				{
					ChangeComplexValue(PropRxData, tcp_buffer, bytesReceived);
				}
				else
				{
					if (vstr && vstr->data)
						ChangeStringValue(PropRxData, vstr->data);
				}
			}
		}
		DestroyDynStr(&vstr);
		tcppDisplayStrNum (pData, Progress, "tcppProcessRx: Received %d bytes", bytesReceived);
		bytesReceived = bytesReceived + NumberAt(pData, DspButesReady);
		ChangeNumericValue(DspButesReady, bytesReceived);
		ChangeNumericValue(DspRxReady, LEDon);
		pData->bRxContinueAwake = FALSE;	// come around for more data next time
	}
	else
	{
		//* do error recovery here 
		if (bytesReceived = SOCKET_ERROR)
		{
			pData->bRxContinueAwake = FALSE;
#if defined V_LINSYS || defined V_MACSYS
			switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppProcessRx", "Recv", errno)) {
				
			case EAGAIN:
#else
			switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppProcessRx", "Recv", WSAGetLastError())) {
				
			case WSAEWOULDBLOCK:
#endif
				retry_errors++;
				break;
					
			default:
				tcppDisplayStrNum(pData, Exception, "tcppProcessRx: Closing connection.", 0);
				pData->CloseReason = TheyCloseClean;
			}
		
			if (retry_errors > 10)
			{
				pData->CloseReason = TheyCloseClean;
				retry_errors = 0;
			}
		}
		else
			pData->CloseReason = TheyCloseClean;
	}
}

/****************************************************************/
size_t tcppTransferTxData(PDEV_DATA pData)
{
	size_t	bytesToSend;
	LPSTR	 pValue;

	bytesToSend = pData->TxBuffer ? pData->TxBuffer->size : 0;
	
	if (!pData->TxBuffer)
		DynStrCat(&pData->TxBuffer, "");

	if (NULL != (pValue = (LPSTR)GetObjectValue(
			pData->pDev,
			GlobalId(PropTxData),
			GetObjectValueCode(pData->pDev, GlobalId(PropTxData)))))
	{
		if (NumberAt(pData, OptBinaryTx))
		{
			pComplexEnvelope pcxEnv = (pComplexEnvelope)pValue;
			if (pcxEnv->cxSize)
				DynStrCatN(&pData->TxBuffer, pcxEnv->cxValue, pcxEnv->cxSize);
		}
		else
		{
		
			// DynStrCat(&pData->TxBuffer, pValue);
			
			// test for an empty case.
			if (NumberAt (pData, OptCr2Crlf) )
				DynStrNormalizeLineEnds(&pData->TxBuffer, pValue, "\r\n", &pData->txLastLineEnd);
			else
				DynStrCat(&pData->TxBuffer, pValue);
		}
		bytesToSend = pData->TxBuffer ? pData->TxBuffer->size : 0;

		if (NumberAt(pData, OptClrAsSent))
			ChangeNumericValue(CmdClearData, 1);
	}
	
	if (bytesToSend > 0)
		pData->bTxContinueAwake = TRUE;

	return bytesToSend;
}

/****************************************************************/
BOOL tcppProcessTx(PDEV_DATA pData)
{
	size_t bytesToSend;
	int bytesSent = 0;
	char *s;

	bytesToSend = pData->TxBuffer ? pData->TxBuffer->size : 0;
	s=pData->TxBuffer->data;
	if (bytesToSend == 0)
	{ 
		pData->bTxContinueAwake=FALSE;
		return pData->bTxContinueAwake;
	}
	if (bytesToSend > 0)
	{
		if (bytesToSend > TCP_BUFFER_SIZE)
			bytesToSend = TCP_BUFFER_SIZE;

		if (NumberAt(pData, sslStatus)==LEDon)
			bytesSent = SSL_write (pData->ssl, pData->TxBuffer->data, bytesToSend);
		else
			bytesSent = send(pData->open, pData->TxBuffer->data, bytesToSend, 0);

		if (bytesSent > 0)
		{
			//* output the string to the debug window 
			tcppDisplayStrNum (pData, TraceTx, pData->TxBuffer->data, bytesSent);
			tcppDisplayStrNum(pData, Progress, "tcppProcessTx: Transmitted %d bytes", bytesSent);
			DynStrPreTruncate(&pData->TxBuffer, bytesSent);
		}
		else
		{
			if (bytesSent = SOCKET_ERROR)
			{
#if defined V_LINSYS || defined V_MACSYS
				switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppProcessTx", "Send", errno)) {
				
				case EAGAIN:
#else
				switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppProcessTx", "Send", WSAGetLastError())) {
				
				case WSAEWOULDBLOCK:
#endif
					break;
				default:
					tcppDisplayStrNum(pData, DebugMsgs, "tcppProcessTx: Debug, closing connection.", 0);
					pData->CloseReason = TheyCloseClean;
				}
			}
		}
	}

	return pData->bTxContinueAwake;
}

#pragma mark

/****************************************************************/
ST_FUNC VNOS_CALLBACK tcppPortListening(RefPtr pDirect, RefPtr fData)
{
	PDEV_DATA pData = (PDEV_DATA) fData;
	int alen;
	
	unsigned int accept_connection;

	SetTaskSleep(ST_SLEEP);

	// shut everything down if we've been terminated
	TerminationTest;

	// perform state entry tasks
	if (!pDirect && tcppUpdateStateDisplays(pData, Listening))
		NextState(tcppPortListening);

	// handle the accept request that was detected by the main polling routine
	if ((RefPtr) kData == pDirect || !pDirect)
	{
		if (LEDleaving == NumberAt(pData, DspListen))
		{
			pData->CloseReason = WeCloseClean;
			SetTaskSleep(ST_WAKE);
			NextState(tcppPortClosing);
		}

		alen = sizeof(pData->socket_addr);

		if ((accept_connection = accept(pData->open, (struct sockaddr *) &pData->socket_addr, &alen)) == SOCKET_ERROR)
		{					
#if defined V_LINSYS || defined V_MACSYS
				switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppPortListening", "accept", errno)) {
				
				case EAGAIN:
#else
				switch (tcppPrintError(pData, __FILE__, __LINE__, "tcppPortListening", "accept", WSAGetLastError())) {
				
				case WSAEWOULDBLOCK:
#endif
					NextState(tcppPortListening);
					break;
					
				default:
					tcppUnregisterActiveSocket(pData);
					closesocket(pData->open);
					pData->open = -1;
					pData->listen = -1;
					NextState(tcppPortIdling);
				}
		}						// if accept

		//* stop selecting on server socket 
		tcppUnregisterActiveSocket(pData);
		
		// only do this if you are using the old method.
//		closesocket(pData->open);

		//* copy the active file descriptor to the active socket slot 
		pData->open = accept_connection;

		// Put IP of who connected to us in box
		SetObjectStringValue(PropDomain, inet_ntoa(pData->socket_addr.sin_addr));

		tcppSetNonBlocking(pData->open);
		tcppSetNotInheritable(pData->open);

		// Register this Client as active
		tcppRegisterActiveSocket(pData);
		SetTaskSleep(ST_WAKE);
		NextState(tcppPortConnected);
	}

	if ((RefPtr) kActivate == pDirect || !pDirect)
	{
		if (LEDleaving == NumberAt(pData, DspListen))
		{
			pData->CloseReason = WeCloseClean;
			SetTaskSleep(ST_WAKE);
			NextState(tcppPortClosing);
		}
	}

	NextState(tcppPortListening);
}

/****************************************************************/
ST_FUNC VNOS_CALLBACK tcppPortOpening(RefPtr pDirect, RefPtr fData)
{

	PDEV_DATA pData = (PDEV_DATA) fData;
	
	pData->CloseReason=StayOpen;
	SetTaskSleep(10 * ONESECOND);

	// shut everything down if we've been terminated
	TerminationTest;

	// perform state entry tasks
	if (!pDirect && tcppUpdateStateDisplays(pData, Opening))
		NextState(tcppPortOpening);

	if (pDirect == (RefPtr) kData)
	{

		SetTaskSleep(ST_WAKE);
		FD_CLR(pData->open, &tcpp_a_w_fds);
		tcppProcessRx(pData);
	   	switch (pData->CloseReason)
		{
		
		case StayOpen:
			NextState(tcppPortConnected);
			break;
			
		default:
			tcppDisplayStrNum(pData, DebugMsgs, "tcppPortOpening: Failed to connect to remote host.", 0);
			pData->CloseReason = TheyCloseClean;
			ChangeNumericValue(DspOpen, LEDoff);
			NextState(tcppPortClosing);
		}
	}

	if (pDirect == (RefPtr) kActivate || pDirect == (RefPtr) NULL)
	{
		pData->CloseReason = WeAbort;
		FD_CLR(pData->open, &tcpp_a_w_fds);

		pData->retry_count++;
		if (pData->retry_count > 2){
			ChangeNumericValue(CmdClose, 1);
			pData->retry_count = 0;
		}

		SetTaskSleep(ST_WAKE);
		NextState(tcppPortClosing);
	}

	SetTaskSleep(ST_WAKE);
	tcppDisplayStrNum(pData, DebugMsgs, "tcppPortOpening: Connection attempt timed out.", 0);

	NextState(tcppPortClosing);
}

/****************************************************************/
ST_FUNC VNOS_CALLBACK tcppPortConnected(RefPtr pDirect, RefPtr fData)
{
	PDEV_DATA pData = (PDEV_DATA) fData;

	// shut everything down if we've been terminated
	TerminationTest;

	// perform state entry tasks
	if (!pDirect || (RefPtr) kData == pDirect)
	{		
		// set the state flag
		if (tcppUpdateStateDisplays(pData, Connected))
		{
			// send stuff if we are in autosend mode when we connect
			if (NumberAt(pData, OptAutoSend))
				// if we actually put anything into the buffer, then set Tx continue to be true
				tcppTransferTxData(pData);
  		}
	}
	
			// if ssl led is pending, attempt to start ssl
		if (NumberAt(pData, sslStatus)==LEDpending)
		{
			if (NumberAt(pData, DspOpen)==LEDon)
			{
				pData->meth = SSLv2_client_method();
				pData->ctx = SSL_CTX_new (pData->meth);
				pData->ssl = SSL_new (pData->ctx);   
				SSL_set_fd (pData->ssl, pData->open);
				SSL_connect (pData->ssl);
			}
			else if ( NumberAt(pData, DspListen) )
			{

				pData->meth = SSLv23_server_method();
				pData->ctx = SSL_CTX_new (pData->meth);
				
				if (SSL_CTX_use_certificate_file(pData->ctx, StringAt(pData, sslCert), SSL_FILETYPE_PEM) <= 0) {
//					ERR_print_errors_fp(stderr);
				}
				if (SSL_CTX_use_PrivateKey_file(pData->ctx, StringAt(pData, sslKey), SSL_FILETYPE_PEM) <= 0) {
//					ERR_print_errors_fp(stderr);
				}

				if (!SSL_CTX_check_private_key(pData->ctx)) {
//					fprintf(stderr,"Private key does not match the certificate public key\n");
				}

				pData->ssl = SSL_new (pData->ctx);   
				SSL_set_fd (pData->ssl, pData->open);
				SSL_accept (pData->ssl);
			}
			
			if(pData->meth <= 0 || pData->ctx <= 0 || pData->ssl <= 0 )
				NextState(tcppPortConnected);
			ChangeNumericValue(sslStatus, LEDon);   
		}

	pData->CloseReason = StayOpen;
	// if we've been activated see what comes next
	if ((RefPtr) kActivate == pDirect || !pDirect)
	{
		if (0 == NumberAt(pData, CmdEnable))
		{
			// emter Disabled state via closing
			pData->CloseReason = WeAbort;
			SetTaskSleep(ST_WAKE);
			NextState(tcppPortClosing);
		}

		if ((LEDpending == NumberAt(pData, DspClose))
			|| (LEDpending == NumberAt(pData, DspListen)))
		{	
			// Vnos requested orderly close
			tcppDisplayStrNum(pData, Progress, "tcppPortConnected: Connection Closed by local Host\n", 0);
			pData->CloseReason = WeCloseClean;
			SetTaskSleep(ST_WAKE);
			NextState(tcppPortClosing);
		}
	}
	
	// Process the data to be received 
	// This is caused by the select routine in the main polling state machine
	if (pData->bRxContinueAwake && NumberAt(pData, sslStatus)!=LEDpending)
		tcppProcessRx(pData);
		
	// Process the data to be transmitted
	// This is caused by clicking the send button or an autosend
	if (pData->bTxContinueAwake && NumberAt(pData, sslStatus)!=LEDpending)
		tcppProcessTx(pData);

	switch (pData->CloseReason) {
	case StayOpen:
		break;
	default:
		tcppDisplayStrNum(pData, DebugMsgs, "tcppPortConnected: Connection Closed by remote Host\n", 0);
		pData->CloseReason = TheyCloseClean;
		SetTaskSleep(ST_WAKE);
		NextState(tcppPortClosing);
	}

	if (pData->bTxContinueAwake || pData->bRxContinueAwake) {
		SetTaskSleep(ST_WAKE);
	} else {
		SetTaskSleep(ST_SLEEP);
	}

	NextState(tcppPortConnected);
}

/****************************************************************/
ST_FUNC VNOS_CALLBACK tcppPortClosing(RefPtr pDirect, RefPtr fData)
{
	PDEV_DATA pData = (PDEV_DATA) fData;

	SetTaskSleep(ST_SLEEP);

	// shut everything down if we've been terminated
	TerminationTest;

	// perform state entry tasks
	if (!pDirect || pDirect == (RefPtr) kData)
	{
		tcppUpdateStateDisplays(pData, Closing);
		ChangeNumericValue(DspClose, LEDpending); 

		if (LEDon == NumberAt(pData, DspListen))
			ChangeNumericValue(DspListen, LEDpending);

		if (LEDon == NumberAt(pData, DspOpen))
			ChangeNumericValue(DspOpen, LEDpending);

		SetTaskSleep(ST_WAKE);
		NextState(tcppPortFlushTxRx);
	}
	// if we've been activated see what comes next
	if ((RefPtr) kActivate == pDirect || !pDirect)
	{
		SetTaskSleep(ST_WAKE);
		NextState(tcppPortClosing);
	}
	
	if (pDirect > (RefPtr) kLastReason)
	{
		SetTaskSleep(ST_WAKE);
		tcppUnregisterActiveSocket(pData);
		tcppDisplayStrNum(pData, Progress, "tcppPortClosing: Connection Also Closed by remote Host", 0);
		NextState(tcppPortFlushTxRx);
	}
	
	NextState(tcppPortClosing);
}

/****************************************************************/
ST_FUNC VNOS_CALLBACK tcppPortFlushTxRx(RefPtr pDirect, RefPtr fData)
{
	PDEV_DATA pData = (PDEV_DATA) fData;

	pData->bTxContinueAwake = FALSE;
	pData->bRxContinueAwake = FALSE;
	DestroyDynStr(&pData->TxBuffer);	// Clear the Tx buffer
	tcppUnregisterActiveSocket(pData);
   
	// Handle the security ssl stuff
	if (NumberAt(pData, sslStatus)==LEDon){
		SSL_shutdown (pData->ssl);
		SSL_free (pData->ssl);
		SSL_CTX_free (pData->ctx);
	}
	if (NumberAt(pData, sslEnable)==1)
		ChangeNumericValue(sslStatus, LEDpending);
	else
		ChangeNumericValue(sslStatus, LEDoff);

	pData->rxLastLineEnd = FALSE;
	pData->txLastLineEnd = FALSE;

	closesocket(pData->open);
	SetTaskSleep(ST_WAKE);
	NextState(tcppPortIdling);
}

#pragma mark

