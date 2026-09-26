#include <dualsynth/video_bridge.hpp>

#include <cstdlib>
#include <iostream>
#include <new>
#include <sstream>

namespace {
thread_local int allocations = -1;
thread_local bool injected = false;
}

void* operator new(std::size_t size) {
  if (allocations == 0) {
    // Fail once so a stream that swallows the failure can still return its text.
    allocations = -1;
    injected = true;
    throw std::bad_alloc();
  }
  if (allocations > 0)
    --allocations;
  if (void* p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
  ds::FilterDescriptor descriptor{"AllocationTest", {}};
  std::string vs_expected, avs_expected, overloads;
  for (int i = 0; i < 40; ++i) {
    const auto name = "parameter_with_a_long_name_" + std::to_string(i);
    descriptor.params.push_back({name, ds::ParamType::Integer, {}, false, true});
    vs_expected += name + ":int[]:opt;";
    avs_expected += '[' + name + "]s";
    overloads += '[' + name + "()]i";
  }
  avs_expected += overloads;
  for (bool vs : {true, false}) {
    bool complete = false;
    for (int budget = 0; budget < 4096; ++budget) {
      injected = false;
      allocations = budget;
      bool correct = false;
      try {
        const auto result = vs ? ds::make_vapoursynth_signature(descriptor)
                               : ds::make_avisynth_signature(descriptor);
        allocations = -1;
        correct = result.has_value() && result.value() == (vs ? vs_expected : avs_expected);
      } catch (const std::bad_alloc&) {
        allocations = -1;
        correct = injected;
      } catch (const std::ios_base::failure&) {
        allocations = -1;
        correct = injected;
      }
      if (!correct) {
        std::cerr << (vs ? "VS" : "AVS") << " signature corrupted at allocation " << budget << '\n';
        return 1;
      }
      if (!injected) {
        complete = true;
        break;
      }
    }
    if (!complete) {
      std::cerr << "allocation sweep did not complete\n";
      return 1;
    }
  }
}
