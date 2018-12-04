//By Monica Moniot and Alyssa Riceman
#include <cstring>
#include <stdio.h>
#include "types.hh"
#include "book.hh"
#include "eviction.hh"
#include "cache_server.hh"

constexpr Index INIT_HASH_TABLE_CAPACITY = 1<<7;//must be a power of 2

constexpr Index EMPTY = 0;
constexpr Index HIGH_BIT = 1<<(8*sizeof(Index) - 1);
constexpr Index HASH_MULTIPLIER = 2654435769;
constexpr char NULL_TERMINATOR = 0;


uint64 total_searches = 0;
uint64 total_path_length = 0;
uint64 total_worst_case_searches = 0;
uint64 total_worst_case_path_length = 0;
uint64 worst_path_length = 0;
uint64 false_hash_hits = 0;
uint64 true_hash_hits = 0;
double total_summed_load = 0;
double total_worst_case_summed_load = 0;
uint64 total_added = 0;
uint64 total_deleted = 0;
uint64 total_evictions = 0;


constexpr inline Index hash(const byte* item, Index size) {
	//we want to flag entries by setting their key_hash to EMPTY
	//we need to modify the hash so that it can't equal EMPT
	//if we don't then everything will break
	//generates a hash of a c string
	//We are using David Knuth's multiplicative hash algorithm
	// uint32* sections = cast<uint32*>(item);
	// uint top = size&0b11;
	// uint rem = size&(~0b11);
	// uint end = size>>2;
	Index hash = 0;
	// Index hash = size*HASH_MULTIPLIER;
	// for(uint i = 0; i < end; i += 1) {
	// 	hash = (hash*HASH_MULTIPLIER) + sections[i];
	// }
	for(uint i = 0; i < size; i += 1) {
		hash = (hash*HASH_MULTIPLIER) + item[i];
	}
	if(hash == EMPTY) {
		hash = size|HIGH_BIT;
	}
	// assert(hash != EMPTY);
	return hash;
}
constexpr inline bool is_mem_equal(const byte* data0, Index size0, const byte* data1, Index size1) {
	if(size0 != size1) return false;
	for(uint i = 0; i < size0; i += 1) {
		if(data0[i] != data1[i]) return false;
	}
	return true;
}


constexpr inline Index get_entry_capacity(Index hash_table_capacity) {
	//instead of storing the capactiy of the hash table, we calculate it jit
	return hash_table_capacity/2;
}
constexpr inline bool is_exceeding_load(Index entry_total, Index hash_table_capacity) {
	//when returns true triggers a table resizing
	//we would seg-fault if entry_total exceeds entry_capacity
	Index entry_capacity = get_entry_capacity(hash_table_capacity);
	return (entry_total + 1 >= entry_capacity);
}

//instead of storing pointers to our tables, we calculate them jit
constexpr inline Index* get_hashes    (byte* mem_arena) {
	//hashes is part of the hash table
	//in order to traverse the hash table, we traverse hashes
	//the hash marks if an entry is empty, deleted or populated
	return cast<Index*>(mem_arena);
}
constexpr inline Index* get_bookmarks (byte* mem_arena, Index hash_table_capacity) {
	//bookmarks is part of the hash table
	//it stores the index of the page in the book connected to the hash table entry
	return cast<Index*>(mem_arena + sizeof(Index)*hash_table_capacity);
}
constexpr inline Page*  get_pages     (byte* mem_arena, Index hash_table_capacity) {
	//pages stores the primary data structure of Book
	return cast<Page*>(mem_arena + 2*sizeof(Index)*hash_table_capacity);
}

inline byte* allocate(Index hash_table_capacity) {
	//we allocate all of our dynamic memory right here
	//we do a joint allocation of everything for many reasons:
	//we have to manage almost no memory with a joint allocation
	//a joint allocation is faster than many separate ones
	//a joint allocation greatly improves locality
	//we don't have to store pointers to every data structure
	//a jointly allocated block is easily serializable
	const auto hash_table_size = (sizeof(Bookmark) + sizeof(Index))*hash_table_capacity;
	const auto book_size = sizeof(Page)*get_entry_capacity(hash_table_capacity);
	byte* mem_arena = malloc<byte>(hash_table_size + book_size);
	//make sure all entries are marked as EMPTY, so they can be populated
	memset(get_hashes(mem_arena), EMPTY, sizeof(Index)*hash_table_capacity);
	return mem_arena;
}

