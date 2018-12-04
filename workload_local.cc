//By Monica Moniot and Alyssa Riceman
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <ctime>
#include <math.h>
#include <string.h>
// #include <pthread.h>
#include "cache_server.hh"
#include "random.hh"

using uint = unsigned int;

constexpr double PI = 3.14159265358979323846;


// pthread_mutex_t threadCountMutex;
// volatile uint32_t threadNo = 1;


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




constexpr uint MAX_STRING_SIZE = 500;
constexpr uint SET_KEY_SIZE = 128;
constexpr uint KEY_SIZE = 8;
constexpr uint VALUE_SIZE = 16;

double total_summed_average = 0;
double worst_case_average = 0;
constexpr uint worst_case_averages_size = 1<<10;
double worst_case_averages[worst_case_averages_size] = {};
uint worst_case_i = 0;


bool workload(Cache* cache, uint requests_per_second, uint mean_string_size, uint std_string_size, uint total_requests) {
	mean_string_size = sqrt(mean_string_size);
	std_string_size = sqrt(std_string_size);
	char buffer[MAX_STRING_SIZE] = {};
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

	for(uint i = 0; i < total_requests; i += 1) {
		pre_sleep_time = clock();
		double r = pcg_random_uniform();
		if(r < .7) {//GET
			char* key = NULL;
			uint key_size;
			if(r/.7 < .9) {
				key = get_string_or_null(set_key, SET_KEY_SIZE);
				if(key) {
					key_size = KEY_SIZE;
				}
			}
			if(!key) {
				key_size = KEY_SIZE;
				key = buffer;
				generate_string(key, key_size);
			}

			// cache_get()
			pre_request_time = clock();
			uint value_size;
			cache_get(cache, key, key_size, &value_size);
			cur_request_time = clock();
			// if(value) {
			// delete[] (char*)key;
			// }
		} else if(r < 1) {//SET
			uint key_size = KEY_SIZE;

			char* key = new char[key_size];
			generate_string(key, key_size);

			uint value_size = VALUE_SIZE;
			auto value = new char[value_size];
			generate_string(value, value_size);

			pre_request_time = clock();
			cache_set(cache, key, key_size, value, value_size);
			cur_request_time = clock();
			add_string(set_key, SET_KEY_SIZE, key);
		} else {//DELETE
			// char* key = NULL;
			// uint key_size;
			// if((r - .8)/.2 < .1) {
			// 	key = get_string_or_null(set_key, SET_KEY_SIZE);
			// 	if(key) {
			// 		key_size = strlen(key) + 1;
			// 	}
			// }
			// if(!key) {
			// 	double r_size = random_normal(mean_string_size, std_string_size);
			// 	key_size = static_cast<uint>(r_size*r_size + 2);
			// 	key = buffer;
			// 	generate_string(key, key_size);
			// }
			// pre_request_time = clock();
			// cache_delete(cache, key, key_size);
			// cur_request_time = clock();
		}
		total_request_time += cur_request_time - pre_request_time;

		double request_time = 1e6*((double)(cur_request_time - pre_request_time))/CLOCKS_PER_SEC;
		if(worst_case_average < request_time) {
			for(;; worst_case_i = (worst_case_i + 1)%(worst_case_averages_size)) {
				auto w = worst_case_averages[worst_case_i];
				if(w < request_time) {
					worst_case_averages[worst_case_i] = request_time;
					worst_case_average = worst_case_average - w/(worst_case_averages_size) + request_time/(worst_case_averages_size);
					break;
				}
			}
		}

		cur_sleep_time = clock();
		int sleep_time = time_per_request - (cur_sleep_time - pre_sleep_time);
		if(sleep_time > 0) {
			total_sleep_time += sleep_time;
			uint nan = (1e9*(double)sleep_time/CLOCKS_PER_SEC);
			timespec t = {0, (long)nan};
			// nanosleep(&t, NULL);
		} else {
			overflow += 1;
		}
 	}

	double average_time = (((double)(total_request_time))/CLOCKS_PER_SEC)/total_requests;
	total_summed_average += average_time;
	bool is_valid = average_time < .001;

	// printf("Average time: %fus\n", 1e6*average_time);
	if(overflow > 0) {
		// printf("Request time took longer than desired %d times\n", overflow);
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

constexpr uint INITIAL_I = 1;
constexpr uint FINAL_I = 32;
int main() {
	auto cache = create_cache(1<<20);
	uint i = INITIAL_I;
	// while(true) {
		// i = 6;
		for(;i <= FINAL_I; i += 1) {
			uint j = pow(2, (float)i/4);
			// printf("Starting %d\n", j);
			bool is_valid = workload(cache, j, 25, 4, 100000);
			if(!is_valid) break;
			// sleep(1);
		}
	// }
	// printf("The highest number of request per second reached was %d\n", 1<<(i - 1));
	printf("Total average time: %fus\n", 1e6*total_summed_average/(FINAL_I - INITIAL_I + 1));
	printf("Worst case average: %fus\n", worst_case_average);
	destroy_cache(cache);
	return 0;
}
