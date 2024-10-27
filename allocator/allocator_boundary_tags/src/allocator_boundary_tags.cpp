#include <not_implemented.h>

#include "../include/allocator_boundary_tags.h"

allocator_boundary_tags::~allocator_boundary_tags()
{
	debug_with_guard("~allocator_boundary_tags() method called");
	free_memory();
}

allocator_boundary_tags::allocator_boundary_tags(
    allocator_boundary_tags &&other) noexcept
{
    if (other._trusted_memory == nullptr) {
		return;
	}

	std::lock_guard<std::mutex> lock(get_mutex());

	_trusted_memory = other._trusted_memory;
	other._trusted_memory = nullptr;

	debug_with_guard("allocator_boundary_tags(allocator_boundary_tags &&) method called");
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
    allocator_boundary_tags &&other) noexcept
{
	debug_with_guard("operator=(allocator_boundary_tags &&) method called");
    if (this != &other) {
		free_memory();

		std::lock_guard<std::mutex> lock(get_mutex());

		_trusted_memory = other._trusted_memory;
		other._trusted_memory = nullptr;
	}
	return *this;
}

allocator_boundary_tags::allocator_boundary_tags(
    size_t space_size,
    allocator *parent_allocator,
    logger *logger,
    allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (space_size < descriptors_size()) {
		throw std::logic_error("Can't initialize allocator instance");
	}

	size_t memory_size = space_size + allocator_metadata_size() + descriptors_size();

	try {
		_trusted_memory = parent_allocator == nullptr
				? ::operator new (memory_size)
				: parent_allocator->allocate(1, memory_size);
	}
	catch (std::bad_alloc const &error) {
		error_with_guard(error.what());
		throw std::bad_alloc();
	}

	allocator **parent_allocator_placement = reinterpret_cast<allocator **>(_trusted_memory);
	*parent_allocator_placement = parent_allocator;

	class logger **logger_placament = reinterpret_cast<class logger **>(parent_allocator_placement + 1);
	*logger_placament = logger;

	std::mutex *synchronizer_placement = reinterpret_cast<std::mutex *>(logger_placament + 1);
	new (reinterpret_cast<void *>(synchronizer_placement)) std::mutex;

	allocator_with_fit_mode::fit_mode *fit_mode_placement = reinterpret_cast<allocator_with_fit_mode::fit_mode *>
			(synchronizer_placement + 1);
	*fit_mode_placement = allocate_fit_mode;

	size_t *allocator_size_without_metadata_without_descriptors_placement = reinterpret_cast<size_t *>
			(fit_mode_placement + 1);
	*allocator_size_without_metadata_without_descriptors_placement = space_size;

	bool *is_block_occupied_upper_border_tag_placement = reinterpret_cast<bool *>
			(allocator_size_without_metadata_without_descriptors_placement + 1);
	*is_block_occupied_upper_border_tag_placement = false;

	size_t *block_size_without_descriptors_upper_border_tag_placement = reinterpret_cast<size_t *>
			(is_block_occupied_upper_border_tag_placement + 1);
	*block_size_without_descriptors_upper_border_tag_placement = space_size;

	bool *is_block_occupied_low_border_tag_placement = reinterpret_cast<bool *>(reinterpret_cast<unsigned char *>
			(block_size_without_descriptors_upper_border_tag_placement + 1) + space_size);
	*is_block_occupied_low_border_tag_placement = false;

	size_t *block_size_without_descriptors_low_border_tag_placement = reinterpret_cast<size_t *>
			(is_block_occupied_low_border_tag_placement + 1);
	*block_size_without_descriptors_low_border_tag_placement = space_size;

	debug_with_guard("trusted " + std::to_string(space_size) + " bytes of memory allocator_boundary_tags()");
}

