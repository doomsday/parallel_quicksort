#pragma once
#include <list>
#include <future>
#include "lfs_hazard.hpp"

template <typename T>
class sorter	// 1
{
private:
	struct chunk_to_sort
	{
		std::list<T> data;
		std::promise<std::list<T>> promise;
	};

	lock_free_stack<chunk_to_sort> chunks;	// 2
	std::vector<std::thread> threads;	// 3
	unsigned const max_thread_count;
	std::atomic<bool> end_of_data;

	void try_sort_chunk() {
		std::shared_ptr<chunk_to_sort> chunk = chunks.pop();	// 7
		if (chunk) {
			sort_chunk(chunk);	// 8
		}
	}

	void sort_chunk(std::shared_ptr<chunk_to_sort> const& chunk) {
		chunk->promise.set_value(do_sort(chunk->data));	// 15
	}

	void sort_thread() {
		while (!end_of_data) {	// 16
			try_sort_chunk();	// 17
			std::this_thread::yield();	// 18
		}
	}

	public:
	// Sorts list of T by partitioning chunk_data and calling 'try_sort_chunk'
	std::list<T> do_sort(std::list<T>& chunk_data) {	// 9
		if (chunk_data.empty()) {
			return chunk_data;
		}

		std::list<T> result;	// will be returned finally
		result.splice(result.begin(), chunk_data, chunk_data.begin());	// move to 'result' the first T of chunk_data
		T const& pivot_val = *result.begin();	// pivot value

		typename std::list<T>::iterator pivot_iter = std::partition(chunk_data.begin(), chunk_data.end(),	// 10
			[&](T const& val) {return val < pivot_val; }); // reorder by pivot, return iterator to the first element of the second group

		chunk_to_sort new_lower_chunk;
		new_lower_chunk.data.splice(new_lower_chunk.data.end(), chunk_data, chunk_data.begin(), pivot_iter);	// move elements preceding pivot from chunk_data
		std::future<std::list<T>> new_lower_fut = new_lower_chunk.promise.get_future();
		chunks.push(std::move(new_lower_chunk));	// push lower part to 'chunks'	// 11

		if (threads.size() < max_thread_count) {	// 12
			threads.push_back(std::thread(&sorter<T>::sort_thread, this));
		}

		std::list<T> new_higher(do_sort(chunk_data));	// recursive call: sort elements after pivot
		result.splice(result.end(), new_higher);	// higher part is ready, store to the result 'past-the-end'

		while (new_lower_fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {	// 13
			try_sort_chunk();	// sort lower part in 'chunks' (?)	// 14
		}

		result.splice(result.begin(), new_lower_fut.get());	// lower part is ready, store to the 'result'
		return result;
	}


	sorter() :
		max_thread_count(std::thread::hardware_concurrency() - 1),
		end_of_data(false)
	{}
	~sorter() {
		end_of_data = true;
		for (unsigned i = 0; i < threads.size(); ++i) {
			threads[i].join();
		}
	}
};

template <typename T>
std::list<T> parallel_quick_sort(std::list<T> input) {	// 19
	if (input.empty()) {
		return input;
	}
	sorter<T> s;

	return s.do_sort(input);	// 20
}