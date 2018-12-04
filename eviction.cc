//By Monica Moniot and Alyssa Riceman
#include <stdlib.h>
#include "eviction.hh"
#include "book.hh"
#include "types.hh"

constexpr Bookmark INVALID_NODE = -1;

inline Evict_item* get_evict_item(Book* book, Bookmark item_i) {
	return &read_book(book, item_i)->evict_item;
}

Node* get_node(Book* book, Bookmark item_i) {
	return &get_evict_item(book, item_i)->node;
}
void remove   (DLL* list, Bookmark item_i, Node* node, Book* book) {
	auto next_i = node->next;
	auto pre_i = node->pre;
	if(list->head == item_i) {
		if(next_i == item_i) {//all items have been removed
			list->head = INVALID_NODE;
			return;
		}
		list->head = next_i;
	}
	get_node(book, pre_i)->next = next_i;
	get_node(book, next_i)->pre = pre_i;
}
void append   (DLL* list, Bookmark item_i, Node* node, Book* book) {
	auto head = list->head;
	if(head == INVALID_NODE) {
		list->head = item_i;
		node->next = item_i;
		node->pre = item_i;
	} else {
		Node* head_node = get_node(book, head);
		auto last = head_node->pre;
		Node* last_node = get_node(book, last);
		last_node->next = item_i;
		head_node->pre = item_i;
		node->next = head;
		node->pre = last;
	}
}
void prepend  (DLL* list, Bookmark item_i, Node* node, Book* book) {
	auto head = list->head;
	list->head = item_i;
	if(head == INVALID_NODE) {
		node->next = item_i;
		node->pre = item_i;
	} else {
		Node* head_node = get_node(book, head);
		auto first = head_node->next;
		Node* first_node = get_node(book, first);
		first_node->pre = item_i;
		head_node->next = item_i;
		node->next = first;
		node->pre = head;
	}
}
void set_last (DLL* list, Bookmark item_i, Node* node, Book* book) {
	auto head = list->head;
	auto head_node = get_node(book, head);

	auto last = head_node->pre;
	auto last_node = get_node(book, last);
	auto next_i = node->next;
	auto pre_i = node->pre;
	if(item_i == head) {
		list->head = head_node->next;
		return;
	} else if(item_i == last) {
		return;
	}

	last_node->next = item_i;
	head_node->pre = item_i;
	node->next = head;
	node->pre = last;

	get_node(book, pre_i)->next = next_i;
	get_node(book, next_i)->pre = pre_i;
}
void set_first(DLL* list, Bookmark item_i, Node* node, Book* book) {
	auto head = list->head;
	auto head_node = get_node(book, head);
	list->head = item_i;
	if(item_i == head) {
		return;
	}

	auto first = head_node->next;
	auto first_node = get_node(book, first);
	auto next_i = node->next;
	auto pre_i = node->pre;

	first_node->pre = item_i;
	head_node->next = item_i;
	node->next = first;
	node->pre = head;

	get_node(book, pre_i)->next = next_i;
	get_node(book, next_i)->pre = pre_i;
}


void create_evictor(Evictor* evictor, evictor_type policy) {
	evictor->policy = policy;
	if(policy == FIFO or policy == LIFO or policy == LRU or policy == MRU or policy == CLOCK) {
		auto list = &evictor->data.list;
		list->head = INVALID_NODE;
	} else if(policy == SLRU) {
		auto dlist = &evictor->data.dlist;
		auto protect = &evictor->data.dlist.protect;
		auto prohibate = &evictor->data.dlist.prohibate;
		protect->head = INVALID_NODE;
		prohibate->head = INVALID_NODE;
		dlist->pp_delta = 0;
	} else {//RANDOM
		evictor->data.rand_data.total_items = 0;
	}
}

