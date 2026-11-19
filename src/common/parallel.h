#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace quakelib::detail {

  /// work(i) for i in [0, count) on `threads` threads, 0 uses all hardware threads
  template <typename Work> void ParallelFor(size_t count, int threads, Work work) {
    std::atomic<size_t> next{0};
    int n = threads > 0 ? threads : (int)std::max(1u, std::thread::hardware_concurrency());
    auto worker = [&]() {
      for (size_t i; (i = next.fetch_add(1)) < count;)
        work(i);
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < n; t++)
      pool.emplace_back(worker);
    worker();
    for (auto &t : pool)
      t.join();
  }

} // namespace quakelib::detail
