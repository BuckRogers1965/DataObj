
/*

DNS lookup is a blocking call. 

If your machine is having network problems a program 
trying to lookup a dns entry can take minutes to respond.

This example demonstrates how to do a non blocking dns lookup. 

Since dns itself is not thread safe you can only do one lookup at a time.

The rest of the requests should be queued up to be processed one at a time.  

As the result comes back it can be checked in the main loop of the program
and the return value for that lookup delivered and the next lookup started.


Another way to speed everything up is to cache the value. 
This is the place to add DNS caching as needed.

The calling routines can also just keep their copy of the entry and use the
values found to recall the 

How to build stand alone test

gcc -g -Wall dnslookup.c -o dnslookup -lpthread -DTESTBUILD


The method used for communicating with the thread is to set
a sentinel value in the data structure that is passed to the
thread, occassionally come back and check the value to see if 
it has been marked completed. 

Once completed join wih the thread, collect it's values, call
the given callback to report the status and if found, the ip 
address.

*/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
#include <err.h>

#include "../node.h"

//#include "pthread.h"


typedef struct dns_entry * dns;

typedef void (*dnscallback)(dns lookup);

enum {dns_processing=0, dns_found, dns_notfound };

struct dns_entry {
	int state;
	char * hostname;
	struct sockaddr_in client_addr;
	dns next;
	dnscallback callback;
	NodeObj data;
} dns_entry; 


/* Lookup dns request in seperate thread so the main thread does not block. */
void* mylookupdns (void* arg)
{

	dns lookup = (dns) arg;
		
	if (isalpha (lookup->hostname[0])) {
		struct hostent *host;

		/*  Get the ip number from the hostname */
		if ((host = gethostbyname (lookup->hostname)) == NULL)
		  {
			lookup->state = dns_notfound;
			return NULL;
		  }		// if by name

		/* copy network order ip number to sinaddr data structure */
		memcpy (&lookup->client_addr.sin_addr.s_addr,
			host->h_addr_list[0], host->h_length);
	} else if (isdigit (lookup->hostname[0]))
	      {
		/* if the hostname begins with a digit, then it is an IP number in dot code */
		lookup->client_addr.sin_addr.s_addr = inet_addr (lookup->hostname);
	} else {
		/* otherwise we did not resolve the hostname. */
		lookup->state = dns_notfound;
		return NULL;
	}

	lookup->state = dns_found;
	return NULL;
}


pthread_t thread;
pthread_attr_t attr;

dns lookup;

dns dnshead = NULL;
dns dnstail = NULL;

void
DnsProcess(){

	/* check current request
	if it has completed, handle it */
	if (lookup && lookup->state) {
		/* Wait for the  dns lookup thread to complete, and get the result. */
		pthread_join (thread, NULL);


		(*lookup->callback)(lookup);
		//if (lookup->state == dns_found)
		//	printf("%s\n\n", inet_ntoa (lookup->client_addr.sin_addr));
		//else printf("\n%s not found.\n\n", lookup->hostname);

		free(lookup->hostname);
		free(lookup);

		lookup = NULL;
	}


	/* if no request is being processed
	check buffer for another request to start processing */

	if(!lookup && dnshead){
		lookup = dnshead;
		dnshead=lookup->next;

		if (!dnshead)
			dnstail = NULL;

//		printf("Started dns lookup for %s\n", lookup->hostname);

		/* Start the lookup thread */
		pthread_create (&thread, NULL, &mylookupdns, lookup);
	}

}

char * mystrdup (char * val){
	int length;
	char * ret_val = NULL;
	if (val == NULL)
		return NULL;
	length = strlen (val);
	ret_val = malloc (length+1);
	if (!ret_val)
		return NULL;
	strncpy(ret_val, val, length+1);
	return ret_val;
}


dns
DnsAddLookup(char * hostname, dnscallback callback, NodeObj data ){

	dns temp = malloc (sizeof(dns_entry));

	if (!temp  || !callback )
		return NULL;

	temp->state = 0;
	temp->hostname = mystrdup(hostname);
	temp->next = NULL;
	temp->callback = callback;
	temp->data = data;

	if (!dnstail) {
		dnshead = dnstail = temp;
	} else {
		dnstail->next = temp;
		dnstail = temp;
	}

	return temp;
}

