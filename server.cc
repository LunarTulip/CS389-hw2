//By Monica Moniot and Alyssa Riceman
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>

using byte = uint8_t;//this must have the size of a unit of memory (a byte)
using uint64 = uint64_t;
using Cache = cache_obj;
using Key_ptr = key_type;
using Value_ptr = val_type;
using uint = index_type;
using Hash_func = hash_func;


constexpr uint DEFAULT_PORT = 33052;
constexpr uint DEFAULT_MAX_MEMORY = 1<<30;
constexpr uint MAX_MESSAGE_SIZE = 1<<10;
constexpr uint HIGH_BIT = 1<<(8*sizeof(uint) - 1);
constexpr uint MAX_MAX_MEMORY = ~HIGH_BIT;

const char* ACCEPTED    = "HTTP/1.1 200";
const char* CREATED     = "HTTP/1.1 201";
const char* BAD_REQUEST = "HTTP/1.1 400";
const char* TOO_LARGE   = "HTTP/1.1 413";
const char* NOT_ALLOWED = "HTTP/1.1 405";
const char* NOT_FOUND   = "HTTP/1.1 404";


constexpr uint64 str_to_uint(const char* const w, const uint size) {
	uint64 ret = 0;
	for(byte i = 0; i < size; i += 1) {
		auto c = w[i];
		ret = (ret<<8)|c;
	}
	return ret;
}

constexpr bool match_start(const char* const item, const uint item_size, const char* const w, const uint size) {
	auto ret = str_to_uint(w, size);
	if(item_size < size) return false;
	const uint64 word = *reinterpret_cast<const uint64*>(item);
	return word>>(8*(8 - size)) == ret;
}
constexpr uint get_item_size(const char* const w, const uint total_size) {
	uint i = 0;
	for(; i < total_size; i += 1) {
		auto c = w[i];
		if(c == '/' or c == 0) break;
	}
	return i;
}
inline void write_uint_to(char* buffer, uint i) {
	*reinterpret_cast<uint*>(buffer) = i;
}


