#pragma once

#include <condition_variable>
#include <queue>
#include <thread>
#include <vector>

namespace engine {
    namespace __ {
        template <typename>
        struct function_traits;

        template <typename C, typename R, typename Arg>
        struct function_traits<R (C::*)(Arg) const> {
            using arg_type = Arg;
        };

        template <typename C, typename R, typename Arg>
        struct function_traits<R (C::*)(Arg)> {
            using arg_type = Arg;
        };
    }

    template <typename Task, typename F>
    class ThreadPool {
      public:
        ThreadPool(F f, std::size_t thread_count = std::thread::hardware_concurrency()) : func(std::move(f)) {
            for (size_t i = 0; i < thread_count; i++) {
                workers.emplace_back([&] {
                    while (true) {
                        Task task;
                        {
                            std::unique_lock lock(mutex);
                            cv.wait(lock, [&] { return stop || !tasks.empty(); });
                            if (stop && tasks.empty()) return;
                            task = std::move(tasks.front());
                            tasks.pop();
                        }

                        func(std::move(task));
                    }
                });
            }
        }

        ~ThreadPool() {
            {
                std::lock_guard lock{mutex};
                stop = true;
            }
            cv.notify_all();
            for (auto& w : workers)
                w.join();
        }

        void enqueue(Task new_task) {
            {
                std::lock_guard lock{mutex};
                tasks.emplace(new_task);
            }
            cv.notify_one();
        }

      private:
        F func;
        std::vector<std::thread> workers;
        std::queue<Task> tasks;
        std::condition_variable cv;
        std::mutex mutex;
        bool stop = false;
    };

    template <typename Task, typename F>
    auto make_thread_pool(F&& f, std::size_t thread_count = std::thread::hardware_concurrency()) {
        using Fn = std::decay_t<F>;

        return ThreadPool<Task, Fn>(std::forward<F>(f), thread_count);
    }

    template <typename F>
    auto make_thread_pool(F&& f, std::size_t thread_count = std::thread::hardware_concurrency()) {
        using Fn = std::decay_t<F>;

        using Task = std::remove_cvref_t<typename __::function_traits<
            decltype(&Fn::operator())
        >::arg_type>;

        return ThreadPool<Task, Fn>(std::forward<F>(f), thread_count);
    }
}
