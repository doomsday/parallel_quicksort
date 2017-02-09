#include "stdafx.h"
#include <iostream>
#include <list>

#include "sorter.hpp"

int main()
{
	std::list<int> li;

	li.push_front(3);
	li.push_front(1);
	li.push_front(12);
	li.push_front(7);
	li.push_front(9);

	std::list<int> li_r = parallel_quick_sort<int>(li);

	std::cout << "test";
	return 0;
}

