/* This approach is a specialized version of a thread pool—there’s a set of threads that each take work to do from a
list of pending work, do the work, and then go back to the list for more.
*/

#pragma once
#include <list>
#include <future>
#include "lfs_hazard.hpp"

using std::list;
using std::thread;
using std::shared_ptr;
using std::move;
using std::promise;
using std::vector;
using std::atomic;
using std::future;

template <typename T>
class sorter	// 1
{
private:
	struct chunk_to_sort {
		list<T> data;
		promise<list<T>> promise;

		chunk_to_sort() = default;

		chunk_to_sort(const chunk_to_sort&) = delete;	// copying std::promise is not defined (its copy constructor is deleted), so copying instance of 'chunk_to_sort' is meaningless
		chunk_to_sort(chunk_to_sort&& rhs)
			: data(move(rhs.data))
			, promise(move(rhs.promise))
		{}
	};

	lock_free_stack<chunk_to_sort> chunks;	// 2: stack for grouping unsorted chunks
	vector<thread> threads;	// 3
	unsigned const max_thread_count;
	atomic<bool> end_of_data;

	void try_sort_chunk() {
		shared_ptr<chunk_to_sort> chunk = chunks.pop();	// 7: pop a chunk off the stack
		if (chunk) {
			sort_chunk(move(chunk));	// 8: sort it
		}
	}

	void sort_chunk(shared_ptr<chunk_to_sort>&& chunk) {
		chunk->promise.set_value(do_sort(move(chunk->data)));	// 15: store the result in the promise, ready to be picked up by the thread that posted the chunk on the stack
	}

	void sort_thread() {
		while (!end_of_data) {	// 16: while the end_of_data flag isn't set
			try_sort_chunk();	// 17: sit in a loop trying to sort chunks off the stack
			std::this_thread::yield();	// 18: (implementation dependent) suspend the current thread and put it on the back of the queue of the same-priority threads that are ready to run
		}
	}

public:
	// Sorts list of T by partitioning chunk_data and calling 'try_sort_chunk'
	list<T> do_sort(list<T>&& chunk_data) {	// 9
		if (chunk_data.empty()) {
			return chunk_data;
		}

		list<T> result;	// will be returned finally
		result.splice(result.begin(), chunk_data, chunk_data.begin());	// move to 'result' the first T of chunk_data
		T const& pivot_val = *result.begin();	// pivot value

		typename list<T>::iterator pivot_iter = std::partition(chunk_data.begin(), chunk_data.end(),	// 10
			[&](T const& val) {return val < pivot_val; }); // reorder by pivot, return iterator to the first element of the second group

		chunk_to_sort new_lower_chunk;
		new_lower_chunk.data.splice(new_lower_chunk.data.end(), chunk_data, chunk_data.begin(), pivot_iter);	// move elements preceding pivot from chunk_data
		future<list<T>> new_lower_fut = new_lower_chunk.promise.get_future();
		chunks.push(move(new_lower_chunk));	// 11: push lower part to 'chunks' rather than spawning a new thread for one chunk

		if (threads.size() < max_thread_count) {	// 12: spawn a new thread while you still have processors to spare
			threads.push_back(thread(&sorter<T>::sort_thread, this));
		}

		list<T> new_higher(do_sort(move(chunk_data)));	// recursive call: sort elements after pivot (higher)
		result.splice(result.end(), new_higher);	// higher part is ready, store to the result 'past-the-end'

		while (new_lower_fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {	// 13: lower chunk might be handled by another thread, you then have to wait for it to be ready
			try_sort_chunk();	// 14: (helper) in case you're the only thread or all the others are already busy, you try to process chunks from the stack on this thread while you're waiting
		}

		result.splice(result.begin(), new_lower_fut.get());	// lower part is ready, store to the 'result'
		return result;
	}

	sorter() :
		max_thread_count(thread::hardware_concurrency() - 1),
		end_of_data(false)
	{}

	~sorter() {	// 4: tidy up these threads
		end_of_data = true;	// 5: Setting the flag terminates the loop in the thread function
		for (unsigned i = 0; i < threads.size(); ++i) {
			threads[i].join();	// 6: wait for the threads to finish
		}
	}
};

template <typename T>
list<T> parallel_quick_sort(list<T> input) {	// 19: delegates most of the functionality to the sorter class
	if (input.empty()) {
		return input;
	}
	sorter<T> s;

	return s.do_sort(move(input));	// 20
}