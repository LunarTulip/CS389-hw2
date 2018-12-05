/*
 * Interface for a generic cache object.
 * Data is given as blobs (void *) of a given size,
 * and indexed by a string key.
 */

 #include <inttypes.h>

// An unspecified (implementation dependent, in the C file) cache object.
struct cache_obj;
typedef struct cache_obj *cache_type;

typedef const char *key_type;  // C string, null terminated
typedef const void *val_type;
typedef uint32_t index_type;

// For a given key string, return a pseudo-random integer:
typedef index_type (*hash_func)(key_type key);

// Create a new cache object with a given maximum memory capacity.
// If hasher is NULL, use some kind of default (unspecified) has function.
cache_type create_cache(index_type maxmem, hash_func hasher);

// Add a <key, value> pair to the cache.
// If key already exists, it will overwrite the old value.
// Both the key and the value are to be deep-copied (not just pointer copied).
// If maxmem capacity is exceeded, sufficient values will be removed
// from the cache to accomodate the new value.
// Returns 0 if no errors ocurred, Some nonzero code otherwise.
int cache_set(cache_type cache, key_type key, val_type val, index_type val_size);

// Retrieve a pointer to the value associated with key in the cache,
// or NULL if not found.
// Sets the actual size of value in val_size.
// In case of an error, returns nullptr, and sets val_size to 0.
val_type cache_get(cache_type cache, key_type key, index_type *val_size);

// Delete an object from the cache, if it's still there
// Returns 0 if no errors ocurred, Some nonzero code otherwise.
int cache_delete(cache_type cache, key_type key);

// Compute the total amount of memory used up by all cache values (not keys)
index_type cache_space_used(cache_type cache);

// Destroy all resource connected to a cache object
void destroy_cache(cache_type cache);
