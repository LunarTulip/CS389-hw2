//By Monica Moniot and Alyssa Riceman
#include <stdlib.h>
#include <cstring>
#include <stdio.h>
#include "types.h"
#include "book.h"
#include "eviction.h"
#include "cache.h"

constexpr evictor_type POLICY = LRU;
constexpr Index INIT_ENTRY_CAPACITY = 64;//must be a power of 2
constexpr double LOAD_FACTOR = .5;
constexpr Index INIT_HASH_TABLE_CAPACITY = static_cast<Index>(INIT_ENTRY_CAPACITY/LOAD_FACTOR);

constexpr Index EMPTY = 0;
constexpr Index DELETED = 1;
constexpr Index HIGH_BIT = 1<<(8*sizeof(Index) - 1);
constexpr Index HASH_MULTIPLIER = 2654435769;
constexpr char NULL_TERMINATOR = 0;

constexpr inline Index get_hash(Index key_hash) {
	//we want to flag entries by setting their key_hash to EMPTY and DELETED
	//we need to modify the hash so that it can't equal EMPTY or DELETED
	//if we don't then everything will break
	return key_hash|HIGH_BIT;
}
constexpr inline Index get_step_size(Index key_hash) {
	//gets the step size for traversing the hash table
	//we are going to be doing double hashing for our hash table
	//the step size must be coprime with hash_table_capacity(a power of 2)
	//so it must be odd(if it's not it could crash)
	//we want the step size to have little relation with the initial hash so we hash it
	return 2*(key_hash*HASH_MULTIPLIER) + 1;
}
Index get_key_size(Key_ptr key) {
	//always returns the size in bytes, includes the null terminator in the size
	Index size = 0;
	while(key[size] != NULL_TERMINATOR) {
		size += 1;
	}
	size += 1;
	return size;//must be in bytes
}
Index default_key_hasher(Key_ptr key) {
	//generates a hash of a c string
	//We are using David Knuth's multiplicative hash algorithm
	Index i = 0;
	Index hash = 0;
	byte byte0;
	byte byte8;
	byte byte16;
	byte byte24;
	while(true) {
		byte0 = key[i];
		i += 1;
		if(byte0 == NULL_TERMINATOR) {
			return hash^(i*HASH_MULTIPLIER);
		};
		byte8 = key[i];
		byte16 = 0;
		byte24 = 0;
		i += 1;
		if(byte8 == NULL_TERMINATOR) break;
		byte16 = key[i];
		i += 1;
		if(byte16 == NULL_TERMINATOR) break;
		byte24 = key[i];
		i += 1;
		if(byte24 == NULL_TERMINATOR) break;
		Index full = (byte24 << 24)|(byte16 << 16)|(byte8 << 8)|(byte0);
		hash = hash^(full*HASH_MULTIPLIER);
	}
	Index full = (byte24 << 24)|(byte16 << 16)|(byte8 << 8)|(byte0);
	hash = hash^(full*HASH_MULTIPLIER);
	return hash^(i*HASH_MULTIPLIER);
}
bool are_keys_equal(Key_ptr key0, Key_ptr key1) {
	//must be null terminated strings or seg-fault
	Index i = 0;
	while(true) {
		auto c0 = key0[i];
		auto c1 = key1[i];
		if(c0 != c1) {
			return false;
		} else if(c0 == NULL_TERMINATOR) {
			return true;
		}
		i += 1;
	}
}


constexpr inline Index get_hash_table_capacity(Index entry_capacity) {
	//instead of storing the capactiy of the hash table, we calculate it jit
	return INIT_HASH_TABLE_CAPACITY*(entry_capacity/INIT_ENTRY_CAPACITY);
}
constexpr inline bool is_exceeding_load(Index entry_total, Index dead_total, Index entry_capacity) {
	//when returns true triggers a table resizing
	//we would seg-fault if entry_total exceeds entry_capacity
	bool is_exceed_entry = entry_total >= entry_capacity;
	bool is_exceed_load_factor = entry_total + dead_total > LOAD_FACTOR*get_hash_table_capacity(entry_capacity);
	return is_exceed_entry or is_exceed_load_factor;
}

