#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BOUNDARY_TAGS_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BOUNDARY_TAGS_H

#include <allocator_guardant.h>
#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <logger_guardant.h>
#include <typename_holder.h>

#include <mutex>

class allocator_boundary_tags final:
    private allocator_guardant,
    public allocator_test_utils,
    public allocator_with_fit_mode,
    private logger_guardant,
    private typename_holder
{

private:
    
    void *_trusted_memory;

public:
    
    ~allocator_boundary_tags() override;
    
    allocator_boundary_tags(
        allocator_boundary_tags const &other) = delete;
    
    allocator_boundary_tags &operator=(
        allocator_boundary_tags const &other) = delete;
    
    allocator_boundary_tags(
        allocator_boundary_tags &&other) noexcept;
    
    allocator_boundary_tags &operator=(
        allocator_boundary_tags &&other) noexcept;

public:
    
    explicit allocator_boundary_tags(
        size_t space_size,
        allocator *parent_allocator = nullptr,
        logger *logger = nullptr,
        allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

public:
    
    [[nodiscard]] void *allocate(
        size_t value_size,
        size_t values_count) override;
    
    void deallocate(
        void *at) override;

public:
    
    inline void set_fit_mode(
        allocator_with_fit_mode::fit_mode mode) override;

private:
    
    inline allocator *get_allocator() const override;

	inline logger *get_logger() const override;

	inline std::mutex &get_mutex() const;

	inline allocator_with_fit_mode::fit_mode &get_fit_mode() const;

	inline size_t get_allocator_size_without_metadata_without_descriptors() const;

	inline std::string get_typename() const noexcept override;

	inline void *&get_ptr_on_first_block() const;

	inline void *&get_ptr_on_next_block(void *current_block) const;

	inline void *get_low_descriptor_of_current_block(void *current_block) const;

	inline size_t get_block_size_without_descriptors(void * current_block) const;

	inline bool is_block_occupied(void *current_block) const;

	void set_status_block_upper_border(void *upper_border_current_block, bool status);

	void set_status_block_low_border(void *low_border_current_block, bool status);

	void set_block_size(void *current_block, size_t block_size);

public:
    
    std::vector<allocator_test_utils::block_info> get_blocks_info() const noexcept override;

private:
    
    constexpr size_t descriptors_size() const;

	constexpr size_t descriptor_size() const;

	constexpr size_t allocator_metadata_size() const;

	void free_memory();
};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BOUNDARY_TAGS_H