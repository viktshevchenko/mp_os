#include <not_implemented.h>

#include "../include/allocator_buddies_system.h"

#include <sstream>

//#define DEBUG

allocator_buddies_system::~allocator_buddies_system()
{
	debug_with_guard("~allocator_buddies_system() method called");
	free_memory();
}

allocator_buddies_system::allocator_buddies_system(
    allocator_buddies_system &&other) noexcept
{
    if (other._trusted_memory == nullptr) {
		return;
	}

	std::lock_guard<std::mutex> lock(other.get_mutex());

	_trusted_memory = other._trusted_memory;
	other._trusted_memory = nullptr;

	debug_with_guard("allocator_buddies_system(allocator_buddies_system &&other) method called");
}

allocator_buddies_system &allocator_buddies_system::operator=(
    allocator_buddies_system &&other) noexcept
{
	debug_with_guard("operator=(allocator_buddies_system &&other) method called");

	if (this != &other) {
		free_memory();

		std::lock_guard<std::mutex> lock(get_mutex());

		_trusted_memory = other._trusted_memory;
		other._trusted_memory = nullptr;
	}
	return *this;
}

allocator_buddies_system::allocator_buddies_system(
    size_t space_size,
    allocator *parent_allocator,
    logger *logger,
    allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
	if ((1 << space_size) < block_metadata_size()) {
		throw std::logic_error("Can't initialize allocator instance");
	}

	size_t memory_size = allocator_metadata_size() + (1 << space_size);

	try {
		_trusted_memory = parent_allocator == nullptr
						  ? ::operator new (memory_size)
						  : parent_allocator->allocate(1, memory_size);
	}
	catch (std::bad_alloc const &error) {
		if (logger != nullptr) {
			logger->error(error.what());
		}
		throw std::bad_alloc();
	}

	allocator **parent_allocator_placement = reinterpret_cast<allocator **>(_trusted_memory);
	*parent_allocator_placement = parent_allocator;

	class logger **logger_placement = reinterpret_cast<class logger **>(parent_allocator_placement + 1);
	*logger_placement = logger;

	std::mutex *synchronizer_placement = reinterpret_cast<std::mutex *>(logger_placement + 1);
	new (reinterpret_cast<void *>(synchronizer_placement)) std::mutex;

	allocator_with_fit_mode::fit_mode *fit_mode_placement = reinterpret_cast<allocator_with_fit_mode::fit_mode *>	(synchronizer_placement + 1);
	*fit_mode_placement = allocate_fit_mode;

	size_t *allocator_size_without_metadata_placement = reinterpret_cast<size_t *>(fit_mode_placement + 1);
	*allocator_size_without_metadata_placement = 1 << space_size;

	void **ptr_on_first_free_block_placement = reinterpret_cast<void **>(allocator_size_without_metadata_placement + 1);
	*ptr_on_first_free_block_placement = ptr_on_first_free_block_placement + 1;

	void **block_parent_allocator_placement = reinterpret_cast<void **>(ptr_on_first_free_block_placement + 1);
	*block_parent_allocator_placement = _trusted_memory;

	bool *is_block_occupied_placement = reinterpret_cast<bool *>(block_parent_allocator_placement + 1);
	*is_block_occupied_placement = false;

	size_t *block_size_with_metadata_placement = reinterpret_cast<size_t *>(is_block_occupied_placement + 1);
	*block_size_with_metadata_placement = space_size;

	void **ptr_on_previous_free_block_placement = reinterpret_cast<void **>(block_size_with_metadata_placement + 1);
	*ptr_on_previous_free_block_placement = nullptr;

	void **ptr_on_next_free_block_placement = ptr_on_first_free_block_placement + 1;
	*ptr_on_next_free_block_placement = nullptr;

	debug_with_guard("trusted " + std::to_string(1 << space_size) + " bytes of memory allocator_buddies_system()");

#ifdef DEBUG
	log_trusted_memory_dump();
#endif

}

