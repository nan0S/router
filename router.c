/* autor: Hubert Obrzut
   nr indeksu: 309306
   zadanie programistyczne nr 2 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include "entry.h"

// wczytywanie początkowej konfiguracji sieci
int read_config()
{
	FILE* config = fopen("config", "r");
	if (config == NULL)
	{
		fprintf(stderr, "config file error: %s\n", strerror(errno));
		return -1;
	}

	fscanf(config, "%d", &dir_entry_cnt);
	dir_entries = malloc(dir_entry_cnt * sizeof(struct entry));
	entries = malloc(MAX_SIZE * sizeof(struct entry));

	for (int i = 0; i < dir_entry_cnt; ++i)
	{
		uint32_t a, b, c, d;
		uint8_t mask;
		fscanf(config, "%u.%u.%u.%u/%hhu", &a, &b, &c, &d, &mask);
		// nie wiem dlaczego, ale do momentu linii add_entry(...)
		// mask było wyzerowane na maszynie virtualboxa
		// zerowało się po linii fscanf(config, "%s", tmp)
		uint8_t m = mask;
		char tmp[8];
		fscanf(config, "%s", tmp);
		uint32_t dist;
		fscanf(config, "%u", &dist);

		uint32_t ip = a << 24 | b << 16 | c << 8 | d;
		struct entry e = {{.ip = ip, .mask = m, .dist = dist},
							.via = 0,
							.last_time_recv = 0,
							.last_time_send = 0};
		dir_entries[i] = e;
	}

	return 0;
}

// przygotowanie serwera słuchającego na porcie 54321, odbierającego pakiety UDP
int setup_udp_server(int* fd)
{
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
	{
		fprintf(stderr, "socket error: %s\n", strerror(errno));
		return -1;
	}

	int broadcast = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
    {	
		fprintf(stderr, "broadcast option error: %s\n", strerror(errno));
        return -1;
    }

	struct sockaddr_in server_address;
	bzero(&server_address, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(54321);
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sockfd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
	{
		fprintf(stderr, "bind error: %s\n", strerror(errno));
		return -1;
	}

	*fd = sockfd;

	return 0;
}

// pakuje wpis w tablicy do wiadomości i wysyła na adres addr
int send_route_table_elem(int sockfd, struct udp_entry entry, uint32_t addr)
{
	struct sockaddr_in server_address;
	bzero(&server_address, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(54321);
	server_address.sin_addr.s_addr = addr;
	
	uint8_t message[9];
	*(uint32_t*)message = entry.ip;
	*(message + 4) = entry.mask;
	*(uint32_t*)(message + 5) = entry.dist;

	ssize_t message_len = sizeof(message);
	if (sendto(sockfd, message, message_len, 0, (struct sockaddr*)&server_address, sizeof(server_address)) != message_len)
	{
		fprintf(stderr, "sendto error: %s\n", strerror(errno));
		return -1;
	}

	// char ip[20];
	// inet_ntop(AF_INET, &(addr), ip, sizeof(ip));
	// printf("Sent UDP packet to IP address: %s\n", ip);

	return 0;
}

// wysłanie całej tablicy zbudowanej dotychczas tablicy routingu
void send_route_table(int sockfd)
{
	for (int i = 0; i < dir_entry_cnt; ++i)
	{	
		struct entry* dest = &dir_entries[i];
		uint32_t dest_addr = htonl(get_broadcast(dest));
		int failed = 0;

		// wysłanie informacji o sąsiadach
		for (int j = 0; j < dir_entry_cnt; ++j)
		{
			struct entry entry = dir_entries[j];
			if (direct_deleted(&entry))
				continue;
			if (direct_unreachable(&entry))
				entry.entry.dist = INFINITY;
			
			if (send_route_table_elem(sockfd, entry.entry, dest_addr) < 0)
				failed = 1;
		}

		// wysłanie reszty wpisów (niesąsiadów)
		for (int j = 0; j < entry_cnt; ++j)
			if (send_route_table_elem(sockfd, entries[j].entry, dest_addr) < 0)
				failed = 1;

		if (failed)
			set_direct_unreachable(dest);
	}
}

// aktualizuje aktualną tablice routingu
void update_route_table()
{
	for (int i = 0; i < dir_entry_cnt; ++i)
	{
		struct entry* entry = &dir_entries[i];
		++entry->last_time_recv;
		if (direct_unreachable(entry))
		{
			// jeśli sieć, z która sąsiadujemy własnie teraz została 
			// uznana za nieosiągalna, oznacz wszystkie wpisy prowadzące przez tą sieć,
			// jako nieosiągalne
			if (entry->last_time_send++ == 0)
				for (int j = 0; j < entry_cnt; ++j)
					if (get_network(entry) == _get_network(entries[j].via, entry->entry.mask))
						set_indirect_unreachable(&entries[j]);
		}
		else
			entry->last_time_send = 0;
	}

	for (int i = 0; i < entry_cnt; ++i)
	{
		struct entry* entry = &entries[i];
		++entry->last_time_recv;
		if (entry->entry.dist > MAX_DIST || entry->last_time_recv > MAX_LAST_TIME_NORESPONSE)
			entry->entry.dist = INFINITY;
		if (indirect_unreachable(entry))
		{
			// jeśli sieć jest nieosiąglna i została rozgłoszona
			// już wystarczająca ilość razy, usuń wpis o niej z tablicy
			++entry->last_time_send;
			if (entry->last_time_send > SEND_LIMIT)
			{
				*entry = entries[entry_cnt-- - 1];
				--i;
			}
		}
		else
			entry->last_time_send = 0;		
	}
}

// wypisanie sformatowanej tablicy routingu
void print_route_table()
{	
	printf("\n");

	// wypisanie sąsiednich sieci
	for (int i = 0; i < dir_entry_cnt; ++i)
	{
		struct entry* entry = &dir_entries[i];
		if (direct_deleted(entry))
			continue;

		uint32_t network = htonl(get_network(entry));
		char ip[20];
		inet_ntop(AF_INET, &network, ip, sizeof(ip));
		printf("%s/%d ", ip, entry->entry.mask);
		
		if (direct_unreachable(entry))
			printf("unreachable ");
		else
			printf("distance %d ", entry->entry.dist);

		printf("connected directly\n");		
	}

	// wypisanie reszty wpisów (niesąsiednich)
	for (int i = 0; i < entry_cnt; ++i)
	{
		struct entry* entry = &entries[i];
		uint32_t network = htonl(get_network(entry));
		char ip[20];
		inet_ntop(AF_INET, &network, ip, sizeof(ip));
		printf("%s/%d ", ip, entry->entry.mask);

		if (indirect_unreachable(entry))
			printf("unreachable\n");
		else
		{
			network = htonl(entry->via);
			inet_ntop(AF_INET, &network, ip, sizeof(ip));
			printf("distance %d via %s\n", entry->entry.dist, ip);
		}
	}
}

// odbierz pakiet UDP
void receive_udp_packet(int sockfd)
{
	struct sockaddr_in sender;
	socklen_t sender_len = sizeof(sender);
	uint8_t message[9];
	ssize_t packet_len = recvfrom(sockfd, message, sizeof(message), 0, (struct sockaddr*)&sender, &sender_len);

	if (packet_len < 0)
	{
		fprintf(stderr, "recvfrom error: %s\n", strerror(errno));
		return;
	}
	
	// pakiet musi mieć wielkość 9 bajtów
	if (packet_len != 9)
		return;

	struct udp_entry entry;
	entry.ip = *(uint32_t*)message;
	entry.mask = *(message + 4);
	entry.dist = *(uint32_t*)(message + 5);

	// dodaj odebrany wpis do tablicy routingu
	add_entry(ntohl(sender.sin_addr.s_addr), entry);

	// char ip[20];
	// inet_ntop(AF_INET, &(sender.sin_addr), ip, sizeof(ip));
	// printf("Received UDP packet from IP address: %s\n", ip);
}

// start serwera, główna pętla
void run_server(int sockfd)
{
	const int round_time = 4e6;
	const int wait_time = 2;
	fd_set descriptors;
	
	while (1)
	{
		//początek rundy 

		// wyślij tablice routingu do sąsiadów
		send_route_table(sockfd);
		// zaktualizuj ją, usuń niedostępne wpisy
		update_route_table();
		// wypisz
		print_route_table();

		// poczekaj przez round_time mikrosekund na wiadomośći UDP 
		// przychodzące na port, na którym słuchamy
		// czekamy nieaktywne - za pomocą select
		// czekamy wait_time sekund - budzimy się, gdy przyszła wiadomość
		// lub minęło wait_time sekund - jeśli jeszcze nie minęło round_time 
		// mikrosekund zasypiamy na nowo na wait_time sekund
		int passed = 0;
		while (passed < round_time)
		{
			FD_ZERO(&descriptors);
			FD_SET(sockfd, &descriptors);

			struct timeval tv = {wait_time, 0};
			int ready = select(sockfd + 1, &descriptors, NULL, NULL, &tv);
			passed += (wait_time - tv.tv_sec) * 1e6 - tv.tv_usec;

			if (ready > 0)
				receive_udp_packet(sockfd);
		}
	}
}

int main()
{	
	if (read_config() < 0)
		return EXIT_FAILURE;

	int sockfd;
	if (setup_udp_server(&sockfd) < 0)
		return EXIT_FAILURE;

	run_server(sockfd);

	close(sockfd);
	free(dir_entries);
	free(entries);

	return EXIT_SUCCESS;
}
