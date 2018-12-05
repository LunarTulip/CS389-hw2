//By Monica Moniot and Alyssa Riceman
#ifndef EVICTION_H
#define EVICTION_H
#include "cache_server.hh"
#include "book.hh"
#include "types.hh"
constexpr Bookmark INVALID_NODE = -1;

inline Node* get_node(Book* book, Bookmark item_i) {
	return &read_book(book, item_i)->evict_item;
}

inline void remove   (DLL* list, Bookmark item_i, Node* node, Book* book) {
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
inline void append   (DLL* list, Bookmark item_i, Node* node, Book* book) {
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
inline void prepend  (DLL* list, Bookmark item_i, Node* node, Book* book) {
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
inline void set_last (DLL* list, Bookmark item_i, Node* node, Book* book) {
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
inline void set_first(DLL* list, Bookmark item_i, Node* node, Book* book) {
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


//only supporting LRU
inline void create_evictor(Evictor* evictor) {
	evictor->list.head = INVALID_NODE;
}

inline void add_evict_item    (Evictor* evictor, Bookmark item_i, Evict_item* item, Book* book) {
	pthread_mutex_lock(&evictor->mutex);
	append(&evictor->list, item_i, item, book);
	pthread_mutex_unlock(&evictor->mutex);
}
inline void remove_evict_item (Evictor* evictor, Bookmark item_i, Evict_item* item, Book* book) {
	pthread_mutex_lock(&evictor->mutex);
	remove(&evictor->list, item_i, item, book);
	pthread_mutex_unlock(&evictor->mutex);
}
inline void touch_evict_item  (Evictor* evictor, Bookmark item_i, Evict_item* item, Book* book) {
	pthread_mutex_lock(&evictor->mutex);
	set_last(&evictor->list, item_i, item, book);
	pthread_mutex_unlock(&evictor->mutex);
}
inline Bookmark get_evict_item(Evictor* evictor, Book* book) {
	auto list = &evictor->list;
	Bookmark item_i = list->head;
	pthread_mutex_lock(&evictor->mutex);
	remove(list, item_i, get_node(book, item_i), book);
	pthread_mutex_unlock(&evictor->mutex);
	return item_i;
}

#endif