[[nodiscard]] void *allocator_buddies_system::allocate(
    size_t value_size,
    size_t values_count)
{
    if(_trusted_memory == nullptr) {
		throw std::logic_error("allocator instance state was moved");
	}

	std::lock_guard<std::mutex> lock(get_mutex());

	debug_with_guard("want allocate " + std::to_string(value_size) + " " + std::to_string(values_count) + " allocate()");

	void *target_block = nullptr;
	size_t requested_size = values_count * values_count + block_metadata_size();
	size_t target_block_size;

	{
		void *current_block = get_ptr_on_first_available_block();
		void *ptr_on_memory_after_trusted = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>
				(_trusted_memory) + allocator_metadata_size() + get_allocator_size_without_metadata());

		allocator_with_fit_mode::fit_mode fit_mode = get_fit_mode();
		while(current_block < ptr_on_memory_after_trusted)
		{
			size_t current_block_size = get_size_current_block_with_metadata(current_block);

			if(!is_block_occupied(current_block) && current_block_size >= requested_size && (fit_mode == allocator_with_fit_mode::fit_mode::first_fit || (fit_mode == allocator_with_fit_mode::fit_mode::the_best_fit && (target_block == nullptr || current_block_size <	target_block_size)) || (fit_mode == allocator_with_fit_mode::fit_mode::the_worst_fit && (target_block == nullptr || current_block_size < target_block_size))))
			{
				target_block = current_block;
				target_block_size = current_block_size;

				if (fit_mode == allocator_with_fit_mode::fit_mode::first_fit)
				{
					break;
				}
			}
			current_block = get_ptr_on_next_available_block(current_block);
		}
	}

	if (target_block == nullptr) {
		error_with_guard("no suitable block throw std::bad_alloc allocate()");
		throw std::bad_alloc();
	}

	while (target_block_size > requested_size * 2) {
		size_t new_size = target_block_size - 1;
		void *ptr_on_buddy = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(target_block) + (1 <<
				new_size));

		initialize_block_metadata(&ptr_on_buddy, _trusted_memory, false, new_size, get_ptr_on_previous_available_block
		(target_block), get_ptr_on_next_available_block(target_block));

		initialize_block_metadata(&target_block, _trusted_memory, true, new_size, nullptr, nullptr);

		target_block_size = new_size;
	}

	if (requested_size != target_block_size) {
		warning_with_guard("redefining requested size allocate()");
	}
#ifdef DEBUG
	log_trusted_memory_dump();
#endif

	return reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(target_block) + block_metadata_size());
}