//instead of storing pointers to our tables, we calculate them jit
constexpr inline Index* get_hashes    (byte* mem_arena) {
	//hashes is part of the hash table
	//in order to traverse the hash table, we traverse hashes
	//the hash marks if an entry is empty, deleted or populated
	return reinterpret_cast<Index*>(mem_arena);
}
constexpr inline Index* get_bookmarks (byte* mem_arena, Index entry_capacity) {
	//bookmarks is part of the hash table
	//it stores the index of the page in the book connected to the hash table entry
	const auto hash_table_capacity = get_hash_table_capacity(entry_capacity);
	return reinterpret_cast<Index*>(mem_arena + sizeof(Index)*hash_table_capacity);
}
constexpr inline Page*  get_pages     (byte* mem_arena, Index entry_capacity) {
	//pages stores the primary data structure of Book
	const auto hash_table_capacity = get_hash_table_capacity(entry_capacity);
	const auto hash_table_size = (2*sizeof(Index))*hash_table_capacity;
	return reinterpret_cast<Page*>(mem_arena + hash_table_size);
}
constexpr inline void*  get_evict_data(byte* mem_arena, Index entry_capacity) {
	//evict_data points to the internal data used by the evictor
	//the evictor might not use this data, so it may be an invalid pointer
	const auto hash_table_capacity = get_hash_table_capacity(entry_capacity);
	const auto hash_table_size = (2*sizeof(Index))*hash_table_capacity;
	const auto book_size = sizeof(Page)*entry_capacity;
	return reinterpret_cast<void*>(mem_arena + hash_table_size + book_size);
}

inline byte* allocate(Index entry_capacity, evictor_type policy) {
	//we allocate all of our dynamic memory right here
	//we do a joint allocation of everything for many reasons:
	//we have to manage almost no memory with a joint allocation
	//a joint allocation is faster than many separate ones
	//a joint allocation greatly improves locality
	//we don't have to store pointers to every data structure
	//a jointly allocated block is easily serializable
	const auto hash_table_capacity = get_hash_table_capacity(entry_capacity);
	const auto hash_table_size = (2*sizeof(Index))*hash_table_capacity;
	const auto book_size = sizeof(Page)*entry_capacity;
	const auto evictor_size = get_evictor_mem_size(policy, entry_capacity);
	return new byte[hash_table_size + book_size + evictor_size];
}


void mark_as_empty(Index* key_hashes, Index hash_table_capacity) {
	for(Index i = 0; i < hash_table_capacity; i += 1) {
		key_hashes[i] = EMPTY;
	}
}

constexpr Index KEY_NOT_FOUND = -1;
inline Index find_entry(Cache* cache, Key_ptr key) {
	//gets the hash table index associated to key
	const auto hash_table_capacity = get_hash_table_capacity(cache->entry_capacity);
	const auto bookmarks = get_bookmarks(cache->mem_arena, cache->entry_capacity);
	const auto key_hashes = get_hashes(cache->mem_arena);
	const auto key_hash = get_hash(cache->hash(key));
	const auto entry_book = &cache->entry_book;
	//check if key is in cache
	Index expected_i = key_hash%hash_table_capacity;
	Index step_size = get_step_size(key_hash);
	for(Index count = 0; count < hash_table_capacity; count += 1) {
		auto cur_key_hash = key_hashes[expected_i];
		if(cur_key_hash == EMPTY) {
			return KEY_NOT_FOUND;
		} else if(cur_key_hash == DELETED) {
			// continue;
		} else if(cur_key_hash == key_hash) {
			Entry* entry = read_book(entry_book, bookmarks[expected_i]);
			if(are_keys_equal(entry->key, key)) {//found key
				return expected_i;
			}
		}
		expected_i = (expected_i + step_size)%hash_table_capacity;
	}
	printf("Error when attempting to find entry in cache: Full table traversal; index was %d, step was %d, key was %s, size was %d\n", expected_i, step_size, key, hash_table_capacity);
	return KEY_NOT_FOUND;
}

inline void remove_entry(Cache* cache, Index i) {
	//removes an entry to our cache, including from the hash table
	//this is the only code that removes entries;
	//it handles everything necessary for removing an entry
	const auto key_hashes = get_hashes(cache->mem_arena);
	const auto bookmarks = get_bookmarks(cache->mem_arena, cache->entry_capacity);
	const auto entry_book = &cache->entry_book;
	const auto evictor = &cache->evictor;

	auto bookmark = bookmarks[i];
	Entry* entry = read_book(entry_book, bookmark);

	delete[] entry->key;
	entry->key = NULL;
	key_hashes[i] = DELETED;
	cache->entry_total -= 1;
	cache->dead_total += 1;

	cache->mem_total -= entry->value_size + entry->key_size;
	delete[] entry->value;
	entry->value = NULL;

	remove_evict_item(evictor, bookmark, &entry->evict_item, entry_book);
	free_book_page(entry_book, bookmark);
}

