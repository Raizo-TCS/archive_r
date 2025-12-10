// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once
#include <string>

#include <chrono>
#include <iostream>
#include <map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <mutex>


namespace archive_r {
namespace internal {



class SimpleProfiler {
public:
    static SimpleProfiler& instance() {
        static SimpleProfiler inst;
        return inst;
    }

    ~SimpleProfiler() {
        report();
    }

    void start(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::high_resolution_clock::now();
        start_times_[name] = now;
    }

    void stop(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::high_resolution_clock::now();
        auto it = start_times_.find(name);
        if (it != start_times_.end()) {
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - it->second).count();
            durations_[name] += duration;
            counts_[name]++;
        }
    }

    void report() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (durations_.empty()) return;

        std::cout << "\n=== Profiling Report (archive_r::internal) ===" << std::endl;
        std::vector<std::pair<std::string, long long>> sorted_durations;
        for (const auto& pair : durations_) {
            sorted_durations.push_back(pair);
        }
        
        std::sort(sorted_durations.begin(), sorted_durations.end(), 
            [](const auto& a, const auto& b) { return a.second > b.second; });

        std::cout << std::left << std::setw(40) << "Name" 
                  << std::right << std::setw(15) << "Total (ms)" 
                  << std::setw(10) << "Count" 
                  << std::setw(15) << "Avg (us)" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        for (const auto& pair : sorted_durations) {
            const auto& name = pair.first;
            long long total_ns = pair.second;
            long long count = counts_[name];
            double avg_ns = count > 0 ? (double)total_ns / count : 0;
            
            std::cout << std::left << std::setw(40) << name 
                      << std::right << std::setw(15) << std::fixed << std::setprecision(3) << total_ns / 1000000.0 
                      << std::setw(10) << count 
                      << std::setw(15) << std::fixed << std::setprecision(3) << avg_ns / 1000.0 
                      << std::endl;
        }
        std::cout << "==============================================\n" << std::endl;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        start_times_.clear();
        durations_.clear();
        counts_.clear();
    }

private:
    std::map<std::string, std::chrono::high_resolution_clock::time_point> start_times_;
    std::map<std::string, long long> durations_;
    std::map<std::string, long long> counts_;
    std::mutex mutex_;
};

class ScopedTimer {
public:
    ScopedTimer(const std::string& name) : name_(name) {
        SimpleProfiler::instance().start(name_);
    }
    ~ScopedTimer() {
        SimpleProfiler::instance().stop(name_);
    }
private:
    std::string name_;
};

} // namespace internal
} // namespace archive_r

#ifdef ARCHIVE_R_SIMPLE_PROFILER_DISABLED
#define ARCHIVE_R_PROFILE(name) ((void)0)
#else
#define ARCHIVE_R_PROFILE(name) ::archive_r::internal::ScopedTimer ARCHIVE_R_PROFILE_UNIQUE_NAME(name)(name)
#endif

#define ARCHIVE_R_PROFILE_UNIQUE_NAME(name) ARCHIVE_R_PROFILE_CONCAT(_archive_r_profiler_scope_, __COUNTER__)
#define ARCHIVE_R_PROFILE_CONCAT(a, b) ARCHIVE_R_PROFILE_CONCAT_INNER(a, b)
#define ARCHIVE_R_PROFILE_CONCAT_INNER(a, b) a##b
