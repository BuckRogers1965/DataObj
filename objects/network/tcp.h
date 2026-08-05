#define MAX_MSG_SIZE 65535
#define MAX_CONNECTS 1024

enum {

// Callbacks from TCP Object to Owner
	TCP_NEW_CONNECTION_CALLBACK=0,
		// return a 0 to refuse the connection, or return yourself to accept.  
		// setup this way to allow the connections to be redirected to other objects.
	TCP_REMOTE_CONNECTION_CLOSED_CALLBACK,
	TCP_RECEIVED_DATA_CALLBACK,
	TCP_ERROR_CALLBACK,

// Messages to TCP Object
	TCP_SEND_DATA_MSG=USER_MESSAGE_BASE, // send a string of data of a given length
	
	TCP_START_MSG, // start the instance
	TCP_STOP_MSG, // stop the instance
	TCP_CLOSE_CONNECTION_MSG, // close an individual connection, server mode only

// Variables that hold information about the connection.
	// everything except the TCP_CURRENT_CONNECTION_VAR is not allowed to be changed while the object is started.
	TCP_REMOTE_HOST_VAR, // various variables, most are self explanitory.
	TCP_REMOTE_PORT_VAR,

	TCP_LOCAL_PORT_VAR,  // if this is 0, then a random local port is selected for you when you connect.
	
	TCP_CURRENT_CONNECTION_VAR,  // select one of the open ports
	// 0 is the server instance, or the client instance.
	// 1 .. N, where N is the TCP_MAX_CONNECTION_COUNT_VAR getting vars is redirected to the correct client instance.
	TCP_CONNECTION_COUNT_VAR,	// get only, how many open clients in this instance
	// is always 0 for the client instance, and 0 for server instance.
	// on the first 2 kinds of server the client connection will be 1
	// on the multiserver connect the clients will connect on 1 .. N, where N is the TCP_MAX_CONNECTION_COUNT_VAR
	TCP_MAX_CONNECTIONS_VAR,	// how many clients in this multi connected server instance

	TCP_CONNECTION_MODE_VAR,  // See connection modes below.

// Connection modes
	TCP_CLIENT,
	TCP_SERVER,
	
// extra return values.
	CONNECTION_PENDING,
	
// security layer
	TCP_SECURITY_MODE_VAR,		// can be off or on, 0 or 1, can only be changed while the object is stopped
	TCP_SECURITY_CERT_VAR,		// pass in a string that points to a file that has a valid certificate
	TCP_SECURITY_KEY_VAR,		// pass in a string that points to a file that has the key for the cert

};

/* SendOMessage(obj, msg, wParam, lParam) is DeliverMsg(obj, port, msg, data)
   here: the id travels as the message and the payload as a data node (which
   carries its own byte count, so size rides on it), delivered to "Msg", the
   object's message entry. A verb whose argument is a plain number needs a node
   to carry it, so TcpSendNum does that and frees it - DeliverMsg is
   synchronous, so nothing outlives the call. */
static inline int TcpSendNum(NodeObj tcp, MsgId message, long number)
{
	NodeObj v = NewNode(STRING);
	int r;

	SetValueInt(v, (int) number);
	r = DeliverMsg(tcp, "Msg", message, v);
	DelNode(v);
	return r;
}

#define TCPSendData(pTCP,size,message) ((void)(size), DeliverMsg(pTCP, "Msg", TCP_SEND_DATA_MSG, (message)))
#define TCPCloseConnection(pTCP, connection) TcpSendNum(pTCP, TCP_CLOSE_CONNECTION_MSG, (long) (connection))
#define TCPStart(pTCP) DeliverMsg(pTCP, "Msg", TCP_START_MSG, 0L)
#define TCPStop(pTCP) DeliverMsg(pTCP, "Msg", TCP_STOP_MSG, 0L)