inline void update_mem_size(Cache* cache, Index mem_change) {
	//sets the mem_total of the cache and evicts if necessary
	const auto entry_book = &cache->entry_book;
	const auto evictor = &cache->evictor;
	const auto mem_capacity = cache->mem_capacity;
	cache->mem_total += mem_change;
	while(cache->mem_total > mem_capacity) {//Evict
		Index bookmark = get_evict_item(evictor, entry_book);
		Entry* entry = read_book(entry_book, bookmark);
		remove_entry(cache, entry->cur_i);
	}
}
inline void grow_cache_size(Cache* cache) {
	const auto pre_capacity = cache->entry_capacity;
	const auto new_capacity = 2*pre_capacity;

	const auto pre_mem_arena = cache->mem_arena;
	const auto pre_key_hashes = get_hashes(pre_mem_arena);
	const auto pre_bookmarks = get_bookmarks(pre_mem_arena, pre_capacity);
	const auto pre_pages = get_pages(pre_mem_arena, pre_capacity);
	const auto pre_evict_data = get_evict_data(pre_mem_arena, pre_capacity);
	const auto entry_book = &cache->entry_book;

	// printf("growing cache now\n");

	auto new_mem_arena = allocate(new_capacity, cache->evictor.policy);
	cache->mem_arena = new_mem_arena;
	cache->entry_capacity = new_capacity;

	const auto new_key_hashes = get_hashes(new_mem_arena);
	const auto new_bookmarks = get_bookmarks(new_mem_arena, new_capacity);
	const auto new_pages = get_pages(new_mem_arena, new_capacity);
	const auto new_evict_data = get_evict_data(new_mem_arena, new_capacity);

	//make sure all entries are marked as EMPTY, so they can be populated
	mark_as_empty(new_key_hashes, get_hash_table_capacity(new_capacity));
	memcpy(new_pages, pre_pages, sizeof(Page)*pre_capacity);
	memcpy(new_evict_data, pre_evict_data, get_evictor_mem_size(cache->evictor.policy, pre_capacity));
	entry_book->pages = new_pages;
	cache->evictor.mem_arena = new_evict_data;

	//rehash our entries back into the new table
	auto entries_left = cache->entry_total;
	for(Index i = 0; entries_left > 0; i += 1) {
		auto key_hash = pre_key_hashes[i];
		if(key_hash != EMPTY and key_hash != DELETED) {
			entries_left -= 1;
			Index key_hash = pre_key_hashes[i];
			//find empty index
			Index i = key_hash%new_capacity;
			Index step_size = get_step_size(key_hash);
			while(true) {
				auto cur_key_hash = new_key_hashes[i];
				if(cur_key_hash == EMPTY) {
					break;
				}
				i = (i + step_size)%new_capacity;
			}
			//write to new entry it's new location
			auto bookmark = pre_bookmarks[i];
			Entry* entry = read_book(entry_book, bookmark);
			entry->cur_i = i;

			new_key_hashes[i] = key_hash;
			new_bookmarks[i] = bookmark;
		}
	}
	cache->dead_total = 0;

	delete[] pre_mem_arena;
}


Cache* create_cache(Index max_mem, Hash_func hash) {
	Index entry_capacity = INIT_ENTRY_CAPACITY;
	Cache* cache = new Cache;
	cache->mem_capacity = max_mem;
	cache->mem_total = 0;
	cache->entry_capacity = entry_capacity;
	cache->entry_total = 0;
	cache->dead_total = 0;
	if(hash == NULL) {
		cache->hash = &default_key_hasher;
	} else {
		cache->hash = hash;
	}
	auto mem_arena = allocate(entry_capacity, POLICY);
	//make sure all entries are marked as EMPTY, so they can be populated
	mark_as_empty(get_hashes(mem_arena), get_hash_table_capacity(entry_capacity));
	cache->mem_arena = mem_arena;
	create_book(&cache->entry_book, get_pages(mem_arena, entry_capacity));
	cache->evictor.mem_arena = get_evict_data(mem_arena, entry_capacity);
	create_evictor(&cache->evictor, POLICY);
	return cache;
}
void destroy_cache(Cache* cache) {
	const auto hash_table_capacity = get_hash_table_capacity(cache->entry_capacity);
	const auto key_hashes = get_hashes(cache->mem_arena);
	const auto bookmarks = get_bookmarks(cache->mem_arena, cache->entry_capacity);
	const auto entry_book = &cache->entry_book;

	//remove every entry
	for(Index i = 0; i < hash_table_capacity; i += 1) {
		auto cur_key_hash = key_hashes[i];
		if(cur_key_hash != EMPTY and cur_key_hash != DELETED) {//delete entry
			Entry* entry = read_book(entry_book, bookmarks[i]);
			delete[] entry->key;
			entry->key = NULL;
			//no need to free the entry, it isn't generally allocated
			delete[] entry->value;
			entry->value = NULL;
		}
	}
	delete[] cache->mem_arena;
	cache->mem_arena = NULL;
	entry_book->pages = NULL;
	delete cache;
}

