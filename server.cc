//By Monica Moniot and Alyssa Riceman
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <poll.h>
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

const char* ACCEPTED    = "HTTP/1.1 200\n\n";
const char* CREATED     = "HTTP/1.1 201\n\n";
const char* BAD_REQUEST = "HTTP/1.1 400\n\n";
const char* TOO_LARGE   = "HTTP/1.1 413\n\n";
const char* NOT_ALLOWED = "HTTP/1.1 405\n\n";
const char* NOT_FOUND   = "HTTP/1.1 404\n\n";
const uint HEADER_SIZE = strlen(ACCEPTED);
const char* REQUEST_TYPE = "Content-Type: text/plain\n";
const char* RESPONSE_TYPE = "Accept: text/plain\n";


constexpr bool match_start(const char* const item, const uint item_size, const char* const w, const uint size) {
	if(item_size < size) return false;
	for(uint i = 0; i < size; i += 1) {
		if(item[i] != w[i]) return false;
	}
	return true;
}
constexpr uint get_item_size(const char* const w, const uint total_size) {
	uint i = 0;
	for(; i < total_size; i += 1) {
		auto c = w[i];
		if(c == '/' or c == '\n') break;
		// if(c == 0) return 0;
	}
	return i;
}
inline void write_uint_to(char* buffer, uint i) {
	*reinterpret_cast<uint*>(buffer) = i;
}
inline uint read_uint_from(char* buffer) {
	return *reinterpret_cast<uint*>(buffer);
}


struct Socket {
	uint64 file_desc;
	sockaddr* address;
	socklen_t* address_size;
};
int create_socket(Socket* open_socket, uint p, uint port) {
	// Creating socket file descriptor
	uint64 server_fd = socket(AF_INET, p, 0);
	if(server_fd == 0) {
		printf("failed to create socket\n");
		return -1;
	}

	uint opt = 1;
	bool failure = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
	if(failure) {
		printf("failed to set socket options\n");
		return -1;
	}

	sockaddr_in address_in;
	uint address_in_size = sizeof(address_in);
	address_in.sin_family = AF_INET;
	address_in.sin_addr.s_addr = INADDR_ANY;
	address_in.sin_port = htons(port);

	sockaddr* address = reinterpret_cast<sockaddr*>(&address_in);
	socklen_t* address_size = reinterpret_cast<socklen_t*>(&address_in_size);

	// Forcefully attaching socket to the port
	auto error_code = bind(server_fd, address, sizeof(address_in));
	if(error_code < 0) {
		printf("failed to bind socket\n");
		return -1;
	}
	if(p != SOCK_DGRAM) {
		error_code = listen(server_fd, 3);
		if(error_code < 0) {
			printf("failed to listen\n");
			return -1;
		}
	}
	open_socket->file_desc = server_fd;
	open_socket->address = address;
	open_socket->address_size = address_size;
	return 0;
}


