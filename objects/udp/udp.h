#define MAX_MSG_SIZE 65535

enum {
	UDP_SEND_PACKET_MSG=USER_MESSAGE_BASE,

	UDP_START_MSG,
	UDP_STOP_MSG,

	UDP_REMOTE_HOST_VAR,
	UDP_REMOTE_PORT_VAR,
	UDP_LISTEN_PORT_VAR
};


/* SendOMessage(obj, msg, wParam, lParam) is DeliverMsg(obj, port, msg, data)
   here: the id travels as the message, the payload as the data node (which
   carries its own byte count, so size rides on it), and "Msg" is the object's
   message entry. Setting or reading a var is the same call with the var's id -
   pass a node with a value to set it, an empty one to have it filled in. */
#define UDPSendPacket(pUDP,size,message) ((void)(size), DeliverMsg(pUDP, "Msg", UDP_SEND_PACKET_MSG, (message)))
#define UDPStart(pUDP) DeliverMsg(pUDP, "Msg", UDP_START_MSG, 0L)
#define UDPStop(pUDP) DeliverMsg(pUDP, "Msg", UDP_STOP_MSG, 0L)
