#include "Poller.h"
#include <iostream>
#include <memory>

int main() {
    std::unique_ptr<Poller> poller = CreatePoller();
    if(!poller) {
        std::cerr << "Failed to create Poller instance." << std::endl;
        return 1;
    }
    std::cout << "Poller instance created successfully." << std::endl;
    return 0;
}