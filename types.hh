//By Monica Moniot and Alyssa Riceman
#ifndef TYPES_H
#define TYPES_H
#include <stdlib.h>
#include "cache_server.hh"

using byte = char;
using uint = uint32_t;
using int8 = uint8_t;
using uint8 = uint8_t;
using int32 = uint32_t;
using uint32 = uint32_t;
using int64 = uint64_t;
using uint64 = uint64_t;

template<class t, class x> constexpr inline t cast(x value) {
	return (t)value;
}
template<class t> inline t* malloc(uint size) {
	return (t*)malloc(sizeof(t)*size);
}

//different evictors want to use memory differently
//we define these different types of memory here and combine them all in a union so that each policy has access to its data
//Evictor goes on the cache itself, Evict_item goes on each individual entry


struct Node {
	Index next;
	Index pre;
};
using Evict_item = Node;
struct DLL {
	Index head;
};

struct Evictor {
	DLL list;
};



struct Entry {
	Index cur_i;//index to the entry's position in the hash table
	byte* key;
	Index key_size;
	byte* value;
	Index value_size;
	Evict_item evict_item;
};

using Bookmark = Index;
using Page_data = Entry;
union Page {
	Bookmark next;
	Page_data data;
};
struct Book {
	Page* pages;
	Bookmark end;
	Bookmark first_unused;
};


struct Cache {//Definition of Cache
	Index mem_capacity;
	Index mem_total;
	Index hash_table_capacity;
	Index entry_total;
	Index dead_total;//records deleted entries
	byte* mem_arena;//joint allocation of: {hash_table {Index* key_hashes, Bookmark* bookmarks}, Page* pages, void* evict_data}; these fields have functions for retrieving them
	Book entry_book;
	Evictor evictor;
};
#endif