void collect_stats(Cache* cache, Index path_length) {
	total_path_length += path_length;
	if(path_length > 1) {
		total_worst_case_searches += 1;
		total_worst_case_summed_load += cast<double>(cache->entry_total)/(cache->hash_table_capacity);
		total_worst_case_path_length += path_length;
		if(worst_path_length < path_length) {
			worst_path_length = path_length;
		}
	}
}

constexpr Index KEY_NOT_FOUND = -1;
inline Index find_entry(Cache* cache, const byte* key, Index key_size) {
	//gets the hash table index associated to key
	const auto hash_table_capacity = cache->hash_table_capacity;
	const auto key_hashes = get_hashes(cache->mem_arena);
	const auto bookmarks = get_bookmarks(cache->mem_arena, hash_table_capacity);
	const auto key_hash = hash(key, key_size);
	const auto entry_book = &cache->entry_book;
	//check if key is in cache
	total_searches += 1;
	total_summed_load += cast<double>(cache->entry_total)/hash_table_capacity;
	Index capacity_bitmask = hash_table_capacity - 1;
	Index expected_i = key_hash&capacity_bitmask;
	for(Index path_length = 1; path_length <= hash_table_capacity; path_length += 1) {
		auto cur_key_hash = key_hashes[expected_i];
		if(cur_key_hash == EMPTY) {
			collect_stats(cache, path_length);
			return KEY_NOT_FOUND;
		} else if(cur_key_hash == key_hash) {
			Entry* entry = read_book(entry_book, bookmarks[expected_i]);
			if(is_mem_equal(entry->key, entry->key_size, key, key_size)) {//found key
				true_hash_hits += 1;
				collect_stats(cache, path_length);
				return expected_i;
			} else {
				false_hash_hits += 1;
			}
		}
		expected_i = (expected_i + 1)&capacity_bitmask;
	}
	printf("Error when attempting to find entry in cache: Full table traversal; index was %d, key was %s, size was %d\n", expected_i, key, hash_table_capacity);
	return KEY_NOT_FOUND;
}

inline void remove_entry(Cache* cache, Index i) {
	//removes an entry to our cache, including from the hash table
	//this is the only code that removes entries;
	//it handles everything necessary for removing an entry
	const auto hash_table_capacity = cache->hash_table_capacity;
	const auto key_hashes = get_hashes(cache->mem_arena);
	const auto bookmarks = get_bookmarks(cache->mem_arena, cache->hash_table_capacity);
	const auto entry_book = &cache->entry_book;
	const auto evictor = &cache->evictor;

	auto bookmark = bookmarks[i];
	Entry* entry = read_book(entry_book, bookmark);

	free(entry->key);
	entry->key = NULL;
	cache->entry_total -= 1;

	cache->mem_total -= entry->value_size + entry->key_size;
	free(entry->value);
	entry->value = NULL;
	remove_evict_item(evictor, bookmark, &entry->evict_item, entry_book);
	free_book_page(entry_book, bookmark);

	Index capacity_bitmask = hash_table_capacity - 1;
	Index removed_i = i;
	Index next_i = (removed_i + 1)&capacity_bitmask;
	for(;;) {
		auto cur_key_hash = key_hashes[next_i];
		if(cur_key_hash == EMPTY) {
			break;
		} else {
			Index first_i = cur_key_hash&capacity_bitmask;
			if((removed_i <= next_i) ? (removed_i < first_i and first_i <= next_i) : (removed_i < first_i or first_i <= next_i)) {
				//skip
			} else {
				key_hashes[removed_i] = cur_key_hash;
				bookmarks[removed_i] = bookmarks[next_i];
				Entry* entry = read_book(entry_book, bookmarks[next_i]);
				entry->cur_i = removed_i;
				removed_i = next_i;
			}
		}
		next_i = (next_i + 1)&capacity_bitmask;
	}
	key_hashes[removed_i] = EMPTY;
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
		total_evictions += 1;
	}
}
inline void grow_cache_size(Cache* cache) {
	// printf("growing cache now\n");
	const auto pre_table_capacity = cache->hash_table_capacity;
	const auto new_table_capacity = 2*pre_table_capacity;

	const auto pre_mem_arena = cache->mem_arena;
	const auto pre_key_hashes = get_hashes(pre_mem_arena);
	const auto pre_bookmarks = get_bookmarks(pre_mem_arena, pre_table_capacity);
	const auto pre_pages = get_pages(pre_mem_arena, pre_table_capacity);
	const auto entry_book = &cache->entry_book;

	auto new_mem_arena = allocate(new_table_capacity);
	cache->mem_arena = new_mem_arena;
	cache->hash_table_capacity = new_table_capacity;

	const auto new_key_hashes = get_hashes(new_mem_arena);
	const auto new_bookmarks = get_bookmarks(new_mem_arena, new_table_capacity);
	const auto new_pages = get_pages(new_mem_arena, new_table_capacity);

	memcpy(new_pages, pre_pages, sizeof(Page)*get_entry_capacity(pre_table_capacity));
	entry_book->pages = new_pages;

	//rehash our entries back into the new table
	Index capacity_bitmask = new_table_capacity - 1;
	auto entries_left = cache->entry_total;

	// printf("edsd: %d, %d, %d, %d\n", entries_left, new_table_capacity, get_entry_capacity(new_table_capacity), cache->entry_total);
	for(Index j = 0; entries_left > 0; j += 1) {
		auto key_hash = pre_key_hashes[j];
		if(key_hash != EMPTY) {
			entries_left -= 1;
			//find empty index
			Index expected_i = key_hash&capacity_bitmask;
			while(true) {
				auto cur_key_hash = new_key_hashes[expected_i];
				if(cur_key_hash == EMPTY) {
					break;
				}
				expected_i = (expected_i + 1)&capacity_bitmask;
			}
			//write to new entry it's new location
			auto bookmark = pre_bookmarks[j];
			Entry* entry = read_book(entry_book, bookmark);
			// printf("grow: %d, %d, %d, %d, %d, %p, %p\n", expected_i, j, entries_left, bookmark, key_hash, entry_book, entry);
			entry->cur_i = expected_i;

			new_key_hashes[expected_i] = key_hash;
			new_bookmarks[expected_i] = bookmark;
		}
	}

	free(pre_mem_arena);
}


