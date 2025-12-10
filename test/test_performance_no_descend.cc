#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/traverser.h"
#include "simple_profiler.h"

#include <archive.h>
#include <archive_entry.h>

#include <chrono>
#include <clocale>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace archive_r;

namespace {

struct BenchmarkResult {
    std::vector<double> durations;
    std::size_t entries = 0;
};

double compute_average(const std::vector<double> &values) {
    if (values.empty()) {
        return 0.0;
    }
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

std::string format_seconds(double seconds) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << seconds;
    return oss.str();
}

bool iterate_with_raw_libarchive(const std::string &path, std::size_t &entry_count) {
    struct ArchiveDeleter {
        void operator()(struct archive *ptr) const {
            if (ptr) {
                archive_read_free(ptr);
            }
        }
    };

    using ArchivePtr = std::unique_ptr<struct archive, ArchiveDeleter>;
    ArchivePtr handle(archive_read_new());

    if (!handle) {
        std::cerr << "Failed to allocate libarchive handle" << std::endl;
        return false;
    }

    archive_read_support_filter_all(handle.get());
    archive_read_support_format_all(handle.get());

    constexpr std::size_t kBlockSize = 10240;
    const int open_result = archive_read_open_filename(handle.get(), path.c_str(), kBlockSize);
    if (open_result != ARCHIVE_OK) {
        std::cerr << "Could not open archive with libarchive: "
                            << archive_error_string(handle.get()) << std::endl;
        return false;
    }

    entry_count = 0;
    while (true) {
        struct archive_entry *entry = nullptr;
        const int header_result = archive_read_next_header(handle.get(), &entry);

        if (header_result == ARCHIVE_EOF) {
            break;
        }

        // Access pathname to match archive_r behavior (fair comparison)
        volatile const char* p = archive_entry_pathname(entry);
        (void)p;

        if (header_result == ARCHIVE_WARN) {
            std::cerr << "libarchive warning: " << archive_error_string(handle.get()) << std::endl;
        } else if (header_result != ARCHIVE_OK) {
            std::cerr << "libarchive error while reading header: "
                                << archive_error_string(handle.get()) << std::endl;
            return false;
        }

        ++entry_count;

        const int skip_result = archive_read_data_skip(handle.get());
        if (skip_result == ARCHIVE_WARN) {
            std::cerr << "libarchive warning while skipping data: "
                                << archive_error_string(handle.get()) << std::endl;
        } else if (skip_result != ARCHIVE_OK) {
            std::cerr << "libarchive error while skipping data: "
                                << archive_error_string(handle.get()) << std::endl;
            return false;
        }
    }

    return true;
}

bool iterate_with_traverser(const std::string &path, std::size_t &entry_count) {
    try {
        Traverser traverser({ make_single_path(path) });
        entry_count = 0;

        for (Entry &entry : traverser) {
            ++entry_count;
            if (entry.depth() > 0) {
                entry.set_descent(false);
            }
        }

        return true;
    } catch (const std::exception &ex) {
        std::cerr << "archive_r Traverser error: " << ex.what() << std::endl;
        return false;
    }
}

std::optional<BenchmarkResult> run_benchmark(
        const std::string &label,
        int iterations,
        const std::function<bool(std::size_t &)> &operation) {

    BenchmarkResult result;
    result.durations.reserve(iterations);

    std::cout << "\n=== " << label << " ===" << std::endl;

    for (int i = 0; i < iterations; ++i) {
        std::size_t entries = 0;
        const auto start = std::chrono::high_resolution_clock::now();

        if (!operation(entries)) {
            std::cerr << "Benchmark aborted: " << label << " iteration " << (i + 1) << std::endl;
            return std::nullopt;
        }

        const auto end = std::chrono::high_resolution_clock::now();
        const double seconds = std::chrono::duration<double>(end - start).count();

        if (result.entries == 0) {
            result.entries = entries;
        } else if (result.entries != entries) {
            std::cerr << "Entry count mismatch in " << label << " (expected " << result.entries
                                << ", got " << entries << ")" << std::endl;
            return std::nullopt;
        }

        result.durations.push_back(seconds);

        std::cout << "  Run " << std::setw(2) << (i + 1) << ": " << format_seconds(seconds)
                            << " s (entries=" << entries << ")" << std::endl;
    }

    std::cout << "  Average: " << format_seconds(compute_average(result.durations)) << " s"
                        << std::endl;

    return result;
}

} // namespace

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    const std::string archive_path =
            (argc >= 2) ? argv[1] : "test_data/test_perf.zip";

    constexpr int kIterations = 10;

    std::cout << "=== Performance Comparison: " << archive_path << " ===" << std::endl;
    std::cout << "Iterations per method: " << kIterations << std::endl;

    const auto raw_result = run_benchmark(
            "libarchive (raw)", kIterations,
            [&](std::size_t &entries) { return iterate_with_raw_libarchive(archive_path, entries); });

    if (!raw_result) {
        return 1;
    }

    archive_r::internal::SimpleProfiler::instance().reset();

    const auto traverser_result = run_benchmark(
            "archive_r Traverser", kIterations,
            [&](std::size_t &entries) { return iterate_with_traverser(archive_path, entries); });

    archive_r::internal::SimpleProfiler::instance().report();

    if (!traverser_result) {
        return 1;
    }

    const double raw_avg = compute_average(raw_result->durations);
    const double traverser_avg = compute_average(traverser_result->durations);

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Entries parsed (per run): " << raw_result->entries << std::endl;
    std::cout << "  libarchive (raw) avg: "
                        << format_seconds(raw_avg) << " s" << std::endl;
    std::cout << "  archive_r Traverser avg: "
                        << format_seconds(traverser_avg) << " s" << std::endl;

    if (raw_avg > 0.0) {
        std::ostringstream ratio_stream;
        ratio_stream << std::fixed << std::setprecision(3) << (traverser_avg / raw_avg);
        std::cout << "  archive_r/libarchive ratio: " << ratio_stream.str() << "x" << std::endl;
    }

    return 0;
}
