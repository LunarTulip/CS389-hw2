/*
 * Interface for a generic cache object.
 * Data is given as blobs (void *) of a given size,
 * and indexed by a string key.
 */
#ifndef CACHES_H
#define CACHES_H
#include "types.hh"


// An unspecified (implementation dependent, in the C file) cache object.
constexpr uint MUTICES_SIZE = 16;
struct Cache {//Definition of Cache
	Index mem_capacity;
	Index mem_total;
	Index hash_table_capacity;
	Index entry_total;
	byte* mem_arena;//joint allocation of: {hash_table {Index* key_hashes, Bookmark* bookmarks}, Page* pages, void* evict_data}; these fields have functions for retrieving them
	pthread_rwlock_t has_access;
	Book entry_book;
	Evictor evictor;
};



// For a given key string, return a pseudo-random integer:
// typedef index_type (*hash_func)(key_type key);

// Create a new cache object with a given maximum memory capacity.
// If hasher is NULL, use some kind of default (unspecified) has function.
void create_cache(Cache* cache, Index maxmem);

// Add a <key, value> pair to the cache.
// If key already exists, it will overwrite the old value.
// Both the key and the value are to be deep-copied (not just pointer copied).
// If maxmem capacity is exceeded, sufficient values will be removed
// from the cache to accomodate the new value.
// Returns 0 if no errors ocurred, Some nonzero code otherwise.
int cache_set(Cache* cache, const char* key, Index key_size, const char* value, Index value_size);

// Retrieve a pointer to the value associated with key in the cache,
// or NULL if not found.
// Sets the actual size of value in val_size.
// In case of an error, returns nullptr, and sets val_size to 0.
const char* cache_get(Cache* cache, const char* key, Index key_size, Index* ret_value_size);

// Delete an object from the cache, if it's still there
// Returns 0 if no errors ocurred, Some nonzero code otherwise.
int cache_delete(Cache* cache, const char* key, Index key_size);

// Compute the total amount of memory used up by all cache values (not keys)
Index cache_space_used(Cache* cache);

// Destroy all resource connected to a cache object
void destroy_cache(Cache* cache);

#endif
