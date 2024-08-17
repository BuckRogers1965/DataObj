// Module:  TCPObject.h
// Purpose: header for a generic VNOS class
// Author: James M. Rogers 01 May 2003
// Copyright (c) 2003 by Singlestep Technologies

#define MAX_MSG_SIZE 65535
#define MAX_CONNECTS 1024

enum {

// Callbacks
	TCP_NEW_CONNECTION_CALLBACK=0,
	TCP_REMOTE_CONNECTION_CLOSED_CALLBACK,
	TCP_RECEIVED_DATA_CALLBACK,
	TCP_ERROR_CALLBACK,

	TCP_SEND_DATA_MSG=USER_MESSAGE_BASE, // send a string of data of a given length
	
	TCP_START_MSG, // start the instance
	TCP_STOP_MSG, // stop the instance
	TCP_CLOSE_CONNECTION_MSG, // close an individual connection, server mode only

	TCP_REMOTE_HOST_VAR, // various variables, most are self explanitory.
	TCP_REMOTE_PORT_VAR,

	TCP_LOCAL_PORT_VAR,
	
	TCP_CURRENT_CONNECTION_VAR,  // select one of the open ports
	TCP_CONNECTION_COUNT_VAR,	// get only, how many open clients in this instance

	TCP_CONNECTION_MODE_VAR,  // See connection modes below.

	// Connection modes
	TCP_CLIENT,
	TCP_SERVER_NO_LISTEN_ON_CONNECT,
	TCP_SERVER_LISTEN,
	TCP_SERVER_MULTI_CONNECTS,
	
	// extra return values.
	CONNECTION_PENDING

};

#define TCPSendData(pTCP,size,message) (BOOL)SendOMessage(pTCP, TCP_SEND_Data_MSG, size, (LONG) (message))
#define TCPCloseConnection(pTCP, connection) (BOOL)SendOMessage(pTCP, TCP_CLOSE_CONNECTION_MSG, 0, (LONG) connection)
#define TCPStart(pTCP) (BOOL)SendOMessage(pTCP, TCP_START_MSG, 0, 0L)
#define TCPStop(pTCP) (BOOL)SendOMessage(pTCP, TCP_STOP_MSG, 0, 0L)
