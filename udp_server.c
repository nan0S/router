#include <netinet/ip.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int main()
{
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		fprintf(stderr, "socket error: %s\n", strerror(errno)); 
		return EXIT_FAILURE;
	}

	struct sockaddr_in server_address;
	bzero (&server_address, sizeof(server_address));
	server_address.sin_family      = AF_INET;
	server_address.sin_port        = htons(32345);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind (sockfd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
		fprintf(stderr, "bind error: %s\n", strerror(errno)); 
		return EXIT_FAILURE;
	}

	for (;;) {

		struct sockaddr_in  sender;	
		socklen_t           sender_len = sizeof(sender);
		u_int8_t            buffer[IP_MAXPACKET+1];

		ssize_t datagram_len = recvfrom (sockfd, buffer, IP_MAXPACKET, 0, (struct sockaddr*)&sender, &sender_len);
		if (datagram_len < 0) {
			fprintf(stderr, "recvfrom error: %s\n", strerror(errno)); 
			return EXIT_FAILURE;
		}

		char sender_ip_str[20]; 
		inet_ntop(AF_INET, &(sender.sin_addr), sender_ip_str, sizeof(sender_ip_str));
		printf ("Received UDP packet from IP address: %s, port: %d\n", sender_ip_str, ntohs(sender.sin_port));

		buffer[datagram_len] = 0;
		printf ("%ld-byte message: +%s+\n", datagram_len, buffer);
		
		char* reply = "Thank you!";
		ssize_t reply_len = strlen(reply);
		if (sendto(sockfd, reply, reply_len, 0, (struct sockaddr*)&sender, sender_len) != reply_len) {
			fprintf(stderr, "sendto error: %s\n", strerror(errno)); 
			return EXIT_FAILURE;		
		}

		fflush(stdout);
	}

	close (sockfd);
	return EXIT_SUCCESS;
}

