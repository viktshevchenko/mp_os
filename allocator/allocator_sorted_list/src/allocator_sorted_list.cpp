#include <not_implemented.h>

#include "../include/allocator_sorted_list.h"

allocator_sorted_list::~allocator_sorted_list()
{
	debug_with_guard("~allocator_sorted_list()");
    free_memory();
}

allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list &&other) noexcept : _trusted_memory(nullptr)
{
    if(other._trusted_memory == nullptr)
		return;
	debug_with_guard("allocator_sorted_list(allocator_sorted_list &&)");
	other.debug_with_guard("allocator_sorted_list(allocator_sorted_list &&)");
	get_mutex().lock();
	_trusted_memory = other._trusted_memory;
	debug_with_guard("move data from other allocator_sorted_list(allocator_sorted_list &&)");
	other._trusted_memory = nullptr;
	other.debug_with_guard("_trusted_memory = nullptr allocator_sorted_list(allocator_sorted_list &&)");
	get_mutex().unlock();
}

allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept
{
	debug_with_guard("allocator_sorted_list::operator=(allocator_sorted_list && )");
    if(this != &other) {
		free_memory();
		other.get_mutex().lock();
		_trusted_memory = other._trusted_memory;
		debug_with_guard("move data from other allocator_sorted_list::operator=(allocator_sorted_list && )");
		other._trusted_memory = nullptr;
		other.debug_with_guard("_trusted_memory = nullptr allocator_sorted_list::operator=(allocator_sorted_list&& )");
		other.get_mutex().unlock();
	}
	return *this;
}

allocator_sorted_list::allocator_sorted_list(
    size_t space_size,
    allocator *parent_allocator,
    logger *logger,
    allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    debug_with_guard("allocator_sorted_list()");

    if(space_size < block_metadata_size()) {
        error_with_guard("space_size < block_metadata_size can't initialize allocator instance allocator_sorted_list()");
        throw std::logic_error("Can't initialize allocator instance");
    }

    size_t memory_size = space_size + allocator_metadata_size() + block_metadata_size();

	try {
		_trusted_memory = parent_allocator == nullptr ? ::operator new (memory_size) : parent_allocator->allocate(1, memory_size);
	}
	catch (std::bad_alloc const &error){
		error_with_guard(error.what());
		throw std::bad_alloc();
	}

	allocator **parent_allocator_placement = reinterpret_cast<allocator **>(_trusted_memory);
	*parent_allocator_placement = parent_allocator;

	class logger **logger_placement = reinterpret_cast<class logger **>(parent_allocator_placement + 1);
	*logger_placement = logger;

	std::mutex *synchronizer_placement = reinterpret_cast<std::mutex *>(logger_placement + 1);
	new (reinterpret_cast<void *>(synchronizer_placement)) std::mutex;

	allocator_with_fit_mode::fit_mode *fit_mode_placement = reinterpret_cast<allocator_with_fit_mode::fit_mode *>
			(synchronizer_placement + 1);
	*fit_mode_placement = allocate_fit_mode;

	size_t *size_placement = reinterpret_cast<size_t *>(fit_mode_placement + 1);
	*size_placement = space_size;

	void **ptr_first_available_block_placement = reinterpret_cast<void **>(size_placement + 1);
	*ptr_first_available_block_placement = ptr_first_available_block_placement + 1;

	void **ptr_next_available_block_placement = reinterpret_cast<void **>(ptr_first_available_block_placement  + 1);
	*ptr_next_available_block_placement = nullptr;

	size_t *block_size_placement = reinterpret_cast<size_t *>(ptr_next_available_block_placement + 1);
	*block_size_placement = space_size;

	debug_with_guard("trusted " + std::to_string(space_size) + " bytes of memory allocator_sorted_list()");
	debug_with_guard("allocator successfully created allocator_sorted_list()");
}

[[nodiscard]] void *allocator_sorted_list::allocate(
    size_t value_size,
    size_t values_count)
{
	debug_with_guard("want allocate " + std::to_string(value_size) + std::to_string(values_count) + " allocate()");
    get_mutex().lock();

	void *target_block = nullptr, *previous_to_target_block = nullptr;
	size_t requested_size = value_size *  values_count + block_metadata_size();
	size_t target_block_size;

	{
		void *current_block, *previous_block = nullptr;
		current_block = get_ptr_on_first_available_block();
		allocator_with_fit_mode::fit_mode fit_mode = get_fit_mode();

		while(current_block != nullptr) {
			size_t current_block_size = get_size_block_without_metadata(current_block);

			if(current_block_size >= requested_size && (fit_mode == allocator_with_fit_mode::fit_mode::first_fit ||
			(fit_mode == allocator_with_fit_mode::fit_mode::the_best_fit && (target_block == nullptr || current_block_size
			< target_block_size)) || (fit_mode == allocator_with_fit_mode::fit_mode::the_worst_fit && (target_block
			== nullptr || current_block_size < target_block_size)))) {
				target_block = current_block;
				target_block_size = current_block_size;
				previous_to_target_block = previous_block;

				if(fit_mode == allocator_with_fit_mode::fit_mode::first_fit)
					break;
			}

			previous_block = current_block;
			current_block = get_ptr_on_next_available_block(current_block);
		}
	}

	if (target_block == nullptr) {
		error_with_guard("no suitable block allocate()");
		throw std::bad_alloc();
	}

	if (target_block_size - requested_size < block_metadata_size()) {
		requested_size = target_block_size;
		warning_with_guard("Redefining the request allocate()");
	}

	if (target_block_size != requested_size) {
		void **next_available_block = reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(target_block) +
																block_metadata_size() + value_size *  values_count);
		*next_available_block = get_ptr_on_next_available_block(target_block);

		size_t *next_available_block_size = reinterpret_cast<size_t *>(next_available_block + 1);
		*next_available_block_size = target_block_size - requested_size - block_metadata_size();

		get_ptr_on_next_available_block(previous_to_target_block) = next_available_block;
	}
	else
		get_ptr_on_next_available_block(previous_to_target_block) = get_ptr_on_next_available_block(target_block);

	void **ptr_on_parent_allocator = reinterpret_cast<void **>(target_block);
	*ptr_on_parent_allocator = _trusted_memory;

	size_t *size_block = reinterpret_cast<size_t *>(ptr_on_parent_allocator + 1);
	*size_block = target_block_size;

	debug_with_guard("memory has been successfully allocated allocate()");

	return reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(target_block) + block_metadata_size());
}

