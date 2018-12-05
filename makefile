
CPP = g++-7

hw6server:
	$(CPP) -O3 types.hh book.hh cache_server.hh cache.cc server.cc -lpthread -o server;
	./server;

hw6benchmark:
	$(CPP) -O3 cache.h client.cc workload.cc random.hh -lpthread -o benchmark;
	./benchmark;

clean:
	rm -f *.o; rm -f *.h.gch; rm test