[[nodiscard]] void *allocator_boundary_tags::allocate(
    size_t value_size,
    size_t values_count)
{
    std::lock_guard<std::mutex> lock(get_mutex());

	debug_with_guard("want allocate " + std::to_string(value_size) + " " + std::to_string(values_count) + " allocate()");

	if(_trusted_memory == nullptr) {
		throw std::logic_error("allocator instance state was moved");
	}

	void *target_block = nullptr;
	size_t requested_size = value_size * values_count;
	size_t target_block_size;

	{
		void *current_block = nullptr;
		void *ptr_on_last_descriptor = reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(_trusted_memory) +
				sizeof(allocator_metadata_size() + get_allocator_size_without_metadata_without_descriptors() +
				descriptor_size()));
		current_block = get_ptr_on_first_block();
		allocator_with_fit_mode::fit_mode fit_mode = get_fit_mode();

		while(current_block < ptr_on_last_descriptor)
		{
			size_t current_block_size = get_block_size_without_descriptors(current_block);

			if(!is_block_occupied(current_block) && current_block_size >= requested_size && (fit_mode ==
			allocator_with_fit_mode::fit_mode::first_fit || (fit_mode ==
			allocator_with_fit_mode::fit_mode::the_best_fit && (target_block == nullptr || current_block_size <
			target_block_size)) || (fit_mode == allocator_with_fit_mode::fit_mode::the_worst_fit && (target_block == nullptr || current_block_size < target_block_size))))
			{
				target_block = current_block;
				target_block_size = current_block_size;

				if(fit_mode == allocator_with_fit_mode::fit_mode::first_fit)
				{
					break;
				}
			}
			current_block = get_ptr_on_next_block(current_block);
		}
	}

	if (target_block == nullptr) {
		error_with_guard("no suitable block allocate()");
		throw std::bad_alloc();
	}

	if(target_block_size - requested_size < descriptors_size()) {
		requested_size = target_block_size;
		warning_with_guard("redefining requested size allocate()");
	}

	if(target_block_size != requested_size) {
		set_status_block_upper_border(target_block, true);

		set_block_size(get_low_descriptor_of_current_block(target_block), target_block_size - requested_size - descriptors_size());

		set_block_size(target_block, requested_size);

		set_status_block_low_border(reinterpret_cast<void *>
			(reinterpret_cast<unsigned char*>(target_block) + descriptor_size() + requested_size), true);

		set_block_size(reinterpret_cast<void *>(reinterpret_cast<unsigned char*>(target_block)
			+ descriptor_size() + requested_size + sizeof(bool)), requested_size);

		set_status_block_upper_border(reinterpret_cast<void *>
			(reinterpret_cast<unsigned char*>(target_block) + descriptors_size() + requested_size), false);

		set_block_size(reinterpret_cast<void *>
					   (reinterpret_cast<unsigned char*>(target_block) + descriptors_size() + requested_size + sizeof
					   (bool)), target_block_size - requested_size - descriptors_size());
	}
	else {
		set_status_block_upper_border(target_block, true);
		set_status_block_low_border(get_low_descriptor_of_current_block(target_block), true);
	}

	return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(target_block) + descriptor_size());
}

void allocator_boundary_tags::deallocate(
    void *at)
{
    throw not_implemented("void allocator_boundary_tags::deallocate(void *)", "your code should be here...");
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
	debug_with_guard("set_fit_mode() method called");
	*reinterpret_cast<allocator_with_fit_mode::fit_mode *>(reinterpret_cast<unsigned char *>(_trusted_memory) +
		sizeof(allocator *) + sizeof(logger *) + sizeof(std::mutex)) = mode;
}

inline allocator *allocator_boundary_tags::get_allocator() const
{
	debug_with_guard("get_allocator() method called");
    return *reinterpret_cast<allocator **>(_trusted_memory);
}

inline logger *allocator_boundary_tags::get_logger() const
{
	return *reinterpret_cast<logger **>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *));
}

inline std::mutex &allocator_boundary_tags::get_mutex() const
{
	debug_with_guard("get_mutex() method called");
	return *reinterpret_cast<std::mutex *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *)
		+ sizeof(logger *));
}

