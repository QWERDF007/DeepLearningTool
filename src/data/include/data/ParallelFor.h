#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace dltool::data {

template<typename Function, typename ProgressFunction = std::nullptr_t>
void parallelFor(const std::size_t count, const int requested_threads, const std::atomic_bool &cancel_requested,
                 Function &&function, ProgressFunction &&progress = nullptr)
{
    if (count == 0 || cancel_requested.load(std::memory_order_relaxed))
        return;

    const std::size_t  worker_count = std::min(count, static_cast<std::size_t>(std::max(1, requested_threads)));
    std::atomic_size_t next_index{0};
    std::atomic_size_t completed_count{0};
    std::atomic_size_t reported_count{0};
    std::atomic_bool   stop_requested{false};
    std::exception_ptr first_exception;
    std::mutex         exception_mutex;

    auto worker = [&]()
    {
        try
        {
            while (!stop_requested.load(std::memory_order_relaxed) && !cancel_requested.load(std::memory_order_relaxed))
            {
                const std::size_t index = next_index.fetch_add(1, std::memory_order_relaxed);
                if (index >= count)
                    break;
                function(index);

                if constexpr (!std::is_same_v<std::decay_t<ProgressFunction>, std::nullptr_t>)
                {
                    const std::size_t completed = completed_count.fetch_add(1, std::memory_order_relaxed) + 1;
                    const std::size_t step       = std::max<std::size_t>(1, count / 10);
                    const std::size_t candidate  = completed == count ? count : (completed / step) * step;
                    if (candidate > 0)
                    {
                        std::size_t previous = reported_count.load(std::memory_order_relaxed);
                        while (candidate > previous
                               && !reported_count.compare_exchange_weak(previous, candidate,
                                                                         std::memory_order_relaxed))
                        {
                        }
                        if (candidate > previous)
                            progress(candidate, count);
                    }
                }
            }
        }
        catch (...)
        {
            stop_requested.store(true, std::memory_order_relaxed);
            std::lock_guard lock(exception_mutex);
            if (!first_exception)
                first_exception = std::current_exception();
        }
    };

    std::vector<std::jthread> workers;
    workers.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) workers.emplace_back(worker);

    for (auto &worker_thread : workers)
    {
        if (worker_thread.joinable())
            worker_thread.join();
    }

    if (first_exception)
        std::rethrow_exception(first_exception);
}

} // namespace dltool::data
