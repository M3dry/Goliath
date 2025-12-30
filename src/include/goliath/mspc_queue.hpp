#pragma once

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <emmintrin.h>
#include <vector>

namespace engine {
    template <typename Task, size_t N>
    class MSPCQueue {
        struct alignas(64) ReverseIx {
            std::atomic<uint32_t> reserve{0};
        };

        struct alignas(64) CommitIx {
            std::atomic<uint32_t> commit{0};
        };

        struct alignas(64) ConsumeIx {
            std::atomic<uint32_t> read{0};
        };

        ReverseIx pub{};
        CommitIx com{};
        ConsumeIx con{};
        std::array<Task, N> buffer{};

      public:
        void enqueue(Task task) {
            uint32_t slot = pub.reserve.fetch_add(1, std::memory_order_relaxed);

            while (slot - con.read.load(std::memory_order_acquire) >= N) {
                _mm_pause();
            }

            buffer[slot % N] = task;

            while (com.commit.load(std::memory_order_relaxed) != slot) {
                _mm_pause();
            }

            com.commit.store(slot + 1, std::memory_order_release);
        }

        void drain(std::vector<Task>& tasks) {
            uint32_t start = con.read.load(std::memory_order_acquire);
            uint32_t end = com.commit.load(std::memory_order_acquire);

            uint32_t count = end - start;
            if (count == 0) return;

            tasks.resize(count);

            uint32_t first = start % N;
            uint32_t n1 = std::min<uint32_t>(count, N - first);

            std::memcpy(tasks.data(), buffer.data() + first, n1 * sizeof(Task));
            std::memcpy(tasks.data() + n1, buffer.data(), (count - n1) * sizeof(Task));

            con.read.store(end, std::memory_order_release);
        }
    };
}
