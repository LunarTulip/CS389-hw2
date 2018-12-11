//By Monica Moniot and Alyssa Riceman
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctime>
#include <math.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include "cache.h"
#include "random.hh"

using uint = unsigned int;

constexpr double PI = 3.14159265358979323846;
constexpr uint32_t THREADPOOLSIZE = 1000;

pthread_mutex_t requestTimeMutex;
volatile clock_t totalRequestTime = 0;

// pthread_mutex_t threadCountMutex;
// volatile uint32_t threadNo = 1;

struct requestArgs {
	cache_type cache;
	key_type key;
	val_type value;
	uint32_t valueSize;
};

double _r_norm0;
double _r_norm1;
bool _has_no_norm = true;
void _random_normal(double* ret0, double* ret1) {
	double r0 = pcg_random_uniform_ex();
	double r1 = pcg_random_uniform_ex();
	*ret0 = sqrt(-2*log(r0))*cos(2*PI*r1);
	*ret1 = sqrt(-2*log(r1))*sin(2*PI*r0);
}
double random_normal(double mean = 0, double std = 1) {
	_has_no_norm = !_has_no_norm;
	if(_has_no_norm) {
		return mean + std*_r_norm1;
	} else {
		_random_normal(&_r_norm0, &_r_norm1);
		return mean + std*_r_norm0;
	}
}
uint min(uint a, uint b) {
	if(a < b) return a;
	return b;
}


void generate_string(char* buffer, uint size) {
	for(uint i = 0; i < size - 1; i += 1) {
		buffer[i] = pcg_random_bound(48, 126);
	}
	buffer[size - 1] = 0;
}

void add_string(char** strings, uint strings_size, char* new_string) {
	uint r = pcg_random_bound(0, strings_size - 1);
	if(strings[r]) {
		delete[] strings[r];
	}
	strings[r] = new_string;
}
char* get_string_or_null(char** strings, uint strings_size) {
	uint r = pcg_random_bound(0, strings_size - 1);
	return strings[r];
}




// constexpr uint MAX_STRING_SIZE = 500;
constexpr uint SET_KEY_SIZE = 100;

double get_network_latency(const char* ipAddress, uint16_t portNo, uint32_t iterations) {

	char* tinyMessage = new char[3];
	tinyMessage[0] = '\n';
	tinyMessage[1] = '\n';
	tinyMessage[2] = 0;

	char serverReply[64];

	clock_t before = clock();
	for(uint i = 0; i < iterations; i += 1) {
		int latencyCheckSocket = socket(AF_INET, SOCK_STREAM, 0);
		assert(latencyCheckSocket >= 0 && "Socket creation failed in latency check.");

		sockaddr_in serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(portNo);
		int addressSuccess = inet_pton(AF_INET, ipAddress, &serverAddress.sin_addr.s_addr);
		assert(addressSuccess == 1 && "Socket address set failed in latency check.");

		const sockaddr *addressPointer = reinterpret_cast<const sockaddr *>(&serverAddress);
		int connectionSuccess = connect(latencyCheckSocket, addressPointer, sizeof(serverAddress));
		assert(connectionSuccess == 0 && "Socket connection failed in latency check.");

		int sendSuccess = send(latencyCheckSocket, tinyMessage, 3, 0);
		assert(sendSuccess >= 0 && "Failed to ping server for latency.");

		int readSuccess = read(latencyCheckSocket, serverReply, 64);
		assert(readSuccess >= 0 && "Failed to read from server.");

		close(latencyCheckSocket);
	}
	clock_t after = clock();

	delete[] tinyMessage;

	return (((double)(after - before))/iterations)/CLOCKS_PER_SEC;
}

void* time_get(void* args) {
	requestArgs* argsAsStruct = static_cast<requestArgs*>(args);
	clock_t before = 0;
	clock_t after = 0;

	before = clock();
	const void* value = cache_get(argsAsStruct->cache, argsAsStruct->key, &(argsAsStruct->valueSize));
	after = clock();

	// pthread_mutex_lock(&threadCountMutex);
	// printf("Timed thread #%d\n", threadNo);
	// threadNo++;
	// pthread_mutex_unlock(&threadCountMutex);

	pthread_mutex_lock(&requestTimeMutex);
	totalRequestTime += (after - before);
	pthread_mutex_unlock(&requestTimeMutex);

	delete[] argsAsStruct->key;
	delete argsAsStruct;
	if(value) {
		delete[] static_cast<const char*>(value);
	}

	pthread_exit(NULL);
}

void* time_set(void* args) {
	requestArgs* argsAsStruct = static_cast<requestArgs*>(args);
	clock_t before = 0;
	clock_t after = 0;

	before = clock();
	cache_set(argsAsStruct->cache, argsAsStruct->key, argsAsStruct->value, argsAsStruct->valueSize);
	after = clock();

	// pthread_mutex_lock(&threadCountMutex);
	// printf("Timed thread #%d\n", threadNo);
	// threadNo++;
	// pthread_mutex_unlock(&threadCountMutex);

	pthread_mutex_lock(&requestTimeMutex);
	totalRequestTime += (after - before);
	pthread_mutex_unlock(&requestTimeMutex);

	delete[] argsAsStruct->key;
	delete[] static_cast<const char*>(argsAsStruct->value);
	delete argsAsStruct;

	pthread_exit(NULL);
}

