#include <iostream>
#include <thread>
#include "ThreadPool.h"

int main(int, char**){
    std::cout << "Hello, from threadpool!\n";
    ThreadPool pool(std::thread::hardware_concurrency());
}
