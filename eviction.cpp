//By Monica Moniot and Alyssa Riceman
#include <stdlib.h>
#include <cstring>
#include "eviction.h"
#include "book.h"
#include "types.h"

constexpr Bookmark INVALID_NODE = -1;

inline Evict_item* get_evict_item(Book* book, Bookmark item_i) {
	return &read_book(book, item_i)->evict_item;
}

Node* get_node(Book* book, Bookmark item_i) {
	return &get_evict_item(book, item_i)->node;
}
void remove  (DLL* list, Bookmark item_i, Node* node, Book* book) {
	auto next_i = node->next;
	auto pre_i = node->pre;

	if(pre_i != INVALID_NODE) {
		get_node(book, pre_i)->next = next_i;
	}
	if(next_i != INVALID_NODE) {
		get_node(book, next_i)->pre = pre_i;
	}

	if(list->first == item_i) {
		list->first = next_i;
	}
	if(list->last == item_i) {
		list->last = pre_i;
	}
}
void append  (DLL* list, Bookmark item_i, Node* node, Book* book) {
	auto last = list->last;
	if(last == INVALID_NODE) {
		list->first = item_i;
	} else {
		Node* last_node = get_node(book, last);
		last_node->next = item_i;
	}
	node->pre = last;
	node->next = INVALID_NODE;
	list->last = item_i;
}
void set_last(DLL* list, Bookmark item_i, Node* node, Book* book) {
	auto last = list->first;
	if(last == list->first or item_i == list->last) {
		return;
	} else if(item_i == list->first) {
		list->first = last;
	}

	auto last_node = get_node(book, last);
	auto next_i = node->next;
	auto pre_i = node->pre;
	auto pre_i1 = last_node->pre;

	last_node->next = next_i;
	last_node->pre = pre_i;
	if(pre_i != INVALID_NODE) {
		get_node(book, pre_i)->next = last;
	}
	if(next_i != INVALID_NODE) {
		get_node(book, next_i)->pre = last;
	}

	node->next = INVALID_NODE;
	node->pre = pre_i1;
	if(pre_i1 != INVALID_NODE) {
		get_node(book, pre_i1)->next = item_i;
	}
}


void create_evictor(Evictor* evictor, evictor_type policy, void* mem_arena) {
	evictor->policy = policy;
	if(policy == FIFO) {
		auto list = &evictor->data.list;
		list->last = INVALID_NODE;
		list->first = INVALID_NODE;
	} else if(policy == LRU) {
		auto list = &evictor->data.list;
		list->last = INVALID_NODE;
		list->first = INVALID_NODE;
	} else {//RANDOM
		evictor->data.rand_data.total_items = 0;
	}
}

Bookmark evict_item(Evictor* evictor, Book* book, void* mem_arena) {
	//return item to evict
	auto policy = evictor->policy;
	Bookmark item_i = 0;
	if(policy == FIFO) {
		auto list = &evictor->data.list;
		item_i = list->first;
		remove(list, item_i, get_node(book, item_i), book);
	} else if(policy == LRU) {
		auto list = &evictor->data.list;
		item_i = list->first;
		remove(list, item_i, get_node(book, item_i), book);
	} else {//RANDOM
		auto data = &evictor->data.rand_data;
		auto rand_items = static_cast<Bookmark*>(mem_arena);
		auto rand_i0 = rand()%data->total_items;
		item_i = rand_items[rand_i0];
		//We need to delete rand_i0 from rand_items in place
		//this requires us to relink some data objects
		auto rand_i1 = data->total_items - 1;
		data->total_items = rand_i1;
		auto item_i1 = get_evict_item(book, rand_items[rand_i1]);
		rand_items[rand_i0] = rand_items[rand_i1];
		item_i1->rand_i = rand_i0;
	}
	return item_i;
}
void add_item      (Evictor* evictor, Bookmark item_i, Evict_item* item, Book* book, void* mem_arena) {
	//item was created
	//we must init "item"
	auto policy = evictor->policy;
	if(policy == FIFO) {
		auto node = &item->node;
		append(&evictor->data.list, item_i, node, book);
	} else if(policy == LRU) {
		auto node = &item->node;
		append(&evictor->data.list, item_i, node, book);
	} else {//RANDOM
		auto data = &evictor->data.rand_data;
		auto rand_items = static_cast<Bookmark*>(mem_arena);
		item->rand_i = data->total_items;
		rand_items[data->total_items] = item_i;
		data->total_items += 1;
	}
}
void remove_item   (Evictor* evictor, Bookmark item_i, Evict_item* item, Book* book, void* mem_arena) {
	//item was removed
	auto policy = evictor->policy;
	if(policy == FIFO) {
		auto node = &item->node;
		remove(&evictor->data.list, item_i, node, book);
	} else if(policy == LRU) {
		auto node = &item->node;
		remove(&evictor->data.list, item_i, node, book);
	} else {//RANDOM
		auto data = &evictor->data.rand_data;
		auto rand_items = static_cast<Bookmark*>(mem_arena);
		//We need to delete from rand_items in place
		//this requires us to relink some data objects
		Bookmark rand_i0 = item->rand_i;
		auto rand_i1 = data->total_items - 1;
		data->total_items = rand_i1;
		auto item_i1 = get_evict_item(book, rand_items[rand_i1]);
		rand_items[rand_i0] = rand_items[rand_i1];
		item_i1->rand_i = rand_i0;
	}
}
void touch_item    (Evictor* evictor, Bookmark item_i, Evict_item* item, Book* book, void* mem_arena) {
	//item was touched
	auto policy = evictor->policy;
	if(policy == FIFO) {

	} else if(policy == LRU) {
		auto node = &item->node;
		set_last(&evictor->data.list, item_i, node, book);
	} else {//RANDOM

	}
}
