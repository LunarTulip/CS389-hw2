# Computer Systems Homework 5 (by Alyssa Riceman and Monica Moniot)

Over the course of this assignment we performed a series of optimizations, as well as restructurings of our code to allow us to better measure its performance or otherwise improve comprehensibility. Sadly, we couldn't get a profiler such as perf or gprof working, so we were forced to rely on code-internal measurements, which worked to some extent for as long as our server was single-threaded but broke down somewhat once it became multithreaded. Thus we have minimal hard quantitative measurements.

Our overall change in throughput was larger than we can measure; as of our changes, the server remains stably under 1ms in response time even when requests are sent literally as quickly as our CPUs can manage. Thus we don't have an exact measurement, but we know that it was a *very substantial* improvement.

## Optimization 1: Refactored header and code
We switched the cache over to using character arrays instead of c-strings, and the cache header was changed to allow for the creation of the cache structure anywhere in memory. Custom types for keys and values were also removed for clarity. These changes made it significantly easier to implement our subsequent optimizations.

## Optimization 2: Baked LRU (Effect not measured)
LRU was made the cache's standard eviction policy and all others were removed. The program no longer does any last-minute decision-making to decide which eviction policy to use. The entire eviction handler code was simplified and condensed into a single file.

## Metric Improvement 1: Added cache debug metrics
Added numerous internal metrics on the agorithmic performance of the cache,including but not limited to average hashtable traversal length, total false hash matches, worst case path length, and total deleted entries.

## Optimization 3: Fixed hash function (Noticeable improvement to performance)
We immediately noticed with the debug information that the cache was getting a large number of hash collisions between different keys. This was bad because our cache relies on the key hashes, not only to distribute entries in the hash-table, but also to quickly determine if two keys are equal, since if they have different hashes, they can't be equal. We dramatically improved our default hash function simply by changing its order of operation, and false hash matches dropped to 0, meaning the cache never noticed two different keys with the same hash.

## Optimization 4: Implemented fast modulus (Minor improvement to performance)
Since the cache's hash-table grows in powers of two, we were able implement modulus by the hash table's size as a bitmask in place of using modulo.

## Optimization 5: Tried linear traversal instead of double hashing (Unnoticeable)
The cache originally used double hashing for its traversal pattern, but we tried switching it over to using linear traversal. Surprisingly, the change made little to no difference in performance. Since linear traversal is simplier, has better multithreading characteristics, and has better algorithmic characteristics in general, we chose to continue using it anyway.

## Metric Improvement 2: Created custom local workload
We made a copy of the workload code we were using and redesigned it to interact locally with the cache, bypassing the network. This gave us much less noisy performance measurements. We retested double hashing and a couple of other optimizations and recomfirmed our original measurements.

## Optimization 6: Implemented proper delete (Sizable improvement to memory usage)
The cache origninally used lazy deletion (marking entries as deleted instead of actually removing them from the table), resulting in a large number of deleted entries pilling up in the hash-table. Since we now use linear traversal, and there is an aglorithm for proper deletion in a linear hash-table, we implemented that and found we got precisely the same performance using a quarter the memory we previously were using.

## Optimization 7: Switched from SOA to AOS (Negative effect on performance)
The cache originally organized the hash-table as a pair of arrays respectively storing key-hashes and pointers to the full table entry in each (Struct of Arrays format). We switched it to use a single array storing the key-hashes and pointers contiguously (Array of Structs format). We found that, after this change, worst-case performance was twice as bad. Thus we chose not to keep it, and restored the cache to Struct of Arrays format.

## Optimization 8: Reduced client memory allocations (Small improvement to memory usage)
Originally, the client would allocate a 2kb chunk of memory to the heap into which to read the server's responses to its requests. The server, however, had a maximum return size of 1kb; so we cut the client's server-reading buffer down to 1kb accordingly, thereby saving a small amount of memory for each client function waiting on a server read at a given time. The effect was small, since the client rarely has very many requests running in parallel.

## Optimization 9: Allocated keys and values together (Unnoticeable)
The cache originally allocated keys and values separately, but this was unnecessary, and so we switched to jointly allocating them in a contiguous block of memory. Due to this and several previous changes, a typical get request will have exactly four cache-line misses: one to fetch the key hash, one to fetch the entry pointer, one to fetch the entry, and once to fetch the key and value. This is an improvement over the previous version, whose average miss count was slightly higher.

## Optimization 10: Multithreading
We implemented threading of our server, as well as improving the process by which our benchmarking program did its own threading.

The server now, whenever it receives a socket connection, spins off a new thread to handle that socket connection, reading in whatever is sent over it and returning a response, before exiting to avoid clogging up the system's thread count.

Unfortunately, implementing threading made it significantly harder to use any of our metrics, because most of them were recorded within the cache, and with threading in place race conditions become a concern. Thus, for the most part, we were forced to rely entirely on our benchmark's own timing mechanisms, which itself required some changes to its clock in order to continue working. However, we believe the multithreaded server is now functional.

Unfortunately, with the limited metrics now available to us, it's difficult to distinguish truly-good results from noise. Thus, although we *believe* the multithread server to be a performance improvement, we're not *sure* of that. Notably, however, it lacks whatever flaw plagued our HW5; we're able to throw arbitrarily long chains of rapid inputs at the server, and it will continue to return in under 1ms rather than eventually being overwhelmed; we ran our benchmark on it (on localhost) for over half an hour to test.

(Over the network, however; it continues to be sufficiently bogged down by fluctuations in ping over the course of tests that we're unable to get any reliable data; the network noise far outpaces our latency-testing function's abiltiy to account for it, and so while we believe that its performance is more-or-less comparably stable we haven't verified its status as such to nearly the same extent.)