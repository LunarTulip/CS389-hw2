//By Monica Moniot and Alyssa Riceman
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctime>
#include <math.h>
#include "cache.h"
#include "random.hh"

using uint = unsigned int;

constexpr double PI = 3.14159265358979323846;

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




constexpr uint MAX_STRING_SIZE = 1000;
constexpr uint SET_KEY_SIZE = 100;

double get_network_latency(cache_obj* cache, uint iterations) {
	clock_t pre_time = clock();

	for(uint i = 0; i < iterations; i += 1) {
		cache_space_used(cache);
 	}
	clock_t cur_time = clock();

	return (((double)(cur_time - pre_time))/iterations)/CLOCKS_PER_SEC;
}

bool workload(cache_obj* cache, uint requests_per_second, uint mean_string_size, uint std_string_size, uint total_requests) {
	mean_string_size = sqrt(mean_string_size);
	std_string_size = sqrt(std_string_size);
	char buffer[MAX_STRING_SIZE];
	char* set_key[SET_KEY_SIZE];

	double network_latency = get_network_latency(cache, 50);

	clock_t time_per_request = CLOCKS_PER_SEC/requests_per_second;
	clock_t pre_time = clock();
	clock_t origin_time = pre_time;

	uint overflow = 0;
	for(uint i = 0; i < total_requests; i += 1) {
		double r = pcg_random_uniform();
		if(r < .65) {//GET
			char* key;
			if(r/.65 < .6) {
				key = get_string_or_null(set_key, SET_KEY_SIZE);
			} else {
				double r_size = random_normal(mean_string_size, std_string_size);
				uint key_size = min(MAX_STRING_SIZE, static_cast<uint>(r_size*r_size + 2));
				generate_string(buffer, key_size);
				key = buffer;
			}
			uint value_size;
			const void* value = cache_get(cache, key, &value_size);
			if(value) {
				delete[] (char*)value;
			}
		} else if(r < .7) {//SET
			double r_size = random_normal(mean_string_size, std_string_size);
			uint key_size = static_cast<uint>(r_size*r_size + 2);
			char* key = new char[key_size];
			generate_string(key, key_size);

			double r_value_size = random_normal(mean_string_size, std_string_size);
			uint value_size = min(MAX_STRING_SIZE, static_cast<uint>(r_value_size*r_value_size + 2));
			auto value = &buffer[key_size];
			generate_string(value, value_size);

			cache_set(cache, key, value, value_size);
			add_string(set_key, SET_KEY_SIZE, key);
		} else {//DELETE
			char* key;
			if((r - .7)/.3 < .05) {
				key = get_string_or_null(set_key, SET_KEY_SIZE);
			} else {
				double r_size = random_normal(mean_string_size, std_string_size);
				uint key_size = min(MAX_STRING_SIZE, static_cast<uint>(r_size*r_size + 2));
				generate_string(buffer, key_size);
				key = buffer;
			}
			cache_delete(cache, buffer);
		}

		clock_t cur_time = clock();
		uint d = (cur_time - pre_time);
		int sleep_time = time_per_request - d;
		if(sleep_time > 0) {
			usleep((1000000*sleep_time)/CLOCKS_PER_SEC);
		} else {
			overflow += 1;
		}
		pre_time = cur_time;
 	}
	clock_t cur_time = clock();
	bool is_valid = ((((double)(cur_time - origin_time))/CLOCKS_PER_SEC)/total_requests - network_latency) < .001;

	if(overflow > 0) {
		printf("Request time took longer than desired %d times\n", overflow);
	}
	if(is_valid and overflow > total_requests/2) {
		printf("More than half of the requests overflowed! The server is faster than the client\n");
		return false;
	}
	return is_valid;
}

// const uint total_requests = 10000;
// const uint mean_string_size = 20;
// const uint std_string_size = 3;
// const uint requests_per_second = 3;

int main() {
	auto cache = create_cache(0, NULL);
	uint i = 4;
	for(;; i += 1) {
		bool is_valid = workload(cache, 1<<i, 25, 4, 100);
		if(!is_valid) break;
	}
	printf("The highest number of request per second reached was %d\n", 1<<(i - 1));
	destroy_cache(cache);
	return 0;
}