int main(int argc, char** argv) {
	uint port = DEFAULT_PORT;
	uint max_mem = DEFAULT_MAX_MEMORY;
	{
		char* port_arg = NULL;
		char* max_mem_arg = NULL;
		opterr = 0;
		auto c = getopt(argc, argv, "mt:");
		while(c != -1) {
			printf("%d\n", c);
			switch (c) {
			case 'm':
				max_mem_arg = optarg;
				break;
			case 't':
				port_arg = optarg;
				break;
			case '?':
				if(optopt == 'm') {
					fprintf(stderr, "Option -%c requires a maxmem value.\n", optopt);
				} else if(optopt == 't') {
					fprintf(stderr, "Option -%c requires a port number.\n", optopt);
				} else if(isprint(optopt)) {
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				} else {
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
					return 1;
				}
			default:
				perror("bad args");
				return -1;
			}
			c = getopt(argc, argv, "abc:");
		}
		if(max_mem_arg) {
			auto user_max_mem = strtoul(max_mem_arg, NULL, 0);
			if(user_max_mem > 0 and user_max_mem <= MAX_MAX_MEMORY) {
				max_mem = user_max_mem;
				printf("1 %d\n", max_mem);
			} else {
				fprintf(stderr, "Option -m requires a valid memory size.\n");
			}
		}
		if(port_arg) {
			auto user_port = strtoul(port_arg, NULL, 0);
			if(user_port > 0 and user_port < 65535) {
				port = user_port;
				printf("2 %d\n", port);
			} else {
				fprintf(stderr, "Option -t requires a valid port no.\n");
			}
		}
	}

	uint64 server_fd;
	sockaddr* address;
	socklen_t* address_size;
	{
		// Creating socket file descriptor
		server_fd = socket(AF_INET, SOCK_STREAM, 0);
		if(server_fd == 0) {
			perror("socket failed");
			return -1;
		}

		// Forcefully attaching socket to the port
		uint opt = 1;
		bool failure = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
		if(failure) {
			perror("setsockopt");
			return -1;
		}

		sockaddr_in address_in;
		uint adress_in_size = sizeof(address_in);
		address_in.sin_family = AF_INET;
		address_in.sin_addr.s_addr = INADDR_ANY;
		address_in.sin_port = htons(port);

		address = reinterpret_cast<sockaddr*>(&address_in);
		adress_size = reinterpret_cast<socklen_t*>(&adress_in_size);

		// Forcefully attaching socket to the port
		auto error_code = bind(server_fd, address, sizeof(address_in));
		if(error_code < 0) {
			perror("bind failed");
			return -1;
		}
		error_code = listen(server_fd, 3);
		if(error_code < 0) {
			perror("listen");
			return -1;
		}
	}

	auto cache = create_cache(max_mem, NULL);//we could have written this to the stack to avoid compulsory cpu misses
	bool is_unset = true;

	//-----------------------
	//NOTE: BUFFER OVERFLOW DANGER, all writes to either buffer must be provably safe(can't overflow buffer)
	char message_buffer[MAX_MESSAGE_SIZE + 1];
	char full_buffer[MAX_MESSAGE_SIZE + strlen(ACCEPTED) + 2*sizeof(uint)];
	memcpy(full_buffer, ACCEPTED, strlen(ACCEPTED));
	full_buffer[strlen(ACCEPTED)] = ' ';
	char* buffer = &full_buffer[strlen(ACCEPTED) + 1];
	//-----------------------

	while(true) {
		uint new_socket = accept(server_fd, address, address_size);
		if(new_socket < 0) {
			perror("accept");
			return -1;
		}

		char* message = message_buffer;
		uint message_size = read(new_socket, message_buffer, MAX_MESSAGE_SIZE);
		const char* response = NULL;
		uint response_size = 0;

		bool is_bad_request = true;
		if(match_start(message, message_size, "GET ", 4)) {
			message = &message[4];
			message_size -= 4;
			if(match_start(message, message_size, "/key/", 5)) {
				message = &message[5];
				message_size -= 5;
				auto key = message;
				uint key_size = get_item_size(key, message_size);
				if(key_size > 0) {
					key[key_size] = 0;//--<--
					uint value_size;
					auto value = cache_get(cache, &buffer[sizeof(uint)], &value_size);
					if(value == NULL) {
						response = NOT_FOUND;
						response_size = strlen(NOT_FOUND);
					} else if(key_size + value_size >= MAX_MESSAGE_SIZE) {//shouldn't be possible
						response = TOO_LARGE;
						response_size = strlen(TOO_LARGE);
					} else {
						uint buffer_size = 0;
						write_uint_to(&buffer[buffer_size], key_size);
						buffer_size += sizeof(uint);
						memcpy(&buffer[buffer_size], key, key_size);
						buffer_size += key_size;

						write_uint_to(&buffer[buffer_size], value_size);
						buffer_size += sizeof(uint);
						memcpy(&buffer[buffer_size], value, value_size);
						buffer_size += value_size;

						response = full_buffer;
						response_size = buffer_size + strlen(ACCEPTED) + 1;
					}
					is_bad_request = false;
				}
			} else if(match_start(message, message_size, "/memsize", 8)) {
				message = &message[8];
				message_size -= 8;
				if(message_size == 0) {
					write_uint_to(buffer, cache_space_used(cache));
					response = full_buffer;
					response_size = sizeof(uint) + strlen(ACCEPTED) + 1;
					is_bad_request = false;
				}
			}
		} else if(match_start(message, message_size, "PUT ", 4)) {
			message = &message[4];
			message_size -= 4;
			if(match_start(message, message_size, "/key/", 5)) {
				message = &message[5];
				message_size -= 5;
				auto key = message;
				uint key_size = get_item_size(key, message_size);
				message = &message[key_size + 1];
				message_size -= key_size + 1;
				auto value = message;
				uint value_size = get_item_size(value, message_size);
				if(key_size > 0 and value_size > 0) {
					key[key_size] = 0;//--<--
					auto code = cache_set(cache, key, value, value_size);
					if(code < 0) {
						response = TOO_LARGE;
						response_size = strlen(TOO_LARGE);
					} else if(code > 0) {
						is_unset = false;
						response = CREATED;
						response_size = strlen(CREATED);
					} else {
						response = ACCEPTED;
						response_size = strlen(ACCEPTED);
					}
					is_bad_request = false;
				}
			}
		} else if(match_start(message, message_size, "DELETE ", 7)) {
			message = &message[7];
			message_size -= 7;
			if(match_start(message, message_size, "/key/", 5)) {
				message = &message[5];
				message_size -= 5;
				auto key = message;
				uint key_size = get_item_size(key, message_size);

				key[key_size] = 0;

				if(key_size > 0) {
					auto code = cache_delete(cache, key);
					if(code < 0) {
						response = NOT_FOUND;
						response_size = strlen(NOT_FOUND);
					} else {
						response = ACCEPTED;
						response_size = strlen(ACCEPTED);
					}
					is_bad_request = false;
				}
			}
		} else if(match_start(message, message_size, "HEAD ", 5)) {
			message = &message[5];
			message_size -= 5;
			if(match_start(message, message_size, "/key/", 5)) {
				// uint buffer_size = 0;
				// tm tm;
				// gmtime_r(time(0), &tm);
				// buffer_size += strftime(buffer, MAX_MESSAGE_SIZE - buffer_size, "Date: %a, %d %b %Y %H:%M:%S %Z ", &tm);
				//
				// response = ACCEPTED;
				// response_size = strlen(ACCEPTED);
				// is_bad_request = false;
			}
		} else if(match_start(message, message_size, "POST ", 5)) {//may break in here
			message = &message[5];
			message_size -= 5;
			if(match_start(message, message_size, "/shutdow", 8)) {
				message = &message[8];
				message_size -= 8;
				if(message_size == 1 and message[0] == 'n') {
					//-----------
					//BREAKS HERE
					send(new_socket, ACCEPTED, strlen(ACCEPTED), 0);
					break;
					//-----------
				}
			} else if(match_start(message, message_size, "/memsize", 8)) {
				message = &message[8];
				message_size -= 8;
				if(message_size == 1 + sizeof(uint) and message[0] == '/') {
					message = &message[1];
					message_size -= 1;
					uint new_max_mem = *reinterpret_cast<uint*>(message);
					if(is_unset and new_max_mem > 0 and new_max_mem <= MAX_MAX_MEMORY) {
						//Resetting the max_mem would be so so easy if it wasn't for the fixed api, now we have to delete the current cache just to reset it. What could have been the least expensive call for the entire server will now most likely be the most expensive.
						destroy_cache(cache);
						cache = create_cache(new_max_mem, NULL);
						response = ACCEPTED;
						response_size = strlen(ACCEPTED);
					} else {
						response = NOT_ALLOWED;
						response_size = strlen(NOT_ALLOWED);
					}
					is_bad_request = false;
				}
			}
		}
		if(is_bad_request) {
			response = BAD_REQUEST;
			response_size = strlen(BAD_REQUEST);
		}
		send(new_socket, response, response_size, 0);
	}

	//-----------------
	//PROGRAM EXITS HERE
	//this is the only exit point for the program
	//release the socket back to the os
	close(server_fd);
	//NOTE: uncomment if program no longer exits here
	// destroy_cache(cache);
	return 0;
	//-----------------
}
