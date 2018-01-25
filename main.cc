#include <linux/mman.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <cassert>

#include <memory>
#include <iostream>
#include <string>
#include <type_traits>
#include <ratio>

void* reserve_memory(size_t size) {
    return mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_UNINITIALIZED, -1, 0);
}

void free_memory(void* addr, size_t size) {
    munmap(addr, size);
}

struct memory_size {
    explicit memory_size(const size_t v) : value(v) {}
    const size_t value;
};

struct element_count {
    explicit element_count(const size_t s) : value(s) {}
    const size_t value;
};

struct helper {
    const static size_t page_size;
};

const size_t helper::page_size = sysconf(_SC_PAGESIZE);

template <typename T, typename R = std::ratio<3, 2>>
struct vector {
    using value_type = T;

    using reference = T&;
    using const_reference = const T&;

    using iterator = T*;
    using const_iterator = const T*;

    vector(const element_count elements)
        : vector(memory_size{elements.value * sizeof(T)})
    {
    }

    vector(const memory_size memoryInBytes) 
        : m_data(reserve_memory(memoryInBytes.value))
        , m_mappingSize(memoryInBytes.value)
    {
        assert(sizeof(T) < helper::page_size);

        if (m_data == MAP_FAILED) {
            throw std::bad_alloc();
        }
        realloc_cap(helper::page_size / sizeof(T));
        m_start = (T*) m_data; // TODO: aligment
    }

    ~vector() {
        free_memory(m_data, m_mappingSize);
    }

    reference operator[] (const size_t i) {
        assert(valid(i));
        return m_start[i];
    }

    const_reference operator[] (const size_t i) const {
        assert(valid(i));
        return m_start[i];
    }

    void push_back(const T& val) {
        resize_if_needed();
        new (&m_start[m_size++]) T(val);
    }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        resize_if_needed();
        new (&m_start[m_size++]) T(args...);
    }
    
    template <typename U = T>
    typename std::enable_if<std::is_trivially_copyable<U>::value, void>::type
    erase(const_iterator first, const_iterator last) {
        destroy(first, last);
        memmove((void*) first, (void*)last, (size_t)(end() - last));
    }

    template <typename U = T>
    typename std::enable_if<!std::is_trivially_copyable<U>::value && std::is_move_assignable<U>::value, void>::type
    erase(const_iterator first, const_iterator last) {
        destroy(first, last);
        auto next = (iterator) last;
        auto start = (iterator) first;
        while (next != end()) {
            *start = std::move(*next);
            ++start;
            ++next;
        }
    }

    template <typename U = T>
    typename std::enable_if<!std::is_move_assignable<U>::value && !std::is_trivially_copyable<U>::value, void>::type
    erase(const_iterator first, const_iterator last) {
        destroy(first, last);
        auto next = (iterator) last;
        auto start = (iterator) first;
        while (next != end()) {
            *start = *next;
            ++start;
            ++next;
        }
    }

    void erase(const_iterator it) { erase(it, it+1); }

    iterator begin() {
        return &m_start[0];
    }

    iterator end() {
        return &m_start[m_size];
    }

    const_iterator begin() const {
        return &m_start[0];
    }

    const_iterator end() const {
        return &m_start[m_size];
    }

    void clear() {
        erase(begin(), end());
    }

    size_t size() const {
        return m_size;
    }

    size_t capacity() const {
        return m_capacity;
    }

    void freez() {
        if (mprotect(m_data, m_capacity * sizeof(T), PROT_READ) != 0) {
            throw mprotect_error();
        }
    }

    void unfreez() {
        if (mprotect(m_data, m_capacity * sizeof(T), PROT_READ | PROT_WRITE) != 0) {
            throw mprotect_error();
        }
    }

private: 

    struct mprotect_error : std::runtime_error {
        mprotect_error() : std::runtime_error("mprotect failed: " + std::string(strerror(errno))) {}
    };

    void resize_if_needed() {
        if (m_size == m_capacity) {
            realloc_cap((m_capacity * sizeof(T) * R::num) / R::den);
        }
    }

    void realloc_cap(size_t bytes) {
        if (bytes > m_mappingSize) {
            throw std::bad_alloc();
        }
        if(mprotect(m_data, bytes, PROT_READ | PROT_WRITE) != 0) {
            throw mprotect_error();
        }
        m_capacity = bytes / sizeof(T);
    }

    template <typename U = T>
    typename std::enable_if<std::is_trivially_destructible<U>::value, void>::type
    destroy(const_iterator , const_iterator ) {
    }

    template <typename U = T>
    typename std::enable_if<!std::is_trivially_destructible<U>::value, void>::type
    destroy(const_iterator first, const_iterator last) {
        for (auto it = first; it != last; ++it) {
            it->~T();
        }
    }

    bool valid(const size_t i) {
        return i >= 0 && i < m_size;
    }

    void* m_data;
    T* m_start;
    size_t m_mappingSize;
    size_t m_capacity;
    size_t m_size = 0;
};

struct Dummy {
    Dummy() = default;
    ~Dummy() = default;

    Dummy(const Dummy&) {}
    Dummy& operator=(const Dummy&) { return *this; }

    Dummy(Dummy&&) = delete;
    Dummy& operator=(Dummy&&) = delete;
};

void test() {
    vector<uint64_t> v{memory_size{1024*1024*1024}};
    assert(v.size() == 0);
    assert(v.capacity() > 0);
    v.push_back(0); assert(v.size() == 1);
    v.push_back(1); assert(v.size() == 2);
    v.push_back(2); assert(v.size() == 3);
    assert(v[0] == 0); assert(v[1] == 1); assert(v[2] == 2);
    std::cout << v.capacity() << std::endl;
    for (int i = 0; i != 1000; ++i) {
        v.push_back(1000 + i);
    }
    for (uint64_t i = 0; i != 1000; ++i) {
        assert(v[i+3] == (1000+i));
    }
    std::cout << v.capacity() << std::endl;
    for (int i = 0; i != 10000; ++i) {
        v.push_back(1000 + i);
    }
    std::cout << v.capacity() << std::endl;
    for (int i = 0; i != 100000; ++i) {
        v.push_back(1000 + i);
    }
    std::cout << v.capacity() << std::endl;
    for (int i = 0; i != 1000000; ++i) {
        v.push_back(1000 + i);
    }
    std::cout << v.capacity() << std::endl;
    for (int i = 0; i != 10000000; ++i) {
        v.push_back(1000 + i);
    }
    std::cout << v.capacity() << std::endl;
    v.clear();
    
    vector<std::string> strings{memory_size{1024*1024*1024}};
    std::string first = "foo bar baz foo bar baz foo bar baz " + std::to_string(1);
    strings.push_back(first);
    assert(strings[0] == first);
    for (int i = 0; i != 10000; ++i) {
        std::string first = "foo bar baz foo bar baz foo bar baz " + std::to_string(i);
    }
    strings.freez();
    strings.unfreez();
    *strings.begin() = std::string{"foo"};
    strings.clear();

    vector<Dummy> dummys{element_count{1024*1024}};
    for (int i = 0; i != 1000; ++i) {
        dummys.emplace_back();
    }
    dummys.freez();
    dummys.clear();
}

int main() {
    for (int i = 0; i != 3; ++i) {
        test();
    }
    return 0;
}
