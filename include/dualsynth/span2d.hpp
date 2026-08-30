#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

#if defined(_MSC_VER)
  #define SPAN2D_RESTRICT __restrict
  #define SPAN2D_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
  #define SPAN2D_RESTRICT __restrict__
  #define SPAN2D_FORCEINLINE __attribute__((always_inline)) inline
#else
  #define SPAN2D_RESTRICT
  #define SPAN2D_FORCEINLINE inline
#endif

namespace span2d {

template <class T>
class Row {
public:
  using element_type    = T;
  using value_type      = std::remove_cv_t<T>;
  using pointer         = T*;
  using const_pointer   = const T*;
  using reference       = T&;
  using const_reference = const T&;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;
  using iterator        = pointer;
  using const_iterator  = const_pointer;

  constexpr Row() noexcept : data_(nullptr), size_(0) {}
  constexpr Row(pointer data, size_type size) noexcept : data_(data), size_(size) {}

  template <class U, typename = std::enable_if_t<std::is_same<const U, T>::value>>
  constexpr Row(const Row<U>& other) noexcept : data_(other.data()), size_(other.size()) {}

  [[nodiscard]] constexpr pointer data() const noexcept { return data_; }
  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr size_type size_bytes() const noexcept { return size_ * sizeof(T); }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

  template <class Index, typename = std::enable_if_t<std::is_integral_v<Index>>>
  [[nodiscard]] SPAN2D_FORCEINLINE reference operator[](Index idx) const noexcept {
    return data_[idx];
  }

  [[nodiscard]] constexpr iterator begin() const noexcept { return data_; }
  [[nodiscard]] constexpr iterator end() const noexcept { return data_ + size_; }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return data_; }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return data_ + size_; }

  [[nodiscard]] constexpr Row subspan(size_type offset, size_type count) const noexcept {
    return Row(data_ + offset, count);
  }

private:
  pointer SPAN2D_RESTRICT data_ = nullptr;
  size_type size_ = 0;
};

template <class T>
class RowCursor {
public:
  using element_type    = T;
  using pointer         = T*;
  using reference       = T&;
  using size_type       = std::int32_t;
  using difference_type = std::int32_t;
  using row_type        = Row<T>;

  constexpr RowCursor() noexcept = default;
  constexpr RowCursor(pointer data, size_type width, difference_type stride_elements) noexcept
    : ptr_(data), width_(width), stride_(stride_elements) {}

  template <class U, typename = std::enable_if_t<std::is_same<const U, T>::value>>
  constexpr RowCursor(const RowCursor<U>& other) noexcept
    : ptr_(other.ptr()), width_(other.width()), stride_(other.stride()) {}

  [[nodiscard]] constexpr pointer ptr() const noexcept { return ptr_; }
  [[nodiscard]] constexpr size_type width() const noexcept { return width_; }
  [[nodiscard]] constexpr difference_type stride() const noexcept { return stride_; }

  [[nodiscard]] SPAN2D_FORCEINLINE row_type row() const noexcept {
    return row_type(ptr_, static_cast<std::size_t>(width_));
  }

  [[nodiscard]] SPAN2D_FORCEINLINE row_type operator*() const noexcept {
    return row();
  }

  [[nodiscard]] SPAN2D_FORCEINLINE row_type operator[](difference_type dy) const noexcept {
    return row_type(ptr_ + static_cast<std::ptrdiff_t>(dy) * stride_, static_cast<std::size_t>(width_));
  }

  SPAN2D_FORCEINLINE RowCursor& operator++() noexcept {
    ptr_ += stride_;
    return *this;
  }

  SPAN2D_FORCEINLINE RowCursor operator++(int) noexcept {
    auto tmp = *this;
    ptr_ += stride_;
    return tmp;
  }

  SPAN2D_FORCEINLINE RowCursor& operator+=(difference_type n) noexcept {
    ptr_ += static_cast<std::ptrdiff_t>(n) * stride_;
    return *this;
  }

  SPAN2D_FORCEINLINE RowCursor& operator--() noexcept {
    ptr_ -= stride_;
    return *this;
  }

  SPAN2D_FORCEINLINE RowCursor operator--(int) noexcept {
    auto tmp = *this;
    ptr_ -= stride_;
    return tmp;
  }

  SPAN2D_FORCEINLINE RowCursor& operator-=(difference_type n) noexcept {
    ptr_ -= static_cast<std::ptrdiff_t>(n) * stride_;
    return *this;
  }

  [[nodiscard]] constexpr bool operator==(const RowCursor& other) const noexcept { return ptr_ == other.ptr_; }
  [[nodiscard]] constexpr bool operator!=(const RowCursor& other) const noexcept { return ptr_ != other.ptr_; }

private:
  pointer SPAN2D_RESTRICT ptr_ = nullptr;
  size_type width_ = 0;
  difference_type stride_ = 0;
};

template <class T>
class Plane {
public:
  using element_type    = T;
  using value_type      = std::remove_cv_t<T>;
  using pointer         = T*;
  using const_pointer   = const T*;
  using reference       = T&;
  using const_reference = const T&;
  using size_type       = std::int32_t;
  using difference_type = std::int32_t;
  using row_type        = Row<T>;
  using cursor_type     = RowCursor<T>;

  constexpr Plane() noexcept = default;

  constexpr Plane(pointer data, size_type width, size_type height, difference_type stride_elements) noexcept
    : data_(data), width_(width), height_(height), stride_(stride_elements) {}

  constexpr Plane(pointer data, size_type width, size_type height, std::ptrdiff_t stride_bytes) noexcept
    : data_(data), width_(width), height_(height),
      stride_(static_cast<difference_type>(stride_bytes / sizeof(T))) {}

  template <class U, typename = std::enable_if_t<std::is_same<const U, T>::value>>
  constexpr Plane(const Plane<U>& other) noexcept
    : data_(other.data()), width_(other.width()), height_(other.height()), stride_(other.stride()) {}

  [[nodiscard]] constexpr pointer data() const noexcept { return data_; }
  [[nodiscard]] constexpr size_type width() const noexcept { return width_; }
  [[nodiscard]] constexpr size_type height() const noexcept { return height_; }
  [[nodiscard]] constexpr difference_type stride() const noexcept { return stride_; }
  [[nodiscard]] constexpr std::ptrdiff_t stride_bytes() const noexcept { return stride_ * sizeof(T); }
  [[nodiscard]] constexpr bool empty() const noexcept { return width_ == 0 || height_ == 0; }

  template <class IndexY, class IndexX,
            typename = std::enable_if_t<std::is_integral_v<IndexY> && std::is_integral_v<IndexX>>>
  [[nodiscard]] SPAN2D_FORCEINLINE reference operator()(IndexY y, IndexX x) const noexcept {
    return data_[static_cast<std::size_t>(y) * stride_ + x];
  }

  [[nodiscard]] SPAN2D_FORCEINLINE pointer SPAN2D_RESTRICT row_ptr(size_type y) const noexcept {
    return data_ + static_cast<std::size_t>(y) * stride_;
  }

  [[nodiscard]] SPAN2D_FORCEINLINE row_type row(size_type y) const noexcept {
    return row_type(row_ptr(y), static_cast<std::size_t>(width_));
  }

  [[nodiscard]] SPAN2D_FORCEINLINE cursor_type cursor(size_type y = 0) const noexcept {
    return cursor_type(row_ptr(y), width_, stride_);
  }

  [[nodiscard]] constexpr cursor_type begin() const noexcept { return cursor(0); }
  [[nodiscard]] constexpr cursor_type end() const noexcept { return cursor(height_); }

  [[nodiscard]] constexpr Plane subplane(size_type x, size_type y, size_type w, size_type h) const noexcept {
    return Plane(data_ + static_cast<std::size_t>(y) * stride_ + x, w, h, stride_);
  }

private:
  pointer SPAN2D_RESTRICT data_ = nullptr;
  size_type width_ = 0;
  size_type height_ = 0;
  difference_type stride_ = 0;
};

template <class T>
using ReadOnlyPlane     = Plane<const T>;
template <class T>
using ReadOnlyRow       = Row<const T>;
template <class T>
using ReadOnlyRowCursor = RowCursor<const T>;

template <class T>
constexpr Plane<T> make_plane(T* data, std::int32_t width, std::int32_t height, std::ptrdiff_t stride_bytes) noexcept {
  return Plane<T>(data, width, height, stride_bytes);
}

template <class T>
constexpr Row<T> make_row(T* data, std::size_t size) noexcept {
  return Row<T>(data, size);
}

} // namespace span2d

namespace ds {

template <class T>
using Plane = ::span2d::Plane<T>;
template <class T>
using Row = ::span2d::Row<T>;
template <class T>
using RowCursor = ::span2d::RowCursor<T>;

using ::span2d::make_plane;
using ::span2d::make_row;

} // namespace ds
