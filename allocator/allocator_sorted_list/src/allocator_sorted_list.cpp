#include <not_implemented.h>

#include "../include/allocator_sorted_list.h"

#include <sstream>

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
	if(space_size < block_metadata_size()) {
		throw std::logic_error("Can't initialize allocator instance");
	}

	size_t memory_size = space_size + allocator_metadata_size();

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
	*block_size_placement = space_size - block_metadata_size();

	debug_with_guard("trusted " + std::to_string(space_size) + " bytes of memory allocator_sorted_list()");
	debug_with_guard("allocator successfully created allocator_sorted_list()");
}

[[nodiscard]] void *allocator_sorted_list::allocate(
		size_t value_size,
		size_t values_count)
{
	std::lock_guard<std::mutex> lock(get_mutex());

	debug_with_guard("want allocate " + std::to_string(value_size) + " " + std::to_string(values_count) + " allocate()");

	if(_trusted_memory == nullptr)
		throw std::logic_error("allocator instance state was moved");

	void *target_block = nullptr, *previous_to_target_block = nullptr;
	size_t requested_size = value_size *  values_count;
	size_t target_block_size;

	{
		void *current_block, *previous_block = nullptr;
		current_block = get_ptr_on_first_available_block();
		allocator_with_fit_mode::fit_mode fit_mode = get_fit_mode();

		while(current_block != nullptr) {
			size_t current_block_size = get_size_block_without_metadata(current_block);

			if(current_block_size >= requested_size && (fit_mode == allocator_with_fit_mode::fit_mode::first_fit ||
														(fit_mode == allocator_with_fit_mode::fit_mode::the_best_fit
														&& (target_block == nullptr || current_block_size <
														target_block_size)) || (fit_mode == allocator_with_fit_mode::fit_mode::the_worst_fit && (target_block == nullptr || current_block_size < target_block_size))))
			{
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
		void **next_available_block = reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(target_block) + block_metadata_size() + requested_size);
		*next_available_block = get_ptr_on_next_available_block(target_block);

		size_t *next_available_block_size = reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(next_available_block) + sizeof(void *));
		*next_available_block_size = target_block_size - requested_size - block_metadata_size();

		(previous_to_target_block == nullptr
		 ? get_ptr_on_first_available_block()
		 : get_ptr_on_next_available_block(previous_to_target_block)) = next_available_block;
	}
	else
	{
		(previous_to_target_block == nullptr)
		? get_ptr_on_first_available_block()
		: get_ptr_on_next_available_block(previous_to_target_block) = get_ptr_on_next_available_block(target_block);
	}

	*reinterpret_cast<void **>(target_block) = _trusted_memory;

	*reinterpret_cast<size_t *>(reinterpret_cast<void **>(target_block) + 1) = requested_size;

	debug_with_guard("memory has been successfully allocated allocate()");

	log_trusted_memory_dump();

	return reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(target_block) + block_metadata_size());
}

void allocator_sorted_list::deallocate(
		void *at)
{
	std::lock_guard<std::mutex> lock(get_mutex());

	if(_trusted_memory == nullptr)
		throw std::logic_error("allocator instance state was moved");

	at = reinterpret_cast<void *>(reinterpret_cast<unsigned char*>(at) - block_metadata_size());

	if (at == nullptr || at < reinterpret_cast<unsigned char *>(_trusted_memory) + allocator_metadata_size() || at > reinterpret_cast<unsigned char *>(_trusted_memory) + allocator_metadata_size() + get_size_allocator_without_metadata() - block_metadata_size()) {
		error_with_guard("invalid block address");
		throw std::logic_error("invalid block address");
	}

	if (*reinterpret_cast<void **>(at) != _trusted_memory) {
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

	get_ptr_on_next_available_block(at) = right_available_block;
	(left_available_block == nullptr
		? get_ptr_on_first_available_block()
		: get_ptr_on_next_available_block(left_available_block)) = at;

	if (right_available_block != nullptr && ((reinterpret_cast<unsigned char *>(at) + block_metadata_size() +
		get_size_block_without_metadata(at)) == right_available_block))
	{
		get_ptr_on_next_available_block(at) = get_ptr_on_next_available_block(right_available_block);
		set_block_size(at) += get_size_block_without_metadata(right_available_block) + block_metadata_size();
	}

	if(left_available_block != nullptr && ((reinterpret_cast<unsigned char *>(at) - get_size_block_without_metadata(left_available_block) - block_metadata_size()) == left_available_block))
	{
		get_ptr_on_next_available_block(left_available_block) = get_ptr_on_next_available_block(at);
		set_block_size(left_available_block) += get_size_block_without_metadata(at) + block_metadata_size();
	}

	debug_with_guard("block successfully deallocated deallocate()");

	log_trusted_memory_dump();
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
	return *reinterpret_cast<logger**>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *));
}