int cache_set(Cache* cache, Key_ptr key, Value_ptr val, Index val_size) {
	Index key_size = get_key_size(key);
	const auto total_size = val_size + key_size;
	if(total_size > cache->mem_capacity) {
		// printf("Error in call to cache_set: Value exceeds max_mem, value was %d, max was %d", val_size, cache->mem_capacity);
		return -1;
	}
	const auto entry_capacity = cache->entry_capacity;
	const auto hash_table_capacity = get_hash_table_capacity(entry_capacity);
	const auto key_hashes = get_hashes(cache->mem_arena);
	const auto bookmarks = get_bookmarks(cache->mem_arena, cache->entry_capacity);
	auto entry_book = &cache->entry_book;
	auto evictor = &cache->evictor;

	const auto key_hash = get_hash(cache->hash(key));

	byte* val_copy = new byte[val_size];//we assume val_size is in bytes
	memcpy(val_copy, val, val_size);
	//check if key is in cache
	Index expected_i = key_hash%hash_table_capacity;
	Index step_size = get_step_size(key_hash);
	for(Index count = 0; count < hash_table_capacity; count += 1) {
		auto cur_key_hash = key_hashes[expected_i];
		if(cur_key_hash == EMPTY) {
			break;
		} else if(cur_key_hash == DELETED) {
			cache->dead_total -= 1;//we want to ressurect this entry
			break;
		} else if(cur_key_hash == key_hash) {
			auto bookmark = bookmarks[expected_i];
			Entry* entry = read_book(entry_book, bookmark);
			if(are_keys_equal(entry->key, key)) {//found key
				update_mem_size(cache, val_size - entry->value_size);
				//delete previous value
				delete[] entry->value;
				//add new value
				entry->value = val_copy;
				entry->value_size = val_size;
				touch_evict_item(evictor, bookmark, &entry->evict_item, entry_book);
				return 1;
			}
		}
		expected_i = (expected_i + step_size)%hash_table_capacity;
	}
	Index new_i = expected_i;

	//add key at new_i
	Key_ptr key_copy;
	{//copy key into key_copy
		byte* key_mem = new byte[key_size];
		memcpy(key_mem, key, key_size);
		key_copy = reinterpret_cast<Key_ptr>(key_mem);
	}
	//add new value
	update_mem_size(cache, total_size);
	cache->entry_total += 1;
	auto bookmark = alloc_book_page(entry_book);
	Entry* entry = read_book(entry_book, bookmark);

	entry->cur_i = new_i;
	entry->key = key_copy;
	entry->key_size = key_size;
	entry->value = val_copy;
	entry->value_size = val_size;
	add_evict_item(evictor, bookmark, &entry->evict_item, entry_book);

	key_hashes[new_i] = key_hash;
	bookmarks[new_i] = bookmark;
	if(is_exceeding_load(cache->entry_total, cache->dead_total, entry_capacity)) {
		grow_cache_size(cache);
	}
	return 0;
}

Value_ptr cache_get(Cache* cache, Key_ptr key, Index* ret_val_size) {
	const auto bookmarks = get_bookmarks(cache->mem_arena, cache->entry_capacity);
	const auto entry_book = &cache->entry_book;
	const auto evictor = &cache->evictor;

	Index i = find_entry(cache, key);
	if(i == KEY_NOT_FOUND) {
		return NULL;
	} else {
		auto bookmark = bookmarks[i];
		Entry* entry = read_book(entry_book, bookmark);
		//let the evictor know this value was accessed
		touch_evict_item(evictor, bookmark, &entry->evict_item, entry_book);
		*ret_val_size = entry->value_size;
		return static_cast<Value_ptr>(entry->value);
	}
}

int cache_delete(Cache* cache, Key_ptr key) {
	Index i = find_entry(cache, key);
	if(i != KEY_NOT_FOUND) {
		remove_entry(cache, i);
		return 0;
	}
	return -1;
}

Index cache_space_used(Cache* cache) {
	return cache->mem_total;
}
