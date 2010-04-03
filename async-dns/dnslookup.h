
/*

	Non Blocking DNS lookup

*/



typedef struct void * dns;

typedef int(*FuncPtr)(TaskPtr, NodeObj, int);

enum {dns_processing=0, dns_found, dns_notfound };


void
DnsProcess();

dns
DnsAddLookup(char * hostname, dnscallback callback, NodeObj data);

void
DnsDelLookup(dns entry);

void
DnsTest ();

NodeObj
DnsGetData(dns entry);

int
DnsGetState(dns entry);

char *
DnsGetHostName(dns entry);

char *
DnsGetIPAddr(dns entry);

struct sockaddr_in 
DnsGetSocketInfo(dns entry);


