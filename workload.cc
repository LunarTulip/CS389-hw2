//By Monica Moniot and Alyssa Riceman
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctime>
#include <math.h>
#include <string.h>
// #include <pthread.h>
#include "cache.h"
#include "random.hh"

using uint = unsigned int;

constexpr double PI = 3.14159265358979323846;

pthread_mutex_t requestTimeMutex;
volatile clock_t totalRequestTime = 0;

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
		buffer[i] = pcg_random_bound(33, 126);
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

double get_network_latency(cache_obj* cache, uint iterations) {
	clock_t pre_time = clock();

	for(uint i = 0; i < iterations; i += 1) {
		cache_space_used(cache);
 	}
	clock_t cur_time = clock();

	return (((double)(cur_time - pre_time))/iterations)/CLOCKS_PER_SEC;
}

void* time_get(void* args) {
	requestArgs* argsAsStruct = static_cast<requestArgs*>(args);
	clock_t before = 0;
	clock_t after = 0;
	before = clock();
	const void* value = cache_get(argsAsStruct->cache, argsAsStruct->key, &(argsAsStruct->valueSize));
	after = clock();

	pthread_mutex_lock(&requestTimeMutex);
	totalRequestTime += (after - before);
	pthread_mutex_unlock(&requestTimeMutex);

	delete[] argsAsStruct->key;
	delete[] argsAsStruct;
	if(value) {
		delete[] static_cast<const char*>(value);
	}

	pthread_exit(NULL);
}

bool workload(cache_obj* cache, uint requests_per_second, uint mean_string_size, uint std_string_size, uint total_requests) {
	mean_string_size = sqrt(mean_string_size);
	std_string_size = sqrt(std_string_size);
	// char buffer[MAX_STRING_SIZE] = {};
	char* set_key[SET_KEY_SIZE] = {};

	// double network_latency = get_network_latency(cache, 20);

	clock_t time_per_request = CLOCKS_PER_SEC/requests_per_second;
	clock_t pre_time = clock();
	clock_t total_sleep_time = 0;
	clock_t total_request_time = 0;
	clock_t pre_sleep_time = pre_time;
	clock_t cur_sleep_time = 0;
	clock_t pre_request_time = 0;
	clock_t cur_request_time = 0;

	uint overflow = 0;

	pthread_t threads[total_requests];
	for(uint i = 0; i < total_requests; i += 1) {
		pre_sleep_time = clock();
		double r = pcg_random_uniform();
		if(r < .65) {//GET
			char* key = NULL;
			if(r/.65 < .6) {
				char* key_copy = get_string_or_null(set_key, SET_KEY_SIZE);
				if(key_copy) {
					uint key_size = strlen(key_copy) + 1;
					key = new char[key_size];
					memcpy(key, key_copy, key_size);
				}
			}
			if(!key) {
				double r_size = random_normal(mean_string_size, std_string_size);
				uint key_size = static_cast<uint>(r_size*r_size + 2);
				key = new char[key_size];
				generate_string(key, key_size);
			}
			uint value_size;
			requestArgs args = {cache, key, NULL, value_size};
			int threadSuccess = pthread_create(&(threads[i]), NULL, time_get, static_cast<void *>(&args));
			// pre_request_time = clock();
			// const void* value = cache_get(cache, key, &value_size);
			// cur_request_time = clock();
			// if(value) {
			// 	delete[] (char*)value;
			// }
		} else if(r < .8) {//SET
			double r_size = random_normal(mean_string_size, std_string_size);
			uint key_size = static_cast<uint>(r_size*r_size + 2);

			char* key = new char[key_size];
			generate_string(key, key_size);
			char* key_copy = new char[key_size];
			memcpy(key_copy, key, key_size);

			double r_value_size = random_normal(mean_string_size, std_string_size);
			uint value_size = static_cast<uint>(r_value_size*r_value_size + 2);
			auto value = new char[value_size];
			generate_string(value, value_size);

			pre_request_time = clock();
			cache_set(cache, key, value, value_size);
			cur_request_time = clock();
			add_string(set_key, SET_KEY_SIZE, key_copy);
		} else {//DELETE
			char* key = NULL;
			if(r/.65 < .6) {
				char* key_copy = get_string_or_null(set_key, SET_KEY_SIZE);
				if(key_copy) {
					uint key_size = strlen(key_copy) + 1;
					key = new char[key_size];
					memcpy(key, key_copy, key_size);
				}
			}
			if(!key) {
				double r_size = random_normal(mean_string_size, std_string_size);
				uint key_size = static_cast<uint>(r_size*r_size + 2);
				key = new char[key_size];
				generate_string(key, key_size);
			}
			pre_request_time = clock();
			cache_delete(cache, key);
			cur_request_time = clock();
		}
		total_request_time += cur_request_time - pre_request_time;

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
	double average_time = (((double)(total_request_time))/CLOCKS_PER_SEC)/total_requests;
	bool is_valid = average_time < .001;

	printf("Average time: %fms\n", 1e3*average_time);
	if(overflow > 0) {
		printf("Request time took longer than desired %d times\n", overflow);
	}
	// if(is_valid and overflow > total_requests/2) {
	// 	printf("More than half of the requests overflowed! The server is faster than the client\n");
	// 	return false;
	// }
	return is_valid;
}

// const uint total_requests = 10000;
// const uint mean_string_size = 20;
// const uint std_string_size = 3;
// const uint requests_per_second = 3;

int main() {
	auto cache = create_cache(0, NULL);
	uint i = 10;
	// while(true) {
		// i = 6;
		for(;i < 26; i += 1) {
			printf("Starting %d\n", 1<<i);
			bool is_valid = workload(cache, 1<<i, 250, 4, 10000);
			if(!is_valid) break;
			sleep(1);
		}
	// }
	printf("The highest number of request per second reached was %d\n", 1<<(i - 1));
	// destroy_cache(cache);
	return 0;
}
