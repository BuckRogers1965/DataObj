#define MAX_HOST_SIZE 256

enum {
	DNS_LOOKUP_MSG=USER_MESSAGE_BASE,

	DNS_CANCEL_MSG,

	DNS_HOSTNAME_VAR,
	DNS_IPADDR_VAR
};


/* Same shape as udp.h: the id travels as the message, the payload as the
   data node - a name to look up, an address coming back. Ask with a node
   whose value is the hostname; the answer arrives later as YOUR message id
   on the port you named at creation, its value the address and empty when
   the name did not resolve. Reading a var is the same call with the var's
   id and an empty node to fill in - inside that callback they hold the
   name and address of the answer being delivered, so a caller that asked
   for several names knows which one this is. Cancel with the name: an
   answer already in flight cannot be stopped, but it stops being yours. */
#define DNSLookup(pDNS,hostname) DeliverMsg(pDNS, "Msg", DNS_LOOKUP_MSG, (hostname))
#define DNSCancel(pDNS,hostname) DeliverMsg(pDNS, "Msg", DNS_CANCEL_MSG, (hostname))
