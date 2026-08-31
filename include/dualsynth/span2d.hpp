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

// Forward declarations
template <class T, bool IsRestrict> class BasicRow;
template <class T, bool IsRestrict> class BasicRowCursor;
template <class T, bool IsRestrict> class BasicPlane;

// ============================================================================
// 1. BasicRow<T, IsRestrict>
// ============================================================================
template <class T, bool IsRestrict>
class BasicRow {
public:
  using element_type    = T;
  using value_type      = std::remove_cv_t<T>;
  using pointer         = std::conditional_t<IsRestrict, T* SPAN2D_RESTRICT, T*>;
  using const_pointer   = std::conditional_t<IsRestrict, const T* SPAN2D_RESTRICT, const T*>;
  using reference       = T&;
  using const_reference = const T&;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;
  using iterator        = pointer;
  using const_iterator  = const_pointer;

  static constexpr bool is_restrict = IsRestrict;

  constexpr BasicRow() noexcept : data_(nullptr), size_(0) {}
  constexpr BasicRow(pointer data, size_type size) noexcept : data_(data), size_(size) {}
  constexpr BasicRow(pointer first, pointer last) noexcept : data_(first), size_(static_cast<size_type>(last - first)) {}

  template <std::size_t N>
  constexpr BasicRow(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

  template <class Container,
            typename = std::enable_if_t<
              !std::is_same_v<std::decay_t<Container>, BasicRow> &&
              !std::is_pointer_v<std::decay_t<Container>> &&
              std::is_convertible_v<decltype(std::data(std::declval<Container&>())), pointer>>>
  constexpr BasicRow(Container& c) noexcept
    : data_(std::data(c)), size_(static_cast<size_type>(std::size(c))) {}

  template <class Container,
            typename = std::enable_if_t<
              !std::is_same_v<std::decay_t<Container>, BasicRow> &&
              !std::is_pointer_v<std::decay_t<Container>> &&
              std::is_convertible_v<decltype(std::data(std::declval<const Container&>())), pointer>>>
  constexpr BasicRow(const Container& c) noexcept
    : data_(std::data(c)), size_(static_cast<size_type>(std::size(c))) {}

  // Conversion from non-const to const
  template <class U, bool OtherRestrict,
            typename = std::enable_if_t<std::is_same<const U, T>::value && (OtherRestrict == IsRestrict)>>
  constexpr BasicRow(const BasicRow<U, OtherRestrict>& other) noexcept
    : data_(other.data()), size_(other.size()) {}

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

  [[nodiscard]] constexpr BasicRow subspan(size_type offset, size_type count) const noexcept {
    return BasicRow(data_ + offset, count);
  }

  // Converters
  [[nodiscard]] SPAN2D_FORCEINLINE BasicRow<T, true> as_restrict() const noexcept {
    return BasicRow<T, true>(data_, size_);
  }

  [[nodiscard]] SPAN2D_FORCEINLINE BasicRow<T, false> as_unrestricted() const noexcept {
    return BasicRow<T, false>(data_, size_);
  }

private:
  pointer data_ = nullptr;
  size_type size_ = 0;
};

// ============================================================================
// 2. BasicRowCursor<T, IsRestrict>
// ============================================================================
template <class T, bool IsRestrict>
class BasicRowCursor {
public:
  using element_type    = T;
  using pointer         = std::conditional_t<IsRestrict, T* SPAN2D_RESTRICT, T*>;
  using reference       = T&;
  using size_type       = std::int32_t;
  using difference_type = std::int32_t;
  using row_type        = BasicRow<T, IsRestrict>;

  static constexpr bool is_restrict = IsRestrict;

  constexpr BasicRowCursor() noexcept = default;
  constexpr BasicRowCursor(pointer data, size_type width, difference_type stride_elements) noexcept
    : ptr_(data), width_(width), stride_(stride_elements) {}