inline allocator_with_fit_mode::fit_mode &allocator_boundary_tags::get_fit_mode() const
{
	debug_with_guard("get_fit_mode() method called");
	return *reinterpret_cast<allocator_with_fit_mode::fit_mode *>(reinterpret_cast<unsigned char *>(_trusted_memory)
																  + sizeof(allocator *) + sizeof(logger *) + sizeof(std::mutex));
}

inline size_t allocator_boundary_tags::get_allocator_size_without_metadata_without_descriptors() const
{
	return *reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(_trusted_memory) + sizeof(allocator *) +
		sizeof(logger *) + sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode));
}

inline void *&allocator_boundary_tags::get_ptr_on_first_block() const
{
	debug_with_guard("get_ptr_on_first_block() method called");
	return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(_trusted_memory) + allocator_metadata_size());
}

inline void *&allocator_boundary_tags::get_ptr_on_next_block(void *current_block) const
{
	debug_with_guard("get_ptr_on_next_block(void *) method called");
	return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(current_block) + descriptors_size() +
			get_block_size_without_descriptors(current_block));
}

inline void *allocator_boundary_tags::get_low_descriptor_of_current_block(void *current_block) const
{
	debug_with_guard("get_low_descriptor_of_current_block(void *) method called");
	return *reinterpret_cast<void **>(reinterpret_cast<unsigned char *>(current_block) + descriptor_size() +
			get_block_size_without_descriptors(current_block));
}

inline size_t allocator_boundary_tags::get_block_size_without_descriptors(void *current_block) const
{
	debug_with_guard("get_block_size_without_descriptors(void *) method called");
	return *reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(current_block) + sizeof(bool));
}

inline bool allocator_boundary_tags::is_block_occupied(void *current_block) const
{
	debug_with_guard("is_block_occupied(void *) method called");
	return *reinterpret_cast<bool *>(current_block);
}

void allocator_boundary_tags::set_status_block_upper_border(void *upper_border_current_block, bool status)
{
	std::string str_status = (status ? "occupied" : "free");
	debug_with_guard("set_status_block_upper_border(void *, bool) method called, status: " + str_status);
	*reinterpret_cast<bool *>(upper_border_current_block) = status;
}

void allocator_boundary_tags::set_status_block_low_border(void *low_border_current_block, bool status)
{
	std::string str_status = (status ? "occupied" : "free");
	debug_with_guard("set_status_block_low_border(void *, bool) method called, status: " + str_status);
	*reinterpret_cast<bool *>(low_border_current_block) = status;
}

void allocator_boundary_tags::set_block_size(void *current_block, size_t block_size)
{
	debug_with_guard("set_block_size(void *, size_t) method called, size: " + std::to_string(block_size));
	*reinterpret_cast<size_t *>(reinterpret_cast<unsigned char *>(current_block) + sizeof(bool)) = block_size;
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const noexcept
{

}

inline std::string allocator_boundary_tags::get_typename() const noexcept
{
	debug_with_guard("get_typename() method called");
    return "allocator boundary tags";
}

constexpr size_t allocator_boundary_tags::descriptors_size() const
{
	return 2 * descriptor_size();
}

constexpr size_t allocator_boundary_tags::descriptor_size() const
{
	return (sizeof(size_t) + sizeof(bool));
}

constexpr size_t allocator_boundary_tags::allocator_metadata_size() const
{
	return sizeof(allocator *) + sizeof(logger *) + sizeof(std::mutex) + sizeof(allocator_with_fit_mode::fit_mode) +
		   sizeof(size_t);
}

void allocator_boundary_tags::free_memory()
{
	if (_trusted_memory == nullptr) {
		return;
	}

	debug_with_guard("free_memory() method called");

	allocator::destruct(&get_mutex()); // get_mutex().~mutex();
	deallocate_with_guard(_trusted_memory);
}