
CPP = g++-7

cache.o:
	$(CPP) -c cache.h cache.cpp;

eviction.o:
	$(CPP) -c eviction.h eviction.cpp;

cache:
	$(CPP) -g types.h book.h cache.cc eviction.cc -o test tests.cc;
	gdb ./test;

cache_server:
	$(CPP) -O3 types.h book.h cache.cc eviction.cc -o server server.cc;
	./server;

cache_client:
	$(CPP) -g cache.h -o client client.cc;
	./client;

cache_test:
	$(CPP) -g types.h book.h client.cc eviction.cc -o test tests.cc;
	./test;

workload:
	$(CPP) -O3 cache.h client.cc workload.cc random.hh -lpthread -o benchmark;
	./benchmark;

hw6server:
	$(CPP) -O3 types.hh book.hh cache_server.hh cache.cc server.cc -o server -g;
	./server;

hw6benchmark:
	$(CPP) -O3 cache_server.hh client.cc workload.cc random.hh -lpthread -o benchmark;
	./benchmark;

clean:
	rm -f *.o; rm -f *.h.gch; rm test