Cache* create_cache(Index max_mem) {
	Cache* cache = malloc<Cache>(1);
	Index hash_table_capacity = INIT_HASH_TABLE_CAPACITY;
	cache->mem_capacity = max_mem;
	cache->mem_total = 0;
	cache->hash_table_capacity = hash_table_capacity;
	cache->entry_total = 0;
	auto mem_arena = allocate(hash_table_capacity);
	cache->mem_arena = mem_arena;
	create_book(&cache->entry_book, get_pages(mem_arena, hash_table_capacity));
	create_evictor(&cache->evictor);
	return cache;
}
void destroy_cache(Cache* cache) {
	const auto hash_table_capacity = cache->hash_table_capacity;
	const auto key_hashes = get_hashes(cache->mem_arena);
	const auto bookmarks = get_bookmarks(cache->mem_arena, hash_table_capacity);
	const auto entry_book = &cache->entry_book;

	//remove every entry
	for(Index i = 0; i < hash_table_capacity; i += 1) {
		auto cur_key_hash = key_hashes[i];
		if(cur_key_hash != EMPTY) {//delete entry
			Entry* entry = read_book(entry_book, bookmarks[i]);
			free(entry->key);
			entry->key = NULL;
			//no need to free the entry, it isn't generally allocated
			free(entry->value);
			entry->value = NULL;
		}
	}
	free(cache->mem_arena);
	cache->mem_arena = NULL;
	entry_book->pages = NULL;
	printf("total searches:      %ld\n", total_searches);
	printf("average path length: %f\n", cast<double>(total_path_length)/total_searches);
	printf("total worst case searches:      %ld\n", total_worst_case_searches);
	printf("average worst case path length: %f\n", cast<double>(total_worst_case_path_length)/total_worst_case_searches);
	printf("average load:      %f\n", total_summed_load/total_searches);
	printf("worst case load:   %f\n", total_worst_case_summed_load/total_worst_case_searches);
	printf("worst path length: %ld\n", worst_path_length);
	printf("false hash hits:   %ld\n", false_hash_hits);
	printf("true hash hits:    %ld\n", true_hash_hits);
	printf("total added:     %ld\n", total_added);
	printf("total deleted:   %ld\n", total_deleted);
	printf("total evictions: %ld\n", total_evictions);
	printf("final capacity: %d\n", hash_table_capacity);
	printf("final count:    %d\n", cache->entry_total);
	printf("final load:     %f\n", cast<double>(cache->entry_total)/hash_table_capacity);
	printf("final usage:    %d\n", cache->mem_total);
	free(cache);
}