void allocator_buddies_system::deallocate(
    void *at)
{
	if(_trusted_memory == nullptr) {
		throw std::logic_error("allocator instance state was moved");
	}

	std::lock_guard<std::mutex>(get_mutex());

	at = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(at) - block_metadata_size());

	if(at == nullptr || at < reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(_trusted_memory) +
													  allocator_metadata_size()) || at > reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(_trusted_memory) +			  allocator_metadata_size() + get_allocator_size_without_metadata())) {
		error_with_guard("invalid block address");
		throw std::logic_error("invalid block address");
	}

	if (get_ptr_on_parent_allocator(at) != _trusted_memory) {
		error_with_guard("attempt to deallocate block into wrong allocator instance");
		throw std::logic_error("attempt to deallocate block into wrong allocator instance");
	}

	void *left_available_block = nullptr;
	void *right_available_block = get_ptr_on_first_available_block();

	while(right_available_block != nullptr)
	{
		if (left_available_block < at && right_available_block > at)
		{
			break;
		}
		right_available_block = get_ptr_on_next_available_block(left_available_block = right_available_block);
	}

	if(left_available_block == nullptr && right_available_block == nullptr) {
		get_ptr_on_next_available_block(get_ptr_on_first_available_block() = at) = nullptr;
		debug_with_guard("block successfully deallocated deallocate()");
		return;
	}

	while (left_available_block != nullptr && (get_ptr_on_next_available_block(left_available_block) == at) &&
	(get_size_current_block_with_metadata(left_available_block) == get_size_current_block_with_metadata(at))	|| right_available_block != nullptr && (get_ptr_on_previous_available_block(right_available_block) == at) &&
	(get_size_current_block_with_metadata(right_available_block) == get_size_current_block_with_metadata(at)))
	{
		if (left_available_block != nullptr && (get_ptr_on_next_available_block(left_available_block) == at) && (get_size_current_block_with_metadata(left_available_block) == get_size_current_block_with_metadata(at)))
		{
			initialize_block_metadata(left_available_block, get_ptr_on_parent_allocator(left_available_block), false,
									  ++get_size_current_block_with_metadata(left_available_block),get_ptr_on_previous_available_block(left_available_block),									  get_ptr_on_next_available_block(at));
		}

		if (right_available_block != nullptr && (get_ptr_on_previous_available_block(right_available_block) == at) && (get_size_current_block_with_metadata(right_available_block) == get_size_current_block_with_metadata(at)))
		{
			initialize_block_metadata(at, get_ptr_on_parent_allocator(at), false,++get_size_current_block_with_metadata(at),get_ptr_on_previous_available_block(at),	get_ptr_on_next_available_block(right_available_block));
		}
		left_available_block = get_ptr_on_previous_available_block(at);
		right_available_block = get_ptr_on_next_available_block(at);
	}

#ifdef DEBUG
	log_trusted_memory_dump();
#endif

	debug_with_guard("block successfully deallocated deallocate()");
}

inline void allocator_buddies_system::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
	debug_with_guard("set_fit_mode(allocator_with_fit_mode ) method called");
	get_fit_mode() = mode;
}

inline allocator *allocator_buddies_system::get_allocator() const
{
	debug_with_guard("get_allocator() method called");
	return *reinterpret_cast<allocator **>(_trusted_memory);
}

inline logger *allocator_buddies_system::get_logger() const
{
	return *reinterpret_cast<logger **>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *));
}

inline std::mutex &allocator_buddies_system::get_mutex() const
{
	return *reinterpret_cast<std::mutex *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) +	sizeof(logger *));
}

inline allocator_with_fit_mode::fit_mode &allocator_buddies_system::get_fit_mode() const
{
	debug_with_guard("get_fit_mode() method called");
	return *reinterpret_cast<allocator_with_fit_mode::fit_mode *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) +	sizeof(logger *) + sizeof(std::mutex));
}

inline size_t allocator_buddies_system::get_allocator_size_without_metadata() const
{
	debug_with_guard("get_allocator_size_without_metadata() method called");
	return *reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) + sizeof(logger *) + sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode));
}

inline void *&allocator_buddies_system::get_ptr_on_first_available_block() const
{
	debug_with_guard("get_ptr_on_first_available_block() method called");
	return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) + sizeof
	(logger *) + sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t));
}

inline void *allocator_buddies_system::get_ptr_on_first_block() const
{
	debug_with_guard("get_ptr_on_first_block() method called");
	return reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(_trusted_memory) + allocator_metadata_size());
}

inline void *&allocator_buddies_system::get_ptr_on_next_available_block(void *current_block) const
{
	debug_with_guard("get_ptr_on_next_available_block(void *) method called");
	return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(current_block) + sizeof(void *) + sizeof(bool)
	+ sizeof(size_t) + sizeof(void *));
}

inline void *allocator_buddies_system::get_ptr_on_previous_available_block(void *current_block) const
{
	debug_with_guard("get_ptr_on_previous_available_block(void *) method called");
	return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(current_block) + sizeof(void *) + sizeof(bool) + sizeof(size_t));
}

inline void *allocator_buddies_system::get_ptr_on_parent_allocator(void *current_block) const
{
	return *reinterpret_cast<void **>(current_block);
}

