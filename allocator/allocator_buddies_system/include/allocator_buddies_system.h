#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H

#include <allocator_guardant.h>
#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <logger_guardant.h>
#include <typename_holder.h>
#include <mutex>

class allocator_buddies_system final:
    private allocator_guardant,
    public allocator_test_utils,
    public allocator_with_fit_mode,
    private logger_guardant,
    private typename_holder
{

private:
    
    void *_trusted_memory;

public:
    
    ~allocator_buddies_system() override;
    
    allocator_buddies_system(
        allocator_buddies_system const &other) = delete;
    
    allocator_buddies_system &operator=(
        allocator_buddies_system const &other) = delete;
    
    allocator_buddies_system(
        allocator_buddies_system &&other) noexcept;
    
    allocator_buddies_system &operator=(
        allocator_buddies_system &&other) noexcept;

public:
    
    explicit allocator_buddies_system(
        size_t space_size_power_of_two,
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

	inline size_t get_allocator_size_without_metadata() const;

	inline void *get_ptr_on_first_available_block() const;

	inline void *get_ptr_on_first_block() const;

	inline void *get_ptr_on_next_available_block(void *current_block) const;

	inline void *get_ptr_on_previous_available_block(void *current_block) const;

	inline size_t get_size_current_block_without_metadata(void *current_block) const;

	inline bool is_block_occupied(void *current_block) const;

public:
    
    std::vector<allocator_test_utils::block_info> get_blocks_info() const noexcept override;

	void log_trusted_memory_dump() const;

private:
    
    inline std::string get_typename() const noexcept override;

	void free_memory();

	constexpr size_t allocator_metadata_size() const;

	constexpr size_t block_metadata_size() const;
};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H
