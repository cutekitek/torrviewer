#include "config.hpp"

#if defined(TORRVIEW_HAVE_TRACY)

#include <tracy/Tracy.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace {

constexpr int allocation_callstack_depth = TORRVIEW_TRACY_ALLOCATION_CALLSTACK_DEPTH;
constexpr std::size_t allocation_table_size = 1U << 20U;
void* const tombstone = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));

thread_local bool inside_allocation_hook = false;
std::array<std::atomic<void*>, allocation_table_size> reported_allocations{};

class AllocationHookScope {
public:
  AllocationHookScope() noexcept : active_(!inside_allocation_hook) {
    if (active_) {
      inside_allocation_hook = true;
    }
  }

  AllocationHookScope(const AllocationHookScope&) = delete;
  AllocationHookScope& operator=(const AllocationHookScope&) = delete;

  ~AllocationHookScope() {
    if (active_) {
      inside_allocation_hook = false;
    }
  }

  bool active() const noexcept { return active_; }

private:
  bool active_ = false;
};

std::size_t pointer_hash(void* pointer) noexcept {
  auto value = reinterpret_cast<std::uintptr_t>(pointer);
  value >>= 4U;
  value ^= value >> 33U;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33U;
  return static_cast<std::size_t>(value) & (allocation_table_size - 1U);
}

bool remember_reported_allocation(void* pointer) noexcept {
  const std::size_t start = pointer_hash(pointer);
  for (std::size_t offset = 0; offset < allocation_table_size; ++offset) {
    std::atomic<void*>& slot = reported_allocations[(start + offset) & (allocation_table_size - 1U)];
    void* expected = nullptr;
    if (slot.compare_exchange_strong(expected, pointer, std::memory_order_release,
                                     std::memory_order_relaxed)) {
      return true;
    }
    if (expected == tombstone &&
        slot.compare_exchange_strong(expected, pointer, std::memory_order_release,
                                     std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

bool forget_reported_allocation(void* pointer) noexcept {
  const std::size_t start = pointer_hash(pointer);
  for (std::size_t offset = 0; offset < allocation_table_size; ++offset) {
    std::atomic<void*>& slot = reported_allocations[(start + offset) & (allocation_table_size - 1U)];
    void* current = slot.load(std::memory_order_acquire);
    if (current == pointer) {
      if (slot.compare_exchange_strong(current, tombstone, std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        return true;
      }
      --offset;
      continue;
    }
    if (current == nullptr) {
      return false;
    }
  }
  return false;
}

void record_allocation(void* pointer, std::size_t size) noexcept {
  if (pointer == nullptr) {
    return;
  }

  AllocationHookScope scope;
  if (scope.active() && remember_reported_allocation(pointer)) {
    TracyAllocS(pointer, size, allocation_callstack_depth);
  }
}

void record_free(void* pointer) noexcept {
  if (pointer == nullptr || !forget_reported_allocation(pointer)) {
    return;
  }

  AllocationHookScope scope;
  if (scope.active()) {
    TracyFreeS(pointer, allocation_callstack_depth);
  }
}

std::size_t normalized_size(std::size_t size) noexcept { return size == 0 ? 1 : size; }

void* allocate(std::size_t size) noexcept {
  void* pointer = std::malloc(normalized_size(size));
  record_allocation(pointer, size);
  return pointer;
}

void* allocate_or_throw(std::size_t size) {
  if (void* pointer = allocate(size)) {
    return pointer;
  }
  throw std::bad_alloc();
}

void deallocate(void* pointer) noexcept {
  record_free(pointer);
  std::free(pointer);
}

void* allocate_aligned(std::size_t size, std::size_t alignment) noexcept {
  void* pointer = nullptr;
  const std::size_t allocation_size = normalized_size(size);

#if defined(_WIN32)
  pointer = _aligned_malloc(allocation_size, alignment);
#else
  if (posix_memalign(&pointer, alignment, allocation_size) != 0) {
    pointer = nullptr;
  }
#endif

  record_allocation(pointer, size);
  return pointer;
}

void* allocate_aligned_or_throw(std::size_t size, std::size_t alignment) {
  if (void* pointer = allocate_aligned(size, alignment)) {
    return pointer;
  }
  throw std::bad_alloc();
}

void deallocate_aligned(void* pointer) noexcept {
  record_free(pointer);
#if defined(_WIN32)
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}

} // namespace

void* operator new(std::size_t size) { return allocate_or_throw(size); }

void* operator new[](std::size_t size) { return allocate_or_throw(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }

void operator delete(void* pointer) noexcept { deallocate(pointer); }

void operator delete[](void* pointer) noexcept { deallocate(pointer); }

void operator delete(void* pointer, std::size_t) noexcept { deallocate(pointer); }

void operator delete[](void* pointer, std::size_t) noexcept { deallocate(pointer); }

void operator delete(void* pointer, const std::nothrow_t&) noexcept { deallocate(pointer); }

void operator delete[](void* pointer, const std::nothrow_t&) noexcept { deallocate(pointer); }

void* operator new(std::size_t size, std::align_val_t alignment) {
  return allocate_aligned_or_throw(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  return allocate_aligned_or_throw(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
  return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
  return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer, std::align_val_t) noexcept { deallocate_aligned(pointer); }

void operator delete[](void* pointer, std::align_val_t) noexcept { deallocate_aligned(pointer); }

void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
  deallocate_aligned(pointer);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
  deallocate_aligned(pointer);
}

void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
  deallocate_aligned(pointer);
}

void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
  deallocate_aligned(pointer);
}

#endif
