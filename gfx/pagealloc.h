#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <sys/mman.h>

namespace fd {

constexpr size_t kOwnMapBytes = 65536;

constexpr size_t page_round(size_t n) { return (n + 4095) & ~(size_t)4095; }

template <class T> struct PageAlloc {
    using value_type = T;

    PageAlloc() = default;
    template <class U> PageAlloc(const PageAlloc<U>&) {}

    T* allocate(size_t n)
    {
#ifndef __SANITIZE_ADDRESS__
        if (n * sizeof(T) >= kOwnMapBytes) {
            void* p =
                mmap(nullptr, page_round(n * sizeof(T)), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED)
                throw std::bad_alloc();
            return static_cast<T*>(p);
        }
#endif
        return std::allocator<T>().allocate(n);
    }

    void deallocate(T* p, size_t n)
    {
#ifndef __SANITIZE_ADDRESS__
        if (n * sizeof(T) >= kOwnMapBytes) {
            munmap(p, page_round(n * sizeof(T)));
            return;
        }
#endif
        std::allocator<T>().deallocate(p, n);
    }
};

template <class T, class U> bool operator==(const PageAlloc<T>&, const PageAlloc<U>&) { return true; }
template <class T, class U> bool operator!=(const PageAlloc<T>&, const PageAlloc<U>&) { return false; }

}
