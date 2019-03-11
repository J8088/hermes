/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#ifndef HERMES_SUPPORT_COPYABLEVECTOR_H
#define HERMES_SUPPORT_COPYABLEVECTOR_H

#include "hermes/Support/CheckedMalloc.h"
#include "hermes/VM/GC.h"

#include "llvm/Support/Compiler.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>

namespace hermes {
namespace vm {

/// CopyableVector is a vector that we ensure can always be trivially byte
/// copyable. We ensure there are no internal pointers, only pointers into the
/// C++ heap.
///
/// This allows use of the CopyableVector in contexts such as within GC cells,
/// because the internal fields cannot be invalidated by copying the bytes.
///
/// Note that this means that CopyableVector can only be copied at byte-level.
/// Any moves, copying, or assignment are disallowed, so the only legal way to
/// copy CopyableVector is by using memcpy() and friends.
///
/// The reallocation strategy is as follows:
/// - If we attempt to push_back and there isn't enough capacity,
///   we reallocate a double capacity space, std::move all the elements to the
///   new space, and then free the old space.
///
/// For simplicity, CopyableVector does not support insertion or removal
/// anywhere except the end of the vector.
template <typename T>
class CopyableVector {
 public:
  using size_type = size_t;

 private:
  /// Pointer to the start of the vector's storage in the C++ heap.
  T *start_;

  /// Number of elements currently in this vector.
  size_type size_;

  /// Total reserved capacity in the vector.
  size_type capacity_;

 public:
  /// Maximum capacity for the vector.
  /// Asserts if we ever cross this capacity.
  static constexpr size_type kMaxCapacity =
      std::numeric_limits<size_type>::max() / sizeof(T);

  /// Create a CopyableVector.
  /// \param capacity the initial capacity. If less than 1, defaults to 1.
  /// \pre capacity is at most kMaxCapacity.
  explicit CopyableVector(size_type capacity = 1)
      : size_(0), capacity_(std::max((size_type)1, capacity)) {
    assert(capacity_ <= kMaxCapacity && "capacity overflow for CopyableVector");
    start_ = static_cast<T *>(checkedMalloc(capacity_ * sizeof(T)));
  }

  /// The destructor frees the memory allocated on the C++ heap.
  ~CopyableVector() {
    for (auto &v : *this) {
      v.~T();
    }
    free(start_);
  }

  /// CopyableVector cannot be copied or moved through conventional means,
  /// as this would result in a double-free of the vector's storage.
  CopyableVector(const CopyableVector &) = delete;
  CopyableVector(CopyableVector &&) = delete;
  CopyableVector &operator=(const CopyableVector &) = delete;
  CopyableVector &operator=(CopyableVector &&) = delete;

  /// \return the number of elements in this vector.
  size_type size() const {
    return size_;
  }

  /// \return true if the vector contains no elements.
  bool empty() const {
    return size_ == 0;
  }

  /// \return number of elements that can be held in allocated storage.
  size_type capacity() const {
    return capacity_;
  }

  /// \return the number of bytes allocated in the C++ heap.
  size_type capacity_in_bytes() const {
    return capacity_ * sizeof(T);
  }

  /// \return the \param i element.
  const T &operator[](size_type i) const {
    assert(i < size_ && "Out of bounds access in CopyableVector");
    return start_[i];
  }

  /// \return the \param i element.
  T &operator[](size_type i) {
    assert(i < size_ && "Out of bounds access in CopyableVector");
    return start_[i];
  }

  /// \return a pointer to the first element.
  const T *begin() const {
    return start_;
  }

  /// \return a pointer to the first element.
  T *begin() {
    return start_;
  }

  /// \return a pointer right after the last element.
  const T *end() const {
    return start_ + size_;
  }

  /// \return a pointer right after the last element.
  T *end() {
    return start_ + size_;
  }

  /// Push a new element on the vector.
  /// If this requires a resize, moves the underlying elements into a newly
  /// allocated region.
  void push_back(const T &elem, GC *gc) {
    if (LLVM_UNLIKELY(size_ == capacity_)) {
      if (capacity_ * 2 < capacity_) {
        // Capacity overflowed.
        gc->oom();
      }
      grow(capacity_ * 2);
    }
    new (start_ + size_) T(elem);
    ++size_;
  }

  /// Push a new element on the vector.
  /// If this requires a resize, moves the underlying elements into a newly
  /// allocated region.
  void push_back(T &&elem, GC *gc) {
    if (LLVM_UNLIKELY(size_ == capacity_)) {
      if (capacity_ * 2 < capacity_) {
        // Capacity overflowed.
        gc->oom();
      }
      grow(capacity_ * 2);
    }
    new (start_ + size_) T(std::move(elem));
    ++size_;
  }

  /// Pop the last element off the vector.
  /// \pre the vector is not empty.
  void pop_back() {
    assert(!empty() && "Cannot pop from empty vector");
    start_[size_ - 1].~T();
    --size_;
  }

  /// Ensure that the capacity of the storage is at least \p capacity elements.
  void reserve(size_type capacity) {
    if (capacity > capacity_) {
      grow(capacity);
    }
  }

 private:
  /// Grow the vector storage to have capacity \p newCapacity.
  /// \pre the \p newCapacity is bigger than the current capacity.
  void grow(size_type newCapacity) {
    assert(newCapacity > capacity_ && "Calls to grow() must grow the vector");
    T *newStart = static_cast<T *>(checkedMalloc(newCapacity * sizeof(T)));
    for (T *src = start_, *end = start_ + size_, *target = newStart; src < end;
         ++src, ++target) {
      new (target) T(std::move(*src));
      src->~T();
    }
    free(start_);
    start_ = newStart;
    capacity_ = newCapacity;
  }
};

} // namespace vm
} // namespace hermes

#endif