  template <class U, bool OtherRestrict,
            typename = std::enable_if_t<std::is_same<const U, T>::value && (OtherRestrict == IsRestrict)>>
  constexpr BasicRowCursor(const BasicRowCursor<U, OtherRestrict>& other) noexcept
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

  template <class IndexY, typename = std::enable_if_t<std::is_integral_v<IndexY>>>
  [[nodiscard]] SPAN2D_FORCEINLINE row_type operator[](IndexY dy) const noexcept {
    return row_type(ptr_ + static_cast<std::ptrdiff_t>(dy) * stride_, static_cast<std::size_t>(width_));
  }

  SPAN2D_FORCEINLINE BasicRowCursor& operator++() noexcept {
    ptr_ += stride_;
    return *this;
  }

  SPAN2D_FORCEINLINE BasicRowCursor operator++(int) noexcept {
    auto tmp = *this;
    ptr_ += stride_;
    return tmp;
  }

  template <class Offset, typename = std::enable_if_t<std::is_integral_v<Offset>>>
  SPAN2D_FORCEINLINE BasicRowCursor& operator+=(Offset n) noexcept {
    ptr_ += static_cast<std::ptrdiff_t>(n) * stride_;
    return *this;
  }

  SPAN2D_FORCEINLINE BasicRowCursor& operator--() noexcept {
    ptr_ -= stride_;
    return *this;
  }

  SPAN2D_FORCEINLINE BasicRowCursor operator--(int) noexcept {
    auto tmp = *this;
    ptr_ -= stride_;
    return tmp;
  }

  template <class Offset, typename = std::enable_if_t<std::is_integral_v<Offset>>>
  SPAN2D_FORCEINLINE BasicRowCursor& operator-=(Offset n) noexcept {
    ptr_ -= static_cast<std::ptrdiff_t>(n) * stride_;
    return *this;
  }

  template <bool OtherRestrict>
  [[nodiscard]] constexpr bool operator==(const BasicRowCursor<T, OtherRestrict>& other) const noexcept {
    return ptr_ == other.ptr();
  }

  template <bool OtherRestrict>
  [[nodiscard]] constexpr bool operator!=(const BasicRowCursor<T, OtherRestrict>& other) const noexcept {
    return ptr_ != other.ptr();
  }

  // Converters
  [[nodiscard]] SPAN2D_FORCEINLINE BasicRowCursor<T, true> as_restrict() const noexcept {
    return BasicRowCursor<T, true>(ptr_, width_, stride_);
  }

  [[nodiscard]] SPAN2D_FORCEINLINE BasicRowCursor<T, false> as_unrestricted() const noexcept {
    return BasicRowCursor<T, false>(ptr_, width_, stride_);
  }

private:
  pointer ptr_ = nullptr;
  size_type width_ = 0;
  difference_type stride_ = 0;
};

// ============================================================================
// 3. BasicPlane<T, IsRestrict>
// ============================================================================
template <class T, bool IsRestrict>
class BasicPlane {
public:
  using element_type    = T;
  using value_type      = std::remove_cv_t<T>;
  using pointer         = std::conditional_t<IsRestrict, T* SPAN2D_RESTRICT, T*>;
  using const_pointer   = std::conditional_t<IsRestrict, const T* SPAN2D_RESTRICT, const T*>;
  using reference       = T&;
  using const_reference = const T&;
  using size_type       = std::int32_t;
  using difference_type = std::int32_t;
  using row_type        = BasicRow<T, IsRestrict>;
  using cursor_type     = BasicRowCursor<T, IsRestrict>;

  static constexpr bool is_restrict = IsRestrict;

  constexpr BasicPlane() noexcept = default;

  constexpr BasicPlane(pointer data, size_type width, size_type height, difference_type stride_elements) noexcept
    : data_(data), width_(width), height_(height), stride_(stride_elements) {}

  constexpr BasicPlane(pointer data, size_type width, size_type height, std::ptrdiff_t stride_bytes) noexcept
    : data_(data), width_(width), height_(height),
      stride_(static_cast<difference_type>(stride_bytes / sizeof(T))) {}

