#include "SharedMemory.hpp"
#include <iostream>

SharedMemory::SharedMemory(bool create)
    : fd(-1), _root(nullptr), owner(false)
{
    bool do_create = create;
    if (do_create) {
        // create and set size
        fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (fd < 0) throw std::runtime_error("shm_open create failed");
        if (ftruncate(fd, sizeof(SharedMemoryRoot)) != 0) {
            close(fd);
            throw std::runtime_error("ftruncate failed");
        }
        owner = true;
    } else {
        fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (fd < 0) throw std::runtime_error("shm_open open failed; run server first");
    }
    void* addr = mmap(nullptr, sizeof(SharedMemoryRoot), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("mmap failed");
    }
    _root = reinterpret_cast<SharedMemoryRoot*>(addr);
}

SharedMemory::~SharedMemory() {
    if (_root) munmap(_root, sizeof(SharedMemoryRoot));
    if (fd >= 0) close(fd);
    if (owner) {
        shm_unlink(SHM_NAME);
    }
}