// void* time_delete(void* args) {
// 	requestArgs* argsAsStruct = static_cast<requestArgs*>(args);
// 	clock_t before = 0;
// 	clock_t after = 0;

// 	before = clock();
// 	cache_delete(argsAsStruct->cache, argsAsStruct->key);
// 	after = clock();

// 	// pthread_mutex_lock(&threadCountMutex);
// 	// printf("Timed thread #%d\n", threadNo);
// 	// threadNo++;
// 	// pthread_mutex_unlock(&threadCountMutex);

// 	pthread_mutex_lock(&requestTimeMutex);
// 	totalRequestTime += (after - before);
// 	pthread_mutex_unlock(&requestTimeMutex);

// 	delete[] argsAsStruct->key;
// 	delete argsAsStruct;

// 	pthread_exit(NULL);
// }

bool workload(cache_obj* cache, uint requests_per_second, uint key_size, uint value_size, uint total_requests) {

	char* set_keys[SET_KEY_SIZE] = {};

	double averageLatency = 0; // get_network_latency("127.0.0.1", 33052, 1000);

	clock_t time_per_request = CLOCKS_PER_SEC/requests_per_second;
	clock_t pre_time = clock();
	clock_t total_sleep_time = 0;
	clock_t pre_sleep_time = pre_time;
	clock_t cur_sleep_time = 0;

	totalRequestTime = 0;

	uint overflow = 0;

	pthread_t* threads = new pthread_t[THREADPOOLSIZE];
	pthread_attr_t joinable;
	pthread_attr_init(&joinable);
	pthread_attr_setdetachstate(&joinable, PTHREAD_CREATE_JOINABLE);
	void* status = NULL;

	for(uint i = 0; i < total_requests; i += 1) {
		pre_sleep_time = clock();
		double r = pcg_random_uniform();
		if(r < .7) {//GET
			char* key = NULL;
			if(r/.7 < .9) {
				char* key_copy = get_string_or_null(set_keys, SET_KEY_SIZE);
				if(key_copy) {
					key = new char[key_size];
					memcpy(key, key_copy, key_size);
				}
			}
			if(!key) {
				key = new char[key_size];
				generate_string(key, key_size);
			}

			requestArgs* args = new requestArgs{cache, key, NULL, 0};
			if (i >= THREADPOOLSIZE) {
				pthread_join(threads[i % THREADPOOLSIZE], &status);
			}
			pthread_create(&(threads[i % THREADPOOLSIZE]), &joinable, time_get, static_cast<void*>(args));
		} else {//SET
			char* key = new char[key_size];
			generate_string(key, key_size);
			char* key_copy = new char[key_size];
			memcpy(key_copy, key, key_size);

			auto value = new char[value_size];
			generate_string(value, value_size);

			requestArgs* args = new requestArgs{cache, key, value, value_size};
			if (i >= THREADPOOLSIZE) {
				pthread_join(threads[i % THREADPOOLSIZE], &status);
			}
			pthread_create(&(threads[i % THREADPOOLSIZE]), &joinable, time_set, static_cast<void*>(args));
		}

		cur_sleep_time = clock();
		int sleep_time = time_per_request - (cur_sleep_time - pre_sleep_time);
		if(sleep_time > 0) {
			total_sleep_time += sleep_time;
			uint nan = (1e9*(double)sleep_time/CLOCKS_PER_SEC);
			timespec t = {0, (long)nan};
			nanosleep(&t, NULL);
		} else {
			overflow += 1;
		}
 	}
	for (uint32_t i = 0; i < THREADPOOLSIZE; i++) {
		pthread_join(threads[i], &status);
	}

	double average_time = ((((double)(totalRequestTime))/CLOCKS_PER_SEC)/total_requests) - averageLatency;
	bool is_valid = average_time < .001;

	printf("Average time: %fms\n", 1e3*average_time);
	if(overflow > 0) {
		printf("Request time was longer than desired %d times.\n", overflow);
	}
	// if(is_valid and overflow > total_requests/2) {
	// 	printf("More than half of the requests overflowed! The server is faster than the client.\n");
	// 	return false;
	// }

	delete[] threads;

	return is_valid;
}

int main() {
	auto cache = create_cache(0, NULL);
	uint i = 20;
	for(;; i += 1) {
		uint j = pow(2, (float)i/2);
		printf("Starting %d\n", j);
		bool is_valid = workload(cache, j, 8, 16, 500);
		// if(!is_valid) break;
		sleep(1);
	}
	printf("The highest number of requests per second with under 1ms mean response time was %d.\n", static_cast<uint32_t>(pow(2, static_cast<float>((i-1)/2))));
	destroy_cache(cache);
	return 0;
}