  template <class U, bool OtherRestrict,
            typename = std::enable_if_t<std::is_same<const U, T>::value && (OtherRestrict == IsRestrict)>>
  constexpr BasicPlane(const BasicPlane<U, OtherRestrict>& other) noexcept
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

  template <class IndexY, typename = std::enable_if_t<std::is_integral_v<IndexY>>>
  [[nodiscard]] SPAN2D_FORCEINLINE pointer row_ptr(IndexY y) const noexcept {
    return data_ + static_cast<std::size_t>(y) * stride_;
  }

  template <class IndexY, typename = std::enable_if_t<std::is_integral_v<IndexY>>>
  [[nodiscard]] SPAN2D_FORCEINLINE row_type row(IndexY y) const noexcept {
    return row_type(row_ptr(y), static_cast<std::size_t>(width_));
  }

  template <class IndexY = size_type, typename = std::enable_if_t<std::is_integral_v<IndexY>>>
  [[nodiscard]] SPAN2D_FORCEINLINE cursor_type cursor(IndexY y = 0) const noexcept {
    return cursor_type(row_ptr(y), width_, stride_);
  }

  [[nodiscard]] constexpr cursor_type begin() const noexcept { return cursor(0); }
  [[nodiscard]] constexpr cursor_type end() const noexcept { return cursor(height_); }

  [[nodiscard]] constexpr BasicPlane subplane(size_type x, size_type y, size_type w, size_type h) const noexcept {
    return BasicPlane(data_ + static_cast<std::size_t>(y) * stride_ + x, w, h, stride_);
  }

  // Converters
  [[nodiscard]] SPAN2D_FORCEINLINE BasicPlane<T, true> as_restrict() const noexcept {
    return BasicPlane<T, true>(data_, width_, height_, stride_);
  }

  [[nodiscard]] SPAN2D_FORCEINLINE BasicPlane<T, false> as_unrestricted() const noexcept {
    return BasicPlane<T, false>(data_, width_, height_, stride_);
  }

private:
  pointer data_ = nullptr;
  size_type width_ = 0;
  size_type height_ = 0;
  difference_type stride_ = 0;
};

// ============================================================================
// 4. Aliases
// ============================================================================

// Standard Safe Views (no restrict)
template <class T> using Row               = BasicRow<T, false>;
template <class T> using Span              = BasicRow<T, false>;
template <class T> using RowCursor         = BasicRowCursor<T, false>;
template <class T> using Plane             = BasicPlane<T, false>;

// High-Performance Views (with restrict)
template <class T> using RestrictRow       = BasicRow<T, true>;
template <class T> using RestrictSpan      = BasicRow<T, true>;
template <class T> using RestrictRowCursor = BasicRowCursor<T, true>;
template <class T> using RestrictPlane     = BasicPlane<T, true>;

// Read-Only Aliases
template <class T> using ReadOnlyPlane             = Plane<const T>;
template <class T> using ReadOnlyRestrictPlane     = RestrictPlane<const T>;
template <class T> using ReadOnlyRow               = Row<const T>;
template <class T> using ReadOnlySpan              = Span<const T>;
template <class T> using ReadOnlyRestrictRow       = RestrictRow<const T>;
template <class T> using ReadOnlyRestrictSpan      = RestrictSpan<const T>;
template <class T> using ReadOnlyRowCursor         = RowCursor<const T>;
template <class T> using ReadOnlyRestrictRowCursor = RestrictRowCursor<const T>;

// ============================================================================
// 5. Free Conversion Functions
// ============================================================================

template <class T, bool IsRestrict>
[[nodiscard]] SPAN2D_FORCEINLINE constexpr BasicRow<T, true> as_restrict(BasicRow<T, IsRestrict> r) noexcept {
  return r.as_restrict();
}

template <class T, bool IsRestrict>
[[nodiscard]] SPAN2D_FORCEINLINE constexpr BasicRow<T, false> as_unrestricted(BasicRow<T, IsRestrict> r) noexcept {
  return r.as_unrestricted();
}

template <class T, bool IsRestrict>
[[nodiscard]] SPAN2D_FORCEINLINE constexpr BasicRowCursor<T, true> as_restrict(BasicRowCursor<T, IsRestrict> c) noexcept {
  return c.as_restrict();
}

template <class T, bool IsRestrict>
[[nodiscard]] SPAN2D_FORCEINLINE constexpr BasicRowCursor<T, false> as_unrestricted(BasicRowCursor<T, IsRestrict> c) noexcept {
  return c.as_unrestricted();
}

template <class T, bool IsRestrict>
[[nodiscard]] SPAN2D_FORCEINLINE constexpr BasicPlane<T, true> as_restrict(BasicPlane<T, IsRestrict> p) noexcept {
  return p.as_restrict();
}

template <class T, bool IsRestrict>
[[nodiscard]] SPAN2D_FORCEINLINE constexpr BasicPlane<T, false> as_unrestricted(BasicPlane<T, IsRestrict> p) noexcept {
  return p.as_unrestricted();
}

// ============================================================================
// 6. Factory Functions
// ============================================================================

template <class T>
constexpr Plane<T> make_plane(T* data, std::int32_t width, std::int32_t height, std::ptrdiff_t stride_bytes) noexcept {
  return Plane<T>(data, width, height, stride_bytes);
}

template <class T>
constexpr RestrictPlane<T> make_restrict_plane(T* data, std::int32_t width, std::int32_t height, std::ptrdiff_t stride_bytes) noexcept {
  return RestrictPlane<T>(data, width, height, stride_bytes);
}

template <class T>
constexpr Row<T> make_row(T* data, std::size_t size) noexcept {
  return Row<T>(data, size);
}

template <class T>
constexpr RestrictRow<T> make_restrict_row(T* data, std::size_t size) noexcept {
  return RestrictRow<T>(data, size);
}

template <class T>
constexpr Span<T> make_span(T* data, std::size_t size) noexcept {
  return Span<T>(data, size);
}

template <class T>
constexpr RestrictSpan<T> make_restrict_span(T* data, std::size_t size) noexcept {
  return RestrictSpan<T>(data, size);
}

} // namespace span2d

