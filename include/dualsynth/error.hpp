#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace ds {

enum class ErrorCode {
  InvalidArgument,
  UnsupportedFormat,
  HostError,
  InternalError,
};

struct Error {
  ErrorCode code;
  std::string message;
};

template <class T>
class Result {
public:
  static Result success(T value) {
    return Result(std::move(value));
  }

  static Result failure(Error error) {
    return Result(std::move(error));
  }

  bool has_value() const noexcept {
    return value_.has_value();
  }

  const T& value() const {
    if (!value_) {
      throw std::logic_error("ds::Result has no value");
    }
    return *value_;
  }

  T& value() {
    if (!value_) {
      throw std::logic_error("ds::Result has no value");
    }
    return *value_;
  }

  const Error& error() const {
    if (!error_) {
      throw std::logic_error("ds::Result has no error");
    }
    return *error_;
  }

private:
  explicit Result(T value) : value_(std::move(value)) {}
  explicit Result(Error error) : error_(std::move(error)) {}

  std::optional<T> value_;
  std::optional<Error> error_;
};

} // namespace ds