void allocator_sorted_list::deallocate(
    void *at)
{
    throw not_implemented("void allocator_sorted_list::deallocate(void *)", "your code should be here...");
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
	debug_with_guard("set_fit_mode()");
    get_fit_mode() = mode;
}

inline allocator *allocator_sorted_list::get_allocator() const
{
	debug_with_guard("get_allocator()");
    return *reinterpret_cast<allocator **>(_trusted_memory);
}

inline logger *allocator_sorted_list::get_logger() const
{
	debug_with_guard("get logger()");
	return *reinterpret_cast<logger**>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *));
}

inline std::mutex &allocator_sorted_list::get_mutex() const {
	debug_with_guard("get_mutex()");
	return *reinterpret_cast<std::mutex *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) +
										  sizeof(logger *));
}

inline allocator_with_fit_mode::fit_mode &allocator_sorted_list::get_fit_mode() const {
	debug_with_guard("get_fit_mode()");
	return *reinterpret_cast<allocator_with_fit_mode::fit_mode *>(reinterpret_cast<unsigned char *>(_trusted_memory) +
																 sizeof(allocator *) + sizeof(logger *) + sizeof(std::mutex));
}

inline size_t allocator_sorted_list::get_size_allocator_without_metadata() const {
	debug_with_guard("get_size_allocator_without_metadata");
	return *reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) +
									   sizeof(logger *) + sizeof(std::mutex) + sizeof
									   (allocator_with_fit_mode::fit_mode));
}

inline void *&allocator_sorted_list::get_ptr_on_first_available_block() const {
	debug_with_guard("get_ptr_on_first_available_block()");
	return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) +
									  sizeof(logger *) + sizeof(std::mutex) + sizeof
									  (allocator_with_fit_mode::fit_mode) + sizeof(size_t));
}

inline void *&allocator_sorted_list::get_ptr_on_next_available_block(void *current_available_block) const {
	debug_with_guard("get_ptr_on_next_available_block(void *)");
	return *reinterpret_cast<void **>(current_available_block);
}

inline size_t allocator_sorted_list::get_size_block_without_metadata(void *current_available_block) const {
	debug_with_guard("get_size_block_without_metadata(void *)");
	return *reinterpret_cast<size_t *>(&get_ptr_on_next_available_block(current_available_block) + 1);
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
	std::vector<allocator_sorted_list::block_info> blocks_info;
	allocator_sorted_list::block_info info_current_block {};

	void **ptr_on_last_bit_in_allocator = reinterpret_cast<void **>(reinterpret_cast<unsigned char *>
			(_trusted_memory) +  allocator_metadata_size() + get_size_allocator_without_metadata() - 1);
	void *ptr_on_next_available_block = get_ptr_on_next_available_block(_trusted_memory);
	void *ptr = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(_trusted_memory) +
			allocator_metadata_size() - 1);

	while(ptr != ptr_on_last_bit_in_allocator) {
		ptr = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(ptr) + 1);

		if (ptr == ptr_on_next_available_block) {
			info_current_block.is_block_occupied = false;
			ptr_on_next_available_block = get_ptr_on_next_available_block(ptr);
		}
		else
			info_current_block.is_block_occupied = true;

		info_current_block.block_size = get_size_block_without_metadata(ptr) + block_metadata_size();

		blocks_info.push_back(info_current_block);

		ptr = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(ptr)+ block_metadata_size() +
				get_size_block_without_metadata(ptr) - 1);
	}

	return blocks_info;
}

inline std::string allocator_sorted_list::get_typename() const noexcept
{
	debug_with_guard(" return typename (allocator_sorted_list) get_typename()");
	return "allocator sorted list";
}

size_t constexpr allocator_sorted_list::block_metadata_size() const
{
    return sizeof(block_size_t) + sizeof(block_pointer_t);
}

size_t constexpr allocator_sorted_list::allocator_metadata_size() const
{
    return sizeof(allocator *) + sizeof(logger *) + sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode) +
		   sizeof(size_t) + sizeof(void *);
}

void allocator_sorted_list::free_memory() {
	if(_trusted_memory == nullptr)
		return;
	debug_with_guard("free_memory()");
	get_mutex().~mutex();
	//get_allocator() == nullptr ? ::operator delete (_trusted_memory) : get_allocator() -> deallocate(_trusted_memory);
	deallocate_with_guard(_trusted_memory);
}