void
DnsDelLookup(dns entry){

	dns current = dnshead;
	dns previous = dnshead;

	/* return if entry is null */
	if (!entry )
		return;

	if (lookup == entry) {
		/* the item being cancelled is being looked up right now */

		int res = 0;

		//lookup->state = dns_notfound;

		//res = pthread_cancel (thread);

		if (res != 0) {
//			printf("Thread cancelation failed");
		}

	}

	/* return if the lookup list is empty */
	if (!dnshead)
		return;

	while (current) {

		if (current == entry)
			break;

		previous = current;
		current = current->next;
	}

	if (current) {
		/* found an item */

		/* detach the item from the list */
		if (current == dnshead ) {
			/* found item is at the head of the list */
			dnshead = current->next;

			/* if that was the only item in the list, mark the tail too */
			if (!dnshead)
				dnstail = NULL;

		} else if (current == dnstail) {
			/*list is more than one item long, 
			  removing item on the end */
			dnstail = previous;
			dnstail->next = NULL;
			
		} else {
			/* removing an item in the middle */
			previous->next = current->next;
		}

		/* free the memory for the item */
		current->next = NULL;
		free(current->hostname);
		free(current);
		current = NULL;
	}
	
	return;
}


NodeObj
DnsGetData(dns entry){

	return entry->data;
}


int
DnsGetState(dns entry){

	return entry->state;
}


char *
DnsGetHostName(dns entry){

	return entry->hostname;
}


char *
DnsGetIPAddr(dns entry){

	return inet_ntoa (entry->client_addr.sin_addr);
}


struct sockaddr_in 
DnsGetSocketInfo(dns entry){

	return entry->client_addr;
}


/* Testing stuff below this point */

void
PrintStatus(dns entry){

	if (DnsGetState(entry) == dns_found)
		printf("\n%s found at %s\n\n", DnsGetHostName(entry), DnsGetIPAddr(entry));
	else printf("\n%s not found.\n\n", DnsGetHostName(entry));

}

dns
TestDnsAddLookup(char * hostname, dnscallback callback, NodeObj data ){

	printf("Adding lookup for %s\n", hostname);
	return DnsAddLookup(hostname, callback, data );
}


void
TestDnsDelLookup(dns entry){

	printf("Removing lookup for %s\n", entry->hostname);
	DnsDelLookup(entry);
}

void
DnsTest ()
{
	dns entry;

	/* test of adding and removing single entry from list */
	TestDnsDelLookup ( TestDnsAddLookup ( "www.google1.com", &PrintStatus, NULL ) );

	/* test of adding and removing single entry from list if it is being looked up */
	entry = TestDnsAddLookup ( "www.google2.com", &PrintStatus, NULL );
	DnsProcess();
	TestDnsDelLookup(entry);
	
	/* test of adding and removing last of two entries from list */
	entry = TestDnsAddLookup ( "localhost1", &PrintStatus, NULL );
	TestDnsDelLookup ( TestDnsAddLookup ( "localhost2", &PrintStatus, NULL ) );

	/*test of removing the first element from a list of two items */
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );
	TestDnsDelLookup(entry);
	
	/* test of removing element from middle of list */
	entry = TestDnsAddLookup ( "localhost3", &PrintStatus, NULL );
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );
	TestDnsDelLookup(entry);

	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );

	/* test of removing item from end of list */
	TestDnsDelLookup ( TestDnsAddLookup ( "www.google3.com", &PrintStatus, NULL ) );

	TestDnsAddLookup ( "www.google.com", &PrintStatus, NULL );
	entry = TestDnsAddLookup ( "localhost4", &PrintStatus, NULL );
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );
	TestDnsAddLookup ( "localhost", &PrintStatus, NULL );

	/* testing removing item from middle of bigger list */
	TestDnsDelLookup(entry);

	/* process entries until they are all finished */
	while (dnshead || lookup ) {
		DnsProcess();
		printf(".");
		usleep(1000);
		fflush(stdout);
	}

	printf("\nFinished DNS test.\n");
}
 
#ifdef TESTBUILD
int main (){

DnsTest();

return 0;
}

#endif