int main(int argc, char** argv) {
	uint port = DEFAULT_PORT;
	uint max_mem = DEFAULT_MAX_MEMORY;
	{
		uint user_port = 0;
		uint user_max_mem = 0;
		opterr = 0;
		int opt;

		bool is_max = false;
		bool is_port = false;
		while((opt = getopt(argc, argv, "m:t:")) != -1) {
			switch(opt) {
				case 'm':
					user_max_mem = atoi(optarg);
					is_max = true;
					break;
				case 't':
					user_port = atoi(optarg);
					is_port = true;
					break;
				default:
					fprintf(stderr, "Usage: %s [-t port_no] [-m max_mem]\n", argv[0]);
					return -1;
			}
		}
		if(is_max) {
			if(user_max_mem > 0 and user_max_mem <= MAX_MAX_MEMORY) {
				max_mem = user_max_mem;
			} else {
				fprintf(stderr, "Option -m requires a valid memory size.\n");
				return -1;
			}
		}
		if(is_port) {
			if(user_port > 0 and user_port < 65535) {
				port = user_port;
			} else {
				fprintf(stderr, "Option -t requires a valid port no.\n");
				return -1;
			}
		}
	}

	Socket tcp_socket;
	Socket udp_socket;
	if(create_socket(&udp_socket, SOCK_DGRAM, port) != 0) {
		return -1;
	}
	if(create_socket(&tcp_socket, SOCK_STREAM, port) != 0) {
		return -1;
	}
	uint file_desc_size = 2;
	pollfd file_descs[2];
	memset(file_descs, 0, 2*sizeof(pollfd));
	pollfd* tcp_fd = &file_descs[1];
	tcp_fd->fd = tcp_socket.file_desc;
	tcp_fd->events = POLLIN;
	tcp_fd->revents = 0;
	pollfd* udp_fd = &file_descs[0];
	udp_fd->fd = udp_socket.file_desc;
	udp_fd->events = POLLIN;
	udp_fd->revents = 0;


	auto cache = create_cache(max_mem, NULL);//we could have written this to the stack to avoid compulsory cpu misses
	// printf("%d\n", max_mem);
	bool is_unset = true;

	//-----------------------
	//NOTE: BUFFER OVERFLOW DANGER, all writes to either buffer must be provably safe(can't overflow buffer)
	char message_buffer[MAX_MESSAGE_SIZE + 1];
	char full_buffer[MAX_MESSAGE_SIZE + HEADER_SIZE + 2*sizeof(uint)];
	memcpy(full_buffer, ACCEPTED, HEADER_SIZE);
	char* buffer = &full_buffer[HEADER_SIZE];
	//-----------------------

	uint request_total = 0;
	while(true) {
		request_total += 1;
		Socket open_socket;
		// printf("starting poll #%d\n", request_total);
		poll(file_descs, file_desc_size, -1);
		bool is_udp = false;
		if(tcp_fd->revents == POLLIN) {
			open_socket = tcp_socket;
			// printf("response on tcp");
		} else if(udp_fd->revents == POLLIN) {
			open_socket = udp_socket;
			is_udp = true;
			// printf("response on udp");
		} else if(udp_fd->revents == POLLERR) {
			printf("udp POLLERR\n");
			return -1;
		} else if(tcp_fd->revents == POLLERR) {
			printf("tcp POLLERR\n");
			return -1;
		} else if(udp_fd->revents == POLLNVAL) {
			printf("udp POLLNVAL\n");
			// create_socket(&udp_socket, SOCK_DGRAM, port);
			// udp_fd->fd = udp_socket.file_desc;
			// udp_fd->events = POLLIN;
			// udp_fd->revents = 0;
			return -1;
		} else if(tcp_fd->revents == POLLNVAL) {
			printf("tcp POLLNVAL\n");
			return -1;
		} else if(udp_fd->revents == POLLHUP) {
			printf("udp POLLHUP\n");
			return -1;
		} else if(tcp_fd->revents == POLLHUP) {
			printf("tcp POLLHUP\n");
			return -1;
		} else {
			printf("Error: tcp was: %d, udp was: %d\n", tcp_fd->revents, udp_fd->revents);
			return -1;
		}

		uint new_socket = accept(open_socket.file_desc, open_socket.address, open_socket.address_size);
		if(new_socket <= 0) {
			perror("accept failure");
			return -1;
		}

		char* message = message_buffer;
		uint message_size = read(new_socket, message_buffer, MAX_MESSAGE_SIZE);
		const char* response = NULL;
		uint response_size = 0;

		// printf("---REQUEST:\n%.*s\n---\n", message_size, message);

		if(message_size >= MAX_MESSAGE_SIZE) {
			response = TOO_LARGE;
			response_size = HEADER_SIZE;
		} else {
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
						auto value = cache_get(cache, key, &value_size);
						if(value == NULL) {
							response = NOT_FOUND;
							response_size = HEADER_SIZE;
						} else if(key_size + value_size >= MAX_MESSAGE_SIZE) {//shouldn't be possible
						response = TOO_LARGE;
						response_size = HEADER_SIZE;
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
							response_size = buffer_size + HEADER_SIZE;
							// printf("value found; was: \"%.*s\"\n", value_size, (const char*)value);
						}
						is_bad_request = false;
					}
				} else if(match_start(message, message_size, "/memsize", 8)) {
					auto i = cache_space_used(cache);
					write_uint_to(buffer, i);
					response = full_buffer;
					response_size = sizeof(uint) + HEADER_SIZE;
					is_bad_request = false;
					// printf("memsize requested; is: \"%d\"\n", i);
				}
			}
			if(!is_udp) {
				if(match_start(message, message_size, "PUT ", 4)) {
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
								response_size = HEADER_SIZE;
							} else if(code == 0) {
								is_unset = false;
								response = CREATED;
								response_size = HEADER_SIZE;
							} else {
								response = ACCEPTED;
								response_size = HEADER_SIZE;
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
								response_size = HEADER_SIZE;
							} else {
								response = ACCEPTED;
								response_size = HEADER_SIZE;
							}
							is_bad_request = false;
						}
					}
				} else if(match_start(message, message_size, "HEAD ", 5)) {
					message = &message[5];
					message_size -= 5;
					if(match_start(message, message_size, "/key/", 5)) {
						uint buffer_size = 0;
						tm tm;
						auto t = time(0);
						gmtime_r(&t, &tm);
						buffer_size += strftime(&buffer[buffer_size], MAX_MESSAGE_SIZE - buffer_size, "Date: %a, %d %b %Y %H:%M:%S %Z\n", &tm);
						memcpy(&buffer[buffer_size], RESPONSE_TYPE, strlen(RESPONSE_TYPE));
						buffer_size += strlen(RESPONSE_TYPE);
						memcpy(&buffer[buffer_size], REQUEST_TYPE, strlen(REQUEST_TYPE));

						response = full_buffer;
						response_size = buffer_size + HEADER_SIZE;
						is_bad_request = false;
					}
				} else if(match_start(message, message_size, "POST ", 5)) {//may break in here
					message = &message[5];
					message_size -= 5;
					if(match_start(message, message_size, "/shutdown", 9)) {
						message = &message[9];
						message_size -= 9;
						//-----------
						//BREAKS HERE
						// printf("%s\n---\n", ACCEPTED);
						send(new_socket, ACCEPTED, HEADER_SIZE, 0);
						break;
						//-----------
					} else if(match_start(message, message_size, "/memsize/", 9)) {
						message = &message[9];
						message_size -= 9;
						if(message_size >= sizeof(uint)) {
							uint new_max_mem = *reinterpret_cast<uint*>(message);
							if(is_unset and new_max_mem > 0 and new_max_mem <= MAX_MAX_MEMORY) {
								//Resetting the max_mem would be so so easy if it wasn't for the fixed api, now we have to delete the current cache just to reset it. What could have been the least expensive call for the entire server will now most likely be very expensive.
								destroy_cache(cache);
								cache = create_cache(new_max_mem, NULL);
								response = ACCEPTED;
								response_size = HEADER_SIZE;
							} else {
								response = NOT_ALLOWED;
								response_size = HEADER_SIZE;
							}
							is_bad_request = false;
						}
					}
				}
			}

			if(is_bad_request) {
				response = BAD_REQUEST;
				response_size = HEADER_SIZE;
			}
		}

		// printf("---RESPONSE:\n%d-%.*s\n---\n", response_size - HEADER_SIZE, response_size, response);

		send(new_socket, response, response_size, 0);
		close(new_socket);
	}

	//-----------------
	//PROGRAM EXITS HERE
	//this is the only exit point for the program
	//release the socket back to the os
	close(tcp_socket.file_desc);
	close(udp_socket.file_desc);
	//NOTE: uncomment if program no longer exits here
	// destroy_cache(cache);
	return 0;
	//-----------------
}
