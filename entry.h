#include <arpa/inet.h>

struct udp_entry
{
	uint32_t ip; // adres ip sieci	
	uint8_t mask; // maska sieci
	uint32_t dist; // dystans do tej sieci
};	

struct entry
{
	struct udp_entry entry; // główna część wpisu
	uint32_t via; // następny na ścieżce
	uint32_t last_time_recv; // jak dawno coś od sieci dostaliśmy 
	uint32_t last_time_send; // przez ile rund już wysyłaliśmy ten wpis (używane gdy dist == INFINITY)
};

// nieskończoność
const uint32_t INFINITY = UINT32_MAX;
// dystans powyżej, którego uważąmy adres za nieosiągalny
const uint32_t MAX_DIST = 32;
// maksymalna ilość rund przez, które możemy nie dostawać wiadomości UDP od sąsiada
const uint32_t MAX_LAST_TIME_NORESPONSE = 3;
// ile razy wysyłamy wpis uznany za nieosiągalny (z odległością nieskończoną)
const uint32_t SEND_LIMIT = 3;

// sąsiedzi
struct entry* dir_entries;
// niesąsiedzi
struct entry* entries;
const int INCR_SIZE = 5;
int MAX_SIZE = INCR_SIZE;
int entry_cnt, dir_entry_cnt;

int direct_unreachable(struct entry* entry)
{
	return entry->last_time_recv >= MAX_LAST_TIME_NORESPONSE;
}

int direct_deleted(struct entry* entry)
{
	return direct_unreachable(entry) && entry->last_time_send > SEND_LIMIT;
}

int indirect_unreachable(struct entry* entry)
{
	return entry->entry.dist == INFINITY;
}

void set_direct_unreachable(struct entry* entry)
{
	entry->last_time_recv = MAX_LAST_TIME_NORESPONSE;
}

void set_indirect_unreachable(struct entry* entry)
{
	entry->entry.dist = INFINITY;
}

// adres sieci na podstawie adresu i maski
uint32_t _get_network(uint32_t addr, uint8_t mask)
{
	uint8_t m = 32 - mask;
	return addr >> m << m;
}

uint32_t get_network(struct entry* entry)
{
	return _get_network(entry->entry.ip, entry->entry.mask);
}

// adres broadcast na podstawie adresu i maski
uint32_t get_broadcast(struct entry* entry)
{
    uint8_t m = 32 - entry->entry.mask;
	return get_network(entry) | ((1 << m) - 1);
}

// dystans do sąsiada i ewentualne oznaczenie sąsiada, że doszła od niego wiadomość
int32_t get_dist_to(uint32_t addr)
{
	for (int i = 0; i < dir_entry_cnt; ++i)
	{
		struct entry* entry = &dir_entries[i];
		uint32_t entry_network = get_network(entry);
		uint32_t addr_network = _get_network(addr, entry->entry.mask);

		if (entry_network == addr_network)
		{
			entry->last_time_recv = 0;
			entry->last_time_send = 0;
			return entry->entry.dist;
		}
	}
	return -1;
}

// jeśli nowy wpis opisuje wpis, który już mamy w naszej tablicy,
// uaktualnimy ten wpis i zwrócimy 1
int update_entries(uint32_t from, struct udp_entry new_entry)
{
	for (int i = 0; i < entry_cnt; ++i)
	{	
		struct entry* cur_entry = &entries[i];
		if (cur_entry->entry.ip == new_entry.ip)
		{
			if (cur_entry->via == from || new_entry.dist < cur_entry->entry.dist)
			{
				cur_entry->entry.dist = new_entry.dist;
				cur_entry->via = from;
				cur_entry->last_time_recv = 0;
				cur_entry->last_time_send = 0;
			}
			// if (cur_entry->entry.dist > MAX_DIST)
				// cur_entry->entry.dist = INFINITY;
			return 1;
		}
	}
	return 0;
}

// dodaj wpis
void add_entry(uint32_t from, struct udp_entry new_entry)
{
	// oblicz adres do sąsiada, od którego to dostaliśmy
	int32_t add_dist = new_entry.dist == INFINITY ? 0 : get_dist_to(from); 
	// jeśli nieznany sąsiad (spoza konfiguracji) odrzuć
	if (add_dist < 0)
		return;

	// jeśli dostaliśmy od samego siebie nie dodamy takiego wpisu 
	// do tablicy - informacja że dostaliśmy wiadomość od naszej sieci
	// jest już zapisana (za pomocą get_dist_to)
	for (int i = 0; i < dir_entry_cnt; ++i)
		if (dir_entries[i].entry.ip == from)
			return;

	// jeśli dostaliśmy informację o sąsiedzie, odrzuć, 
	// bo mamy już informacje o naszych sąsiadach z pliku konfiguracyjnego
	new_entry.ip = _get_network(new_entry.ip, new_entry.mask);
	for (int i = 0; i < dir_entry_cnt; ++i)
		if (get_network(&dir_entries[i]) == new_entry.ip)
			return;

	new_entry.dist += add_dist;
	// jeśli wpis istnieje update_entries zaktualizuje ten wpis i zwróci 1
	if (update_entries(from, new_entry))
		return;	
	
	// jeśli dystans do tego wpisu jest nieskończony to go nie dodawaj
	if (new_entry.dist == INFINITY)
		return;

	// w przeciwnym przypadku dodaj wpis do tablicy
	if (entry_cnt >= MAX_SIZE)
	{
		MAX_SIZE += INCR_SIZE;
		entries = realloc(entries, MAX_SIZE * sizeof(struct entry));
	}

	struct entry* entry = &entries[entry_cnt++];
	entry->entry = new_entry;
	entry->last_time_recv = 0;
	entry->last_time_send = 0;
	entry->via = from;
}