int cache_set(Cache* cache, const byte* key, Index key_size, const byte* value, Index value_size) {
	const auto total_size = value_size + key_size;
	if(total_size > cache->mem_capacity) {
		// printf("Error in call to cache_set: Value exceeds max_mem, value was %d, max was %d", value_size, cache->mem_capacity);
		return -1;
	}
	const auto hash_table_capacity = cache->hash_table_capacity;
	const auto key_hashes = get_hashes(cache->mem_arena);
	const auto bookmarks = get_bookmarks(cache->mem_arena, hash_table_capacity);
	auto entry_book = &cache->entry_book;
	auto evictor = &cache->evictor;

	const auto key_hash = hash(key, key_size);

	byte* value_copy = malloc<byte>(value_size);//we assume value_size is in bytes
	memcpy(value_copy, value, value_size);
	//check if key is in cache
	total_searches += 1;
	total_summed_load += cast<double>(cache->entry_total)/hash_table_capacity;
	Index capacity_bitmask = hash_table_capacity - 1;
	Index expected_i = key_hash&capacity_bitmask;
	for(Index path_length = 1; path_length <= hash_table_capacity; path_length += 1) {
		auto cur_key_hash = key_hashes[expected_i];
		if(cur_key_hash == EMPTY) {
			collect_stats(cache, path_length);
			break;
		} else if(cur_key_hash == key_hash) {
			auto bookmark = bookmarks[expected_i];
			Entry* entry = read_book(entry_book, bookmark);
			if(is_mem_equal(entry->key, entry->key_size, key, key_size)) {//found key
				true_hash_hits += 1;
				update_mem_size(cache, value_size - entry->value_size);
				//delete previous value
				free(entry->value);
				//add new value
				entry->value = value_copy;
				entry->value_size = value_size;
				touch_evict_item(evictor, bookmark, &entry->evict_item, entry_book);
				collect_stats(cache, path_length);
				return 1;
			} else {
				false_hash_hits += 1;
			}
		}
		expected_i = (expected_i + 1)&capacity_bitmask;
	}
	Index new_i = expected_i;
	total_added += 1;

	//add key at new_i
	byte* key_copy = malloc<byte>(key_size);
	memcpy(key_copy, key, key_size);

	//add new value
	update_mem_size(cache, total_size);
	cache->entry_total += 1;

	auto bookmark = alloc_book_page(entry_book);
	Entry* entry = read_book(entry_book, bookmark);

	// printf("setting cache now\n");
	// printf("%d, %d, %d, %d, %d, %d, %p, %p\n", new_i, cache->entry_total, bookmark, key_hash, hash_table_capacity, get_entry_capacity(hash_table_capacity), entry_book, entry);
	entry->cur_i = new_i;
	entry->key = key_copy;
	entry->key_size = key_size;
	entry->value = value_copy;
	entry->value_size = value_size;
	add_evict_item(evictor, bookmark, &entry->evict_item, entry_book);

	key_hashes[new_i] = key_hash;
	bookmarks[new_i] = bookmark;
	if(is_exceeding_load(cache->entry_total, hash_table_capacity)) {
		grow_cache_size(cache);
	}
	return 0;
}

const byte* cache_get(Cache* cache, const byte* key, Index key_size, Index* ret_value_size) {
	const auto bookmarks = get_bookmarks(cache->mem_arena, cache->hash_table_capacity);
	const auto entry_book = &cache->entry_book;
	const auto evictor = &cache->evictor;

	Index i = find_entry(cache, key, key_size);
	if(i == KEY_NOT_FOUND) {
		return NULL;
	} else {
		auto bookmark = bookmarks[i];
		Entry* entry = read_book(entry_book, bookmark);
		//let the evictor know this value was accessed
		touch_evict_item(evictor, bookmark, &entry->evict_item, entry_book);
		*ret_value_size = entry->value_size;
		return cast<byte*>(entry->value);
	}
}

int cache_delete(Cache* cache, const byte* key, Index key_size) {
	Index i = find_entry(cache, key, key_size);
	if(i != KEY_NOT_FOUND) {
		total_deleted += 1;
		remove_entry(cache, i);
		return 0;
	}
	return -1;
}

Index cache_space_used(Cache* cache) {
	return cache->mem_total;
}
