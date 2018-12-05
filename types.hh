//By Monica Moniot and Alyssa Riceman
#ifndef TYPES_H
#define TYPES_H
#include <stdlib.h>
#include <inttypes.h>
// #include <pthread.h>

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
template<class t> inline t* malloc(uint size = 1) {
	return (t*)malloc(sizeof(t)*size);
}

//different evictors want to use memory differently
//we define these different types of memory here and combine them all in a union so that each policy has access to its data
//Evictor goes on the cache itself, Evict_item goes on each individual entry


using Index = uint32_t;
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
	pthread_mutex_t mutex;
};



struct Entry {
	Index cur_i;//index to the entry's position in the hash table
	Index key_size;
	byte* key;
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



#endif
