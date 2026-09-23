#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <dualsynth/frame_services.hpp>

#include <cstring>
#include <limits>

// Shared contract tests driven through each host's actual FrameTraits adapter.
struct NumericPropertyProbe {
  char type = 'i';
  int count = 0;
  int error = 0;
  bool null_array = false;
  int int_arrays = 0, float_arrays = 0;
  std::vector<int> int_indices, float_indices;
  std::vector<std::int64_t> ints;
  std::vector<double> floats;

  void reset_calls() {
    int_arrays = float_arrays = 0;
    int_indices.clear();
    float_indices.clear();
  }
  std::int64_t get_int(int i, int* e) {
    int_indices.push_back(i);
    *e = i == count - 1 ? error : 0;
    return ints[i];
  }
  double get_float(int i, int* e) {
    float_indices.push_back(i);
    *e = i == count - 1 ? error : 0;
    return floats[i];
  }
  const std::int64_t* get_int_array(int* e) {
    ++int_arrays;
    *e = error;
    return null_array ? nullptr : ints.data();
  }
  const double* get_float_array(int* e) {
    ++float_arrays;
    *e = error;
    return null_array ? nullptr : floats.data();
  }
};

inline void check_numeric_properties(ds::FrameProperties& props, NumericPropertyProbe& probe,
                                     bool bulk_ints, bool bulk_floats) {
  for (int count : {0, 1, 32000}) {
    CAPTURE(count, bulk_ints, bulk_floats);
    probe.count = count;
    probe.ints.resize(count);
    probe.floats.resize(count);
    const std::int64_t ints[] = {INT64_MIN, INT64_MAX, 9007199254740993LL, -7, 0};
    const double floats[] = {0.0, -0.0, 0.125, -3.5,
      std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
    for (int i = 0; i < count; ++i) {
      probe.ints[i] = ints[i % 5];
      probe.floats[i] = floats[i % 6];
    }
    const auto expected_ints = probe.ints;
    const auto expected_floats = probe.floats;
    probe.reset_calls();
    probe.type = 'i';
    auto int_value = props.find("numbers");
    probe.type = 'f';
    auto float_value = props.find("numbers");
    REQUIRE(int_value);
    REQUIRE(float_value);
    REQUIRE(std::holds_alternative<std::vector<std::int64_t>>(*int_value));
    REQUIRE(std::holds_alternative<std::vector<double>>(*float_value));
    CHECK(probe.int_arrays == (bulk_ints && count ? 1 : 0));
    CHECK(probe.float_arrays == (bulk_floats && count ? 1 : 0));
    std::vector<int> indices(count);
    for (int i = 0; i < count; ++i) indices[i] = i;
    CHECK(probe.int_indices == (bulk_ints ? std::vector<int>{} : indices));
    CHECK(probe.float_indices == (bulk_floats ? std::vector<int>{} : indices));
    // Destroy the host's backing allocations before inspecting the snapshots.
    std::vector<std::int64_t>{}.swap(probe.ints);
    std::vector<double>{}.swap(probe.floats);
    CHECK(std::get<std::vector<std::int64_t>>(*int_value) == expected_ints);
    const auto& actual_floats = std::get<std::vector<double>>(*float_value);
    REQUIRE(actual_floats.size() == expected_floats.size());
    if (count) CHECK(std::memcmp(actual_floats.data(), expected_floats.data(), count * sizeof(double)) == 0);
  }

  probe.reset_calls();
  probe.count = -1;
  CHECK_FALSE(props.find("missing"));
  probe.count = 0;
  probe.type = 'c';
  CHECK_THROWS_AS(props.find("node"), std::invalid_argument);
  CHECK_THROWS_WITH(props.find("node"), "DualSynth: unsupported frame property type for 'node'");
  CHECK_THROWS_AS(props.find(""), std::invalid_argument);
  CHECK_THROWS_AS(props.find(std::string("a\0b", 3)), std::invalid_argument);
  CHECK(probe.int_arrays + probe.float_arrays == 0);
  CHECK(probe.int_indices.empty());
  CHECK(probe.float_indices.empty());

  probe.count = 3;
  probe.ints = {4, 2, 8};
  probe.floats = {4.5, 2.5, 8.5};
  for (char type : {'i', 'f'}) {
    probe.type = type;
    const bool bulk = type == 'i' ? bulk_ints : bulk_floats;
    for (int error : {1, 2, 4, 8}) {
      CAPTURE(type, error, bulk);
      probe.error = error;
      probe.reset_calls();
      CHECK_THROWS_AS(props.find("numbers"), std::runtime_error);
      CHECK_THROWS_WITH(props.find("numbers"),
        "DualSynth: host frame property operation failed (" + std::to_string(error) + ")");
      if (bulk) {
        CHECK(probe.int_indices.empty());
        CHECK(probe.float_indices.empty());
      }
    }
    probe.error = 0;
    if (bulk) {
      probe.null_array = true;
      CHECK_THROWS_WITH(props.find("numbers"), "DualSynth: invalid property array");
      probe.null_array = false;
    }
  }
}
