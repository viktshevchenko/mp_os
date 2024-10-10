#include <not_implemented.h>

#include "../include/allocator_global_heap.h"

allocator_global_heap::allocator_global_heap(
    logger *logger): _logger(logger)
{
    debug_with_guard("allocator_global_heap()");
}

allocator_global_heap::~allocator_global_heap()
{
    debug_with_guard("~allocator_global_heap()");
    _logger = nullptr;
    debug_with_guard("_logger = nullptr in ~allocator_global_heap()");
}

allocator_global_heap::allocator_global_heap(
    allocator_global_heap &&other) noexcept
{
    debug_with_guard("allocator_global_heap(allocator_global_heap &&)");
    other._logger = _logger;
    _logger = nullptr;
    debug_with_guard("move logger allocator_global_heap(allocator_global_heap &&)");
}

allocator_global_heap &allocator_global_heap::operator=(
    allocator_global_heap &&other) noexcept
{
    debug_with_guard("operator=(allocator_global_heap &&)");
    if(this != &other) {
        other._logger = _logger;
        _logger = nullptr;
    }
    debug_with_guard("move logger operator=(allocator_global_heap &&)");
    return *this;
}

[[nodiscard]] void *allocator_global_heap::allocate(
    size_t value_size,
    size_t values_count)
{
    debug_with_guard(std::string("want allocate " + std::to_string(value_size) + " value_size and " +
        std::to_string(values_count) + " values_count allocate()"));

    size_t requested_memory = value_size * values_count + sizeof(size_t) + sizeof(allocator*);
    warning_with_guard("redefined requested memory to value_size * values_count + sizeof(size_t) + sizeof(allocator*) allocate()");

    void *allocated_memory = nullptr;
    try {
        allocated_memory = ::operator new(requested_memory);
    }
    catch (std::bad_alloc const &error){
        error_with_guard(error.what());
        throw error;
    }
    debug_with_guard(std::string("successful allocate " + std::to_string(value_size) + " value_size and " +
                                 std::to_string(values_count) + " values_count allocate()"));

    allocator **alloc = reinterpret_cast<allocator**>(allocated_memory);
    size_t *block_size = reinterpret_cast<size_t*>(alloc + 1);

    *alloc = this;
    *block_size = requested_memory;

    return reinterpret_cast<unsigned char*>(allocated_memory) + sizeof(size_t) + sizeof(allocator*);
}

void allocator_global_heap::deallocate(
    void *at)
{
    debug_with_guard("deallocate()");
    size_t *block_size = reinterpret_cast<size_t*>(reinterpret_cast<unsigned char*>(at) - sizeof(size_t));
    unsigned char* bytes = reinterpret_cast<unsigned char*>(at);

    bytes_dump(bytes, *block_size);

    allocator *alloc = nullptr;
    try {
        alloc = reinterpret_cast<allocator*>(block_size - 1);
    }
    catch(const std::exception &error) {
        error_with_guard(std::string(error.what()) + " block doesn't belong this allocate deallocate()");
        throw std::logic_error("block doesn't belong this allocate deallocate()");
    }

    ::operator delete(alloc);
}

inline logger *allocator_global_heap::get_logger() const
{
    debug_with_guard("return _logger get_logger()");
    return _logger;
}

inline std::string allocator_global_heap::get_typename() const noexcept
{
    debug_with_guard("return std::string get_typename()");
    return "allocator_global_heap";
}

std::string allocator_global_heap::bytes_dump(unsigned char const *bytes, size_t size_block) {
    std::string dump;

    for(auto i = 0; i < size_block; i++)
        dump += bytes[i];

    debug_with_guard(std::string(dump) + " bytes_dump()");
    return dump;
}