void add_evict_item    (Evictor* evictor, Bookmark item_i, Evict_item* item, Book* book) {
	//item was created
	//we must init "item"
	auto policy = evictor->policy;
	if(policy == FIFO or policy == LRU) {
		auto node = &item->node;
		append(&evictor->data.list, item_i, node, book);
	} else if(policy == LIFO or policy == MRU) {
		auto node = &item->node;
		prepend(&evictor->data.list, item_i, node, book);
	} else if(policy == CLOCK) {
		auto node = &item->node;
		node->rf_bit = false;
		append(&evictor->data.list, item_i, node, book);
	} else if(policy == SLRU) {
		auto dlist = &evictor->data.dlist;
		auto prohibate = &evictor->data.dlist.prohibate;
		auto node = &item->node;
		node->rf_bit = false;
		dlist->pp_delta += 1;
		append(prohibate, item_i, node, book);
	} else {//RANDOM
		auto data = &evictor->data.rand_data;
		auto rand_items = static_cast<Bookmark*>(evictor->mem_arena);
		item->rand_i = data->total_items;
		rand_items[data->total_items] = item_i;
		data->total_items += 1;
	}
}
void remove_evict_item (Evictor* evictor, Bookmark item_i, Evict_item* item, Book* book) {
	//item was removed
	auto policy = evictor->policy;
	if(policy == FIFO or policy == LIFO or policy == LRU or policy == MRU or policy == CLOCK) {
		auto node = &item->node;
		remove(&evictor->data.list, item_i, node, book);
	} else if(policy == SLRU) {
		auto dlist = &evictor->data.dlist;
		auto protect = &evictor->data.dlist.protect;
		auto prohibate = &evictor->data.dlist.prohibate;
		auto node = &item->node;
		if(node->rf_bit) {
			dlist->pp_delta += 1;
			remove(protect, item_i, node, book);
		} else {
			if(dlist->pp_delta == 0) {//evict from protected
				auto p_item = protect->head;
				auto p_node = get_node(book, p_item);
				remove(protect, p_item, p_node, book);
				append(prohibate, p_item, p_node, book);
				dlist->pp_delta += 1;
			} else {
				dlist->pp_delta -= 1;
			}
			remove(prohibate, item_i, node, book);
		}
	} else {//RANDOM
		auto data = &evictor->data.rand_data;
		auto rand_items = static_cast<Bookmark*>(evictor->mem_arena);
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
void touch_evict_item  (Evictor* evictor, Bookmark item_i, Evict_item* item, Book* book) {
	//item was touched
	auto policy = evictor->policy;
	if(policy == FIFO or policy == LIFO) {

	} else if(policy == LRU) {
		auto node = &item->node;
		set_last(&evictor->data.list, item_i, node, book);
	} else if(policy == MRU) {
		auto node = &item->node;
		set_first(&evictor->data.list, item_i, node, book);
	} else if(policy == CLOCK) {
		auto node = &item->node;
		node->rf_bit = true;
		set_last(&evictor->data.list, item_i, node, book);
	} else if(policy == SLRU) {
		auto dlist = &evictor->data.dlist;
		auto protect = &evictor->data.dlist.protect;
		auto prohibate = &evictor->data.dlist.prohibate;
		auto node = &item->node;
		if(node->rf_bit) {
			set_last(protect, item_i, node, book);
		} else {
			node->rf_bit = true;
			remove(prohibate, item_i, node, book);
			append(protect, item_i, node, book);
			if(dlist->pp_delta <= 1) {//evict from protected
				auto p_item = protect->head;
				auto p_node = get_node(book, p_item);
				remove(protect, p_item, p_node, book);
				append(prohibate, p_item, p_node, book);
			} else {
				dlist->pp_delta -= 2;
			}
		}
	} else {//RANDOM

	}
}
Bookmark get_evict_item(Evictor* evictor, Book* book) {
	//return item to evict
	auto policy = evictor->policy;
	Bookmark item_i = 0;
	if(policy == FIFO or policy == LIFO or policy == LRU or policy == MRU) {
		auto list = &evictor->data.list;
		item_i = list->head;
		remove(list, item_i, get_node(book, item_i), book);
	} else if(policy == CLOCK) {
		auto list = &evictor->data.list;
		item_i = list->head;
		auto node = get_node(book, item_i);
		while(node->rf_bit) {
			node->rf_bit = false;
			item_i = get_node(book, item_i)->next;
			list->head = item_i;
		}
		remove(list, item_i, node, book);
	} else if(policy == SLRU) {
		auto dlist = &evictor->data.dlist;
		auto protect = &evictor->data.dlist.protect;
		auto prohibate = &evictor->data.dlist.prohibate;
		item_i = prohibate->head;
		if(dlist->pp_delta == 0) {//evict from protected
			auto p_item = protect->head;
			auto p_node = get_node(book, p_item);
			remove(protect, p_item, p_node, book);
			append(prohibate, p_item, p_node, book);
			dlist->pp_delta += 1;
		} else {
			dlist->pp_delta -= 1;
		}
		auto node = get_node(book, item_i);
		remove(prohibate, item_i, node, book);
	} else {//RANDOM
		auto data = &evictor->data.rand_data;
		auto rand_items = static_cast<Bookmark*>(evictor->mem_arena);
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
