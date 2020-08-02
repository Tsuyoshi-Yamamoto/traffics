#include "stdafx.h"

#ifdef RASPBERRY_PI
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

bool winsock_ready = false;

int init_winsock(){
	int status = 0;

#ifndef RASPBERRY_PI
	if(!winsock_ready){
	/* Windows dependent part */
		WSADATA data;
		status = WSAStartup(MAKEWORD(2,0), &data); 
		if(!status)
			winsock_ready = true;
	};
#endif
	return status;
}

void finish_winsock()
{
#ifndef RASPBERRY_PI
	if(winsock_ready){
  /* Windows dependent code */
		WSACleanup();
	};
#endif
	return;
}

#ifndef RASPBERRY_PI
// Windows WinSoc needs theese constants
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
#endif

int shutdown_socket(int dstSocket)
{
   shutdown(dstSocket, SHUT_WR);
   return 0;

}

int close_socket(int dstSocket)
{
	int status;
#ifdef RASPBERRY_PI
		status = close(dstSocket);	  
#else
		status = closesocket(dstSocket);
#endif
   return status;
}
//
//	Initializevirtual Circuit
//
int start_tcp(unsigned short service_port)
{
	struct sockaddr_in srcAddr;

/*	int status = init_winsock();
	if(status != 0)
		return status; */

	/* sockaddr_in initialization */
	memset(&srcAddr, 0, sizeof(srcAddr));
	srcAddr.sin_port = htons(service_port);
	srcAddr.sin_family = AF_INET;
	srcAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* Create Socket */
	int srcSocket = (int)socket(AF_INET, SOCK_STREAM, 0);
	if(srcSocket < 0)
		return -1;
	/* bind socket to this process */
	int status = bind(srcSocket, (struct sockaddr *) &srcAddr, sizeof(srcAddr));
	if(status != 0){
		fprintf(stderr, "bind Error\n");
#ifdef RASPBERRY_PI
		shutdown(srcSocket , SHUT_RDWR);
#else
		shutdown(srcSocket , SD_BOTH);
#endif
		close_socket(srcSocket);
		finish_winsock();
		return -1;
	}
	listen(srcSocket, 1);
	return srcSocket;
}

int accept_tcp(int srcSocket)
{
	int dstSocket;  // Socket for client
	struct sockaddr_in dstAddr;
	int dstAddrSize = sizeof(dstAddr);

	/* Accept connection */
#ifdef DEBUG_MODE
	std::cerr << "Waiting for connection ..." << std::endl;
#endif
	dstSocket = (int)accept(srcSocket, (struct sockaddr *)&dstAddr, (socklen_t *)&dstAddrSize);
#ifdef DEBUG_MODE
	std::cerr << "Connected from " << inet_ntoa(dstAddr.sin_addr) << std::endl;
#endif

	return dstSocket;
}
int accept_tcp(int srcSocket, char *ip_info)
{
	int dstSocket;  // Socket for client
	struct sockaddr_in dstAddr;
	int dstAddrSize = sizeof(dstAddr);

	/* Accept connection */
	dstSocket = (int)accept(srcSocket, (struct sockaddr *)&dstAddr, (socklen_t *)&dstAddrSize);
	sprintf(ip_info,"%s:%d",inet_ntoa(dstAddr.sin_addr),dstAddr.sin_port);

#ifdef DEBUG_MODE
	std::cerr << "Connected from " << ip_info << std::endl;
#endif

	return dstSocket;
}



int connect_tcp(char *destination, unsigned short port)
{
	int dstSocket;
	struct sockaddr_in dstAddr;

/*	int status = init_winsock();
	if(status != 0)
		return status; */

	// set up sockaddr_in structure
	memset(&dstAddr, 0, sizeof(dstAddr));
	dstAddr.sin_port = htons(port);
	dstAddr.sin_family = AF_INET;
	dstAddr.sin_addr.s_addr = inet_addr(destination);

	// Create Scoket
	dstSocket = (int)socket(AF_INET, SOCK_STREAM, 0);

	// Connect
	if(connect(dstSocket, (struct sockaddr *) &dstAddr, sizeof(dstAddr))){
		std::cerr << "Cannot establish connection to  " << destination << ":"<<  port << std::endl;
		return(-1);
	}
#ifdef DEBUG_MODE
  	std::cerr << "Connecting to " << destination << ":" << port << std::endl;
#endif
	return dstSocket;
}


