//By Monica Moniot and Alyssa Riceman
#include "cache_server.hh"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cassert>
#include <ctype.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>


constexpr uint DEFAULT_PORT = 33052;
constexpr uint DEFAULT_MAX_MEMORY = 1<<18;
constexpr uint MAX_MESSAGE_SIZE = 1<<10;
constexpr uint HIGH_BIT = 1<<(8*sizeof(uint) - 1);
constexpr uint MAX_MAX_MEMORY = ~HIGH_BIT;

constexpr const char* ACCEPTED    = "HTTP/1.1 200\n\n";
constexpr const char* CREATED     = "HTTP/1.1 201\n\n";
constexpr const char* BAD_REQUEST = "HTTP/1.1 400\n\n";
constexpr const char* TOO_LARGE   = "HTTP/1.1 413\n\n";
constexpr const char* NOT_ALLOWED = "HTTP/1.1 405\n\n";
constexpr const char* NOT_FOUND   = "HTTP/1.1 404\n\n";
constexpr uint HEADER_SIZE = strlen(ACCEPTED);
constexpr const char* REQUEST_TYPE = "Content-Type: text/plain\n";
constexpr const char* RESPONSE_TYPE = "Accept: text/plain\n";

struct threadArgs {
	uint socket;
	bool isUdp;
};

volatile bool destroying = false;

volatile uint32_t threadCount = 0;
pthread_mutex_t threadCountMutex;

Cache _cache;
Cache *cache = &_cache;

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
	sockaddr_in address;
	socklen_t address_size;
};

Socket tcp_socket;
Socket udp_socket;

int create_socket(Socket* open_socket, uint protocol, uint port) {
	// Creating socket file descriptor
	int64 server_fd = socket(AF_INET, protocol, 0);
	if(server_fd <= 0) {
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
	// uint address_in_size = ;
	address_in.sin_family = AF_INET;
	address_in.sin_addr.s_addr = INADDR_ANY;
	address_in.sin_port = htons(port);

	sockaddr* address = cast<sockaddr*>(&address_in);
	// socklen_t* address_size = reinterpret_cast<socklen_t*>(&address_in_size);

	// Forcefully attaching socket to the port
	auto error_code = bind(server_fd, address, sizeof(address_in));
	if(error_code < 0) {
		printf("failed to bind socket\n");
		return -1;
	}
	if(protocol != SOCK_DGRAM) {
		error_code = listen(server_fd, 3);
		if(error_code < 0) {
			printf("failed to listen\n");
			return -1;
		}
	}
	open_socket->file_desc = server_fd;
	open_socket->address = address_in;
	open_socket->address_size = sizeof(sockaddr_in);
	return 0;
}

uint startSocket(uint16_t portNum, const char* ipAddress) {
    int newSocket = socket(AF_INET, SOCK_STREAM, 0);
    assert(newSocket >= 0 && "Socket creation failed.");

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNum);
    int addressSuccess = inet_pton(AF_INET, ipAddress, &serverAddress.sin_addr.s_addr);
    assert(addressSuccess == 1 && "Socket address set failed.");

    const sockaddr* addressPointer = reinterpret_cast<const sockaddr *>(&serverAddress);
    int connectionSuccess = connect(newSocket, addressPointer, sizeof(serverAddress));
    assert(connectionSuccess == 0 && "Socket connection failed.");

    return newSocket;
}

void* serverThread(void* args) {
	// printf("---RESPONDING\n");
	pthread_mutex_lock(&threadCountMutex);
	threadCount++;
	// printf("answer #%d\n", threadCount);
	pthread_mutex_unlock(&threadCountMutex);

	int32 socket = static_cast<int32>(reinterpret_cast<int64>(args));

	bool is_unset = true;
	char message_buffer[MAX_MESSAGE_SIZE + 1];
	char full_buffer[MAX_MESSAGE_SIZE + HEADER_SIZE + 2*sizeof(uint)];
	memcpy(full_buffer, ACCEPTED, HEADER_SIZE);
	char* buffer = &full_buffer[HEADER_SIZE];
	char* message = message_buffer;

	int message_size = recv(socket, message, MAX_MESSAGE_SIZE, 0);

	const char* response = NULL;
	uint response_size = 0;
	if(message_size < 0) {
		printf("failure to read\n");
		close(socket);
		pthread_mutex_lock(&threadCountMutex);
		threadCount--;
		// printf("fin #%d\n", threadCount);
		pthread_mutex_unlock(&threadCountMutex);
		pthread_exit(NULL);
		// return 0;
	}

	// printf("---REQUEST: %d\n%.*s\n---\n", message_size, message_size, message);

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
					auto value = cache_get(cache, key, key_size, &value_size);
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
					auto code = cache_set(cache, key, key_size, value, value_size);
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
					auto code = cache_delete(cache, key, key_size);
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
				response = ACCEPTED;
				response_size = HEADER_SIZE;
				destroying = true;
				uint socketToBreakMainLoop = startSocket(DEFAULT_PORT, "127.0.0.1");
				close(socketToBreakMainLoop);
				// break;
				//-----------
			} else if(match_start(message, message_size, "/memsize/", 9)) {
				message = &message[9];
				message_size -= 9;
				if(message_size >= sizeof(uint)) {
					uint new_max_mem = *reinterpret_cast<uint*>(message);
					if(is_unset and new_max_mem > 0 and new_max_mem <= MAX_MAX_MEMORY) {
						//Resetting the max_mem would be so so easy if it wasn't for the fixed api, now we have to delete the current cache just to reset it. What could have been the least expensive call for the entire server will now most likely be very expensive.
						destroy_cache(cache);
						create_cache(cache, new_max_mem);
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

		if(is_bad_request) {
			response = BAD_REQUEST;
			response_size = HEADER_SIZE;
		}
	}

	// printf("---RESPONSE:\n%d-%.*s\n---\n", response_size - HEADER_SIZE, response_size, response);

	if(send(socket, response, response_size, 0) < 0) {
		printf("failure to send socket\n");
	}
	if(close(socket) < 0) {
		printf("failure to close socket\n");
	}

	pthread_mutex_lock(&threadCountMutex);
	threadCount--;
	// printf("fin #%d\n", threadCount);
	pthread_mutex_unlock(&threadCountMutex);
	pthread_exit(NULL);
	// return 0;
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

	create_cache(cache, max_mem);

	Socket tcp_socket;
	if(create_socket(&tcp_socket, SOCK_STREAM, port) != 0) {
		return -1;
	}

	uint request_total = 0;
	while(!destroying) {
		request_total += 1;
		// printf("starting poll #%d #%d\n", request_total, threadCount);
		while (threadCount > 1000) {}//prevent more than 1000 file descriptors from being openned at one time
		int32 new_socket = accept(tcp_socket.file_desc, NULL, NULL);
		// printf("threadCount: #%d\n", threadCount);

		if(destroying) {
			close(new_socket);
			break;
		}
		if(new_socket <= 0) {
			printf("accept failure");
			break;
		}

		pthread_t thread;
		auto error_code = pthread_create(&thread, NULL, serverThread, reinterpret_cast<void*>(new_socket));
		if(error_code != 0) {
			printf("failure to create thread, errorno: %d\n", error_code);
			break;
		}
		pthread_detach(thread);
		// serverThread(reinterpret_cast<void*>(new_socket));
	}
	while (threadCount > 0) {}
	//-----------------
	//PROGRAM EXITS HERE
	//this is the only exit point for the program
	//release the socket back to the os
	close(tcp_socket.file_desc);
	//NOTE: uncomment if program no longer exits here
	// destroy_cache(cache);
	return 0;
	//-----------------
}