inline size_t &allocator_buddies_system::get_size_current_block_with_metadata(void *current_block) const
{
	debug_with_guard("get_size_current_block(void *) method called");
	return *reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(current_block) + sizeof(void *) + sizeof	(bool));
}

inline bool allocator_buddies_system::is_block_occupied(void *current_block) const
{
	debug_with_guard("is_block_occupied(void *) method called");
	return *reinterpret_cast<bool *>(reinterpret_cast<unsigned char *>(current_block) + sizeof(void *));
}

void allocator_buddies_system::initialize_block_metadata(void *ptr_on_block, void *ptr_on_parent_allocator,
														bool is_block_occupied, size_t new_size, void
														*ptr_on_previous_aval_block,
														void *ptr_on_next_aval_block)
{
	debug_with_guard("initialize_block_metadata() method called");
	*reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(ptr_on_block) + (1 << new_size)) =
			ptr_on_parent_allocator;
	*reinterpret_cast<bool *>(reinterpret_cast<unsigned char *>(ptr_on_block) + sizeof(void *)) = is_block_occupied;
	*reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(ptr_on_block) + sizeof(void *) + sizeof(bool)) =
			new_size;
	*reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(ptr_on_block) + sizeof(void *) + sizeof(bool) +
			sizeof(size_t)) = ptr_on_previous_aval_block;
	*reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(ptr_on_block) + sizeof(void *) + sizeof(bool) +
			sizeof(size_t) + sizeof(void *)) = ptr_on_next_aval_block;
}

std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info() const noexcept
{
	debug_with_guard("get_blocks_info() method called");
    std::vector<allocator_test_utils::block_info> blocks_info;
	allocator_test_utils::block_info info_current_block {};

	if (_trusted_memory == nullptr) {
		return blocks_info;
	}

	void *memory_after_trusted_for_allocator = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>
			(_trusted_memory) + allocator_metadata_size() + get_allocator_size_without_metadata());

	void *ptr_on_block;
	ptr_on_block = get_ptr_on_first_block();

	while (ptr_on_block != memory_after_trusted_for_allocator) {
		info_current_block.is_block_occupied = is_block_occupied(&ptr_on_block);

		size_t size_current_block = get_size_current_block_with_metadata(ptr_on_block);
		info_current_block.block_size = (1 << size_current_block);

		ptr_on_block = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(ptr_on_block) + block_metadata_size() + size_current_block);

		blocks_info.push_back(info_current_block);
	}

	return blocks_info;
}

inline std::string allocator_buddies_system::get_typename() const noexcept
{
	debug_with_guard("get_typename() method called");
    return "allocator buddies system";
}

void allocator_buddies_system::free_memory()
{
	if (_trusted_memory == nullptr) {
		return;
	}

	debug_with_guard("free_memory() method called");
	allocator::destruct(&get_mutex());
	deallocate_with_guard(_trusted_memory);
}

constexpr size_t allocator_buddies_system::allocator_metadata_size() const
{
	return sizeof(allocator *) + sizeof(logger *) + sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t) + sizeof(void *);
}

constexpr size_t allocator_buddies_system::block_metadata_size() const
{
	return sizeof(void *) + sizeof(bool) + sizeof(size_t) + sizeof(void *) + sizeof(void *);
}

void allocator_buddies_system::log_trusted_memory_dump() const
{
	debug_with_guard("log_trusted_memory_dump() method called");

	if (_trusted_memory == nullptr || get_logger() == nullptr)
	{
		return;
	}

	std::ostringstream str("");
	str << "Allocator trusted memory dump: |";

	auto blocks_state = get_blocks_info();

	for (auto it = blocks_state.begin(); it != blocks_state.end(); ++it)
	{
		str << (it->is_block_occupied ? "anc " : "avl ") << it->block_size << "|";
	}
	information_with_guard(str.str());
}