int read_tcp(int dstSocket, char *buffer, int bsize)
{
	int numrcv = recv(dstSocket, buffer, bsize, 0);
	return numrcv;
}

int write_tcp(int dstSocket, char *buffer, int bsize)
{
	int numsend = send(dstSocket, buffer, bsize, 0);
	return numsend;
}

//
//	Functions for UDP Communication
//
int start_udp_port(unsigned short service_port)
{

	int sock;
	struct sockaddr_in addr;

/*	int status = init_winsock();
	if(status != 0)
		return status; */

	sock = (int)socket(AF_INET, SOCK_DGRAM, 0);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(service_port);

#ifdef RASPBERRY_PI
	addr.sin_addr.s_addr = INADDR_ANY;
#else
	addr.sin_addr.S_un.S_addr = INADDR_ANY;
#endif

	if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0){
#ifdef RASPBERRY_PI
		shutdown(sock , SHUT_RDWR);
#else
		shutdown(sock , SD_BOTH);
#endif
		int status = close_socket(sock);
		finish_winsock();
		return -1;
	}
/*
// For non-blocking IO
#ifdef RASPBERRY_PI

int flag = fcntl(sock,F_GETFL, 0);
	if(flag < 0)
		perror("fcntl(GET) error");

	fcntl(sock,F_SETFL,flag | O_NONBLOCK);
#else
	u_long val=1;
	ioctlsocket(sock, FIONBIO, &val);

#endif
*/
	return sock;

}
//
//	udp port for trasmission only
//
int start_udp_send()
{
/*	int status = init_winsock();
	if(status != 0)
		return -1; */
	return (int)socket(AF_INET, SOCK_DGRAM, 0);
} 
//
//	recv_udp returns 0 or -1 if no data arrives.
//
int recv_udp(int socket, char *buffer, int bsize)
{
    memset(buffer, 0,bsize);
    int rsize = recv(socket, buffer,bsize, 0);
	return rsize;
}

//	recv_udp returns 0 or -1 if no data arrives.
//
int recvfrom_udp(int socket, char *buffer, int bsize, char *frominfo)
{

    struct sockaddr_in from;
    socklen_t sockaddr_in_size = sizeof(struct sockaddr_in);
    memset(buffer, 0,bsize);
    int rsize = recvfrom(socket, buffer, bsize, 0, (struct sockaddr *)&from, &sockaddr_in_size);
	if(rsize > 0){
		sprintf(frominfo,"%s:%d",inet_ntoa(from.sin_addr), ntohs(from.sin_port));
	}
	else {
		*frominfo=0;
	};
	return rsize;
}
//
//	send_udp 
//
int sendto_udp(int socket, char *dest, unsigned short port, char *buffer, int bsize)
{
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
#ifdef RASPBERRY_PI
	addr.sin_addr.s_addr = inet_addr(dest);
#else
	addr.sin_addr.S_un.S_addr = inet_addr(dest);
#endif

	return sendto(socket, buffer, bsize, 0, (struct sockaddr *)&addr, sizeof(addr));
}

int send_broadcast(int socket, char *dest, unsigned short port, char *buffer, int bsize)
{
	struct sockaddr_in addr;
	int yes=TRUE;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
#ifdef RASPBERRY_PI
	addr.sin_addr.s_addr = inet_addr(dest);
#else
	addr.sin_addr.S_un.S_addr = inet_addr(dest);
#endif
	setsockopt(socket,
            SOL_SOCKET, SO_BROADCAST, (char *)&yes, sizeof yes);
	return sendto(socket, buffer, bsize, 0, (struct sockaddr *)&addr, sizeof(addr));
}
//
//	Asyncronous communication parts
//
int select_2sockets(int *socks)
{

	fd_set fds, readfds;
	int sock1 = *socks;
	int sock2 = *(socks+1);
	int maxfd = sock2;
	if(sock1>sock2){
		maxfd=sock1;
	};

	FD_ZERO(&readfds);
	FD_SET(sock1, &readfds);
	FD_SET(sock2, &readfds);

	memcpy(&fds, &readfds, sizeof(fd_set));
	select(maxfd+1, &fds, NULL, NULL, NULL);

	int n_ready = 0;
	*socks = 0;
	*(socks+1) = 0;
	if(FD_ISSET(sock1, &fds)) { /* sock1 is ready */
		*socks = sock1;
		n_ready++;
	};
	if(FD_ISSET(sock2, &fds)) { /* sock1 is ready */
		*(socks+1) = sock2;
		n_ready++;
	};

	return n_ready;
}

//
//	Asyncronous communication parts
//
int select_2sockets(int *socks, int waittime_sec)
{

	struct timeval tv;

	fd_set fds, readfds;
	int sock1 = *socks;
	int sock2 = *(socks+1);
	int maxfd = sock2;
	if(sock1>sock2){
		maxfd=sock1;
	};

	FD_ZERO(&readfds);
	FD_SET(sock1, &readfds);
	FD_SET(sock2, &readfds);

	tv.tv_sec = waittime_sec;
	tv.tv_usec = 0;

	memcpy(&fds, &readfds, sizeof(fd_set));
	if(0 == select(maxfd+1, &fds, NULL, NULL, &tv)){
		*socks = 0;
		*socks = 0;
		return 0;
	};

	int n_ready = 0;
	*socks = 0;
	*(socks+1) = 0;
	if(FD_ISSET(sock1, &fds)) { /* sock1 is ready */
		*socks = sock1;
		n_ready++;
	};
	if(FD_ISSET(sock2, &fds)) { /* sock1 is ready */
		*(socks+1) = sock2;
		n_ready++;
	};

	return n_ready;
}

std::string localhost() {
    std::string dest;
/*	int status = init_winsock();
	if(status != 0)
		return dest; */
    int s = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s < 0) return dest;
    
    // コネクションを確立（しようと）するためのダミーの IP アドレス
    struct in_addr dummy;
#ifdef RASPBERRY_PI
	dummy.s_addr = inet_addr("192.0.2.4");
#else
	dummy.S_un.S_addr = inet_addr("192.0.2.4");
#endif
    
    struct sockaddr_in exaddr = { 0 };
    memset(&exaddr, 0, sizeof(exaddr));
    exaddr.sin_family = AF_INET;
    memcpy((char*)&exaddr.sin_addr, &dummy, sizeof(dummy));
    
    if (connect(s, (struct sockaddr*)&exaddr, sizeof(exaddr)) < 0) {
        close_socket(s);
        return dest;
    }
    
    struct sockaddr_in addr = {0};
    socklen_t len = sizeof(addr);
    int status = getsockname(s, (struct sockaddr*)&addr, &len);
    if (status >= 0) dest = inet_ntoa(addr.sin_addr);
    close_socket(s);
    
    return dest;
}

// Get hostname and IP addresses
int getLocalHostName(char *hostnamebuf, int size)
{
	gethostname(hostnamebuf, size);
	return (int)strlen(hostnamebuf);
}

#ifndef RASPBERRY_PI
int getIpAdresses(unsigned int *ipaddrbuf,int size)
{
// Get IP adresses of this computer
	int i;
	HOSTENT *lpHost; 	//  structyre for host information
	char szBuf[256];	// arrays for hostname and IP address

// At first, get hostname of this computer
	gethostname(szBuf, (int)sizeof(szBuf));
// Then, get host information by the name
	lpHost = gethostbyname(szBuf);
// Get IP addresses
	for(i=0; lpHost->h_addr_list[i]; i++) {
		memcpy((ipaddrbuf+i), lpHost->h_addr_list[i], 4);
		i++;
		*(ipaddrbuf+i)=0;
	}
	return i;
}

// Print hostname and IP addresses
void printIpHost(void)
{
	int i;
	HOSTENT *lpHost; 	//  structyre for host information
	IN_ADDR inaddr;		// structure for IP address
	char szBuf[256];	// arrays for hostname and IP address

// At first, get hostname of this computer
	gethostname(szBuf, (int)sizeof(szBuf));
	printf("HOST Name : %s\n", szBuf);

// Then, get host information by the name
	lpHost = gethostbyname(szBuf);
// Get IP addresses
	for(i=0; lpHost->h_addr_list[i]; i++) {
		memcpy(&inaddr, lpHost->h_addr_list[i], 4);
		printf("IP Adress : %s\n", inet_ntoa(inaddr));
	}
	return;
}

#else
void printIpHost(void)
{
	printf("printIpHost is not implemented.\n");
}
#endif