inline std::mutex &allocator_sorted_list::get_mutex() const {
	debug_with_guard("get_mutex()");
	return *reinterpret_cast<std::mutex *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) +
										   sizeof(logger *));
}

inline allocator_with_fit_mode::fit_mode &allocator_sorted_list::get_fit_mode() const {
	debug_with_guard("get_fit_mode()");
	return *reinterpret_cast<allocator_with_fit_mode::fit_mode *>(reinterpret_cast<unsigned char *>(_trusted_memory)
																  + sizeof(allocator *) + sizeof(logger *) + sizeof(std::mutex));
}

inline size_t allocator_sorted_list::get_size_allocator_without_metadata() const {
	debug_with_guard("get_size_allocator_without_metadata");
	return *reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) +
									   sizeof(logger *) + sizeof(std::mutex) + sizeof
											   (allocator_with_fit_mode::fit_mode));
}

inline void *&allocator_sorted_list::get_ptr_on_first_available_block() const {
	debug_with_guard("get_ptr_on_first_available_block()");
	return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator*) + sizeof(logger *) + sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t));
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
	debug_with_guard("get_blocks_info() method called");

	std::vector<allocator_sorted_list::block_info> blocks_info;
	allocator_sorted_list::block_info info_current_block {};

	void **memory_after_trusted_for_allocator = reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(_trusted_memory) + allocator_metadata_size() + get_size_allocator_without_metadata());
	void *ptr_on_available_block = get_ptr_on_first_available_block();
	void *ptr = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(_trusted_memory) +
										 allocator_metadata_size());

	while (ptr != memory_after_trusted_for_allocator) {
		info_current_block.is_block_occupied = ptr != ptr_on_available_block;

		// TODO: get block size operations might be different for available and ancillary blocks
		info_current_block.block_size = info_current_block.is_block_occupied
										? get_size_block_without_metadata(ptr) + block_metadata_size()
										: get_size_block_without_metadata(ptr) + block_metadata_size();

		if (!info_current_block.is_block_occupied)
		{
			ptr_on_available_block = get_ptr_on_next_available_block(ptr_on_available_block);
		}

		blocks_info.push_back(info_current_block);

		ptr = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(ptr) + block_metadata_size() +
									   get_size_block_without_metadata(ptr));
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

inline size_t &allocator_sorted_list::set_block_size(void *block)
{
	debug_with_guard("set_block_size()");
	return *reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(block) + sizeof(allocator *));
}

void allocator_sorted_list::free_memory() {
	if(_trusted_memory == nullptr)
		return;
	debug_with_guard("free_memory()");
	allocator::destruct(&get_mutex());

	deallocate_with_guard(_trusted_memory); //get_allocator() == nullptr ? ::operator delete (_trusted_memory) :get_allocator() -> deallocate(_trusted_memory);
}

void allocator_sorted_list::log_trusted_memory_dump() const
{
	debug_with_guard("log_trusted_memory_dump() method called");

	if (get_logger() == nullptr)
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