namespace ds {

template <class T> using Plane             = ::span2d::Plane<T>;
template <class T> using RestrictPlane     = ::span2d::RestrictPlane<T>;
template <class T> using Row               = ::span2d::Row<T>;
template <class T> using RestrictRow       = ::span2d::RestrictRow<T>;
template <class T> using Span              = ::span2d::Span<T>;
template <class T> using RestrictSpan      = ::span2d::RestrictSpan<T>;
template <class T> using RowCursor         = ::span2d::RowCursor<T>;
template <class T> using RestrictRowCursor = ::span2d::RestrictRowCursor<T>;

template <class T> using ReadOnlyPlane             = ::span2d::ReadOnlyPlane<T>;
template <class T> using ReadOnlyRestrictPlane     = ::span2d::ReadOnlyRestrictPlane<T>;
template <class T> using ReadOnlyRow               = ::span2d::ReadOnlyRow<T>;
template <class T> using ReadOnlyRestrictRow       = ::span2d::ReadOnlyRestrictRow<T>;
template <class T> using ReadOnlySpan              = ::span2d::ReadOnlySpan<T>;
template <class T> using ReadOnlyRestrictSpan      = ::span2d::ReadOnlyRestrictSpan<T>;
template <class T> using ReadOnlyRowCursor         = ::span2d::ReadOnlyRowCursor<T>;
template <class T> using ReadOnlyRestrictRowCursor = ::span2d::ReadOnlyRestrictRowCursor<T>;

using ::span2d::make_plane;
using ::span2d::make_restrict_plane;
using ::span2d::make_row;
using ::span2d::make_restrict_row;
using ::span2d::make_span;
using ::span2d::make_restrict_span;
using ::span2d::as_restrict;
using ::span2d::as_unrestricted;

} // namespace ds
