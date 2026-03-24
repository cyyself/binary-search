#include <bit>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class ParseResult {
    Success,
    Help,
    Error,
};

enum class QueryMode {
    RandomMiss,
    FixedHighMiss,
    SequentialMiss,
};

enum class SearchMode {
    Branchy,
    ConditionalMoveCpp,
    ConditionalMoveAsm,
};

struct Config {
    std::size_t size = 1'000'000;
    std::size_t queries = 20'000'000;
    std::uint64_t seed = 0xC0FFEEULL;
    QueryMode mode = QueryMode::RandomMiss;
    SearchMode search = SearchMode::Branchy;
};

[[nodiscard]] std::string mode_to_string(QueryMode mode) {
    switch (mode) {
    case QueryMode::RandomMiss:
        return "random-miss";
    case QueryMode::FixedHighMiss:
        return "fixed-high-miss";
    case QueryMode::SequentialMiss:
        return "sequential-miss";
    }

    return "unknown";
}

[[nodiscard]] std::string search_to_string(SearchMode search_mode) {
    switch (search_mode) {
    case SearchMode::Branchy:
        return "branchy";
    case SearchMode::ConditionalMoveCpp:
        return "cmov-cpp";
    case SearchMode::ConditionalMoveAsm:
        return "cmov-asm";
    }

    return "unknown";
}

[[nodiscard]] bool parse_unsigned(std::string_view text, std::size_t &value) {
    if (text.empty()) {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text.data(), &end, 10);
    if (errno != 0 || end != text.data() + text.size() || parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    value = static_cast<std::size_t>(parsed);
    return true;
}

[[nodiscard]] bool parse_seed(std::string_view text, std::uint64_t &value) {
    if (text.empty()) {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text.data(), &end, 10);
    if (errno != 0 || end != text.data() + text.size()) {
        return false;
    }

    value = static_cast<std::uint64_t>(parsed);
    return true;
}

[[nodiscard]] bool parse_mode(std::string_view text, QueryMode &mode) {
    if (text == "random-miss") {
        mode = QueryMode::RandomMiss;
        return true;
    }

    if (text == "fixed-high-miss") {
        mode = QueryMode::FixedHighMiss;
        return true;
    }

    if (text == "sequential-miss") {
        mode = QueryMode::SequentialMiss;
        return true;
    }

    return false;
}

[[nodiscard]] bool parse_search(std::string_view text, SearchMode &search_mode) {
    if (text == "branchy") {
        search_mode = SearchMode::Branchy;
        return true;
    }

    if (text == "cmov" || text == "cmov-cpp") {
        search_mode = SearchMode::ConditionalMoveCpp;
        return true;
    }

    if (text == "cmov-asm") {
        search_mode = SearchMode::ConditionalMoveAsm;
        return true;
    }

    return false;
}

void print_usage(const char *program) {
    std::cout
        << "Usage: " << program << " [--size N] [--queries Q] [--mode MODE] [--search KIND] [--seed S]\n"
        << "\n"
        << "Modes:\n"
        << "  random-miss      Random odd values across the array domain (default)\n"
        << "  fixed-high-miss  Repeatedly probe one value above the largest element\n"
        << "  sequential-miss  Walk odd values in ascending order, wrapping at the end\n"
        << "\n"
        << "Search kinds:\n"
        << "  branchy          Original binary search with an unpredictable branch\n"
        << "  cmov             Lower-bound style rewrite that GCC can compile to cmov\n"
        << "  cmov-asm         Binary search with explicit x86 conditional moves\n";
}

[[nodiscard]] ParseResult parse_arguments(int argc, char **argv, Config &config) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);

        if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            return ParseResult::Help;
        }

        if (argument == "--size" && index + 1 < argc) {
            if (!parse_unsigned(argv[++index], config.size)) {
                std::cerr << "Invalid value for --size\n";
                return ParseResult::Error;
            }
            continue;
        }

        if (argument == "--queries" && index + 1 < argc) {
            if (!parse_unsigned(argv[++index], config.queries)) {
                std::cerr << "Invalid value for --queries\n";
                return ParseResult::Error;
            }
            continue;
        }

        if (argument == "--seed" && index + 1 < argc) {
            if (!parse_seed(argv[++index], config.seed)) {
                std::cerr << "Invalid value for --seed\n";
                return ParseResult::Error;
            }
            continue;
        }

        if (argument == "--mode" && index + 1 < argc) {
            if (!parse_mode(argv[++index], config.mode)) {
                std::cerr << "Invalid value for --mode\n";
                return ParseResult::Error;
            }
            continue;
        }

        if (argument == "--search" && index + 1 < argc) {
            if (!parse_search(argv[++index], config.search)) {
                std::cerr << "Invalid value for --search\n";
                return ParseResult::Error;
            }
            continue;
        }

        std::cerr << "Unknown argument: " << argument << "\n";
        return ParseResult::Error;
    }

    if (config.size == 0 || config.queries == 0) {
        std::cerr << "--size and --queries must both be greater than zero\n";
        return ParseResult::Error;
    }

    constexpr std::size_t max_size = (static_cast<std::size_t>(std::numeric_limits<int>::max()) - 1U) / 2U;
    if (config.size > max_size) {
        std::cerr << "--size is too large for 32-bit integer test data\n";
        return ParseResult::Error;
    }

    return ParseResult::Success;
}

[[nodiscard]] std::vector<int> build_sorted_array(std::size_t size) {
    std::vector<int> values(size);
    for (std::size_t index = 0; index < size; ++index) {
        values[index] = static_cast<int>(index * 2U);
    }
    return values;
}

[[nodiscard]] std::vector<int> build_queries(const Config &config) {
    std::vector<int> queries(config.queries);

    switch (config.mode) {
    case QueryMode::RandomMiss: {
        std::mt19937_64 random_engine(config.seed);
        std::uniform_int_distribution<std::size_t> distribution(0, config.size - 1U);
        for (std::size_t index = 0; index < config.queries; ++index) {
            queries[index] = static_cast<int>(distribution(random_engine) * 2U + 1U);
        }
        break;
    }
    case QueryMode::FixedHighMiss: {
        const int target = static_cast<int>(config.size * 2U + 1U);
        std::fill(queries.begin(), queries.end(), target);
        break;
    }
    case QueryMode::SequentialMiss: {
        for (std::size_t index = 0; index < config.queries; ++index) {
            queries[index] = static_cast<int>((index % config.size) * 2U + 1U);
        }
        break;
    }
    }

    return queries;
}

[[gnu::noinline]] bool binary_search_branchy(const int *data, std::size_t size, int target) {
    std::size_t left = 0;
    std::size_t right = size;

    while (left < right) {
        const std::size_t mid = left + ((right - left) >> 1U);
        if (data[mid] < target) {
            left = mid + 1U;
        } else {
            right = mid;
        }
    }

    return left < size && data[left] == target;
}

[[gnu::noinline]] bool binary_search_cmov_cpp(const int *data, std::size_t size, int target) {
    if (size == 0) {
        return false;
    }

    const int *begin = data;
    const int *end = data + size;
    std::size_t length = size;
    std::size_t step = std::bit_floor(length);

    if (step != length && begin[step] < target) {
        length -= step + 1U;
        if (length == 0) {
            return false;
        }

        step = std::bit_ceil(length);
        begin = end - step;
    }

    for (step >>= 1U; step != 0; step >>= 1U) {
        if (begin[step] < target) {
            begin += step;
        }
    }

    begin += (*begin < target);
    return begin != end && *begin == target;
}

[[gnu::noinline]] bool binary_search_cmov_asm(const int *data, std::size_t size, int target) {
    std::size_t left = 0;
    std::size_t right = size;

    while (left < right) {
        const std::size_t mid = left + ((right - left) >> 1U);
        const std::size_t next_left = mid + 1U;
        const int value = data[mid];

#if defined(__x86_64__) || defined(__i386__)
        std::size_t new_left = left;
        std::size_t new_right = right;

        asm volatile(
            "cmp %[target], %[value]\n\t"
            "cmovl %[next_left], %[new_left]\n\t"
            "cmovge %[mid], %[new_right]"
            : [new_left] "+r"(new_left), [new_right] "+r"(new_right)
            : [value] "r"(value),
              [target] "r"(target),
              [next_left] "r"(next_left),
              [mid] "r"(mid)
            : "cc");

        left = new_left;
        right = new_right;
#else
        const bool move_right = value < target;
        left = move_right ? next_left : left;
        right = move_right ? right : mid;
#endif
    }

    return left < size && data[left] == target;
}

struct Result {
    std::uint64_t hits = 0;
    double elapsed_seconds = 0.0;
};

[[nodiscard]] Result run_benchmark(
    const std::vector<int> &values,
    const std::vector<int> &queries,
    SearchMode search_mode) {
    std::uint64_t hits = 0;

    const auto start = std::chrono::steady_clock::now();
    switch (search_mode) {
    case SearchMode::Branchy:
        for (const int query : queries) {
            hits += static_cast<std::uint64_t>(binary_search_branchy(values.data(), values.size(), query));
        }
        break;
    case SearchMode::ConditionalMoveCpp:
        for (const int query : queries) {
            hits += static_cast<std::uint64_t>(binary_search_cmov_cpp(values.data(), values.size(), query));
        }
        break;
    case SearchMode::ConditionalMoveAsm:
        for (const int query : queries) {
            hits += static_cast<std::uint64_t>(binary_search_cmov_asm(values.data(), values.size(), query));
        }
        break;
    }
    const auto end = std::chrono::steady_clock::now();

    const std::chrono::duration<double> elapsed = end - start;
    return Result{hits, elapsed.count()};
}

} // namespace

int main(int argc, char **argv) {
    Config config;
    switch (parse_arguments(argc, argv, config)) {
    case ParseResult::Success:
        break;
    case ParseResult::Help:
        return 0;
    case ParseResult::Error:
        return 1;
    }

    const std::vector<int> values = build_sorted_array(config.size);
    const std::vector<int> queries = build_queries(config);
    const Result result = run_benchmark(values, queries, config.search);

    const double queries_per_second = static_cast<double>(config.queries) / result.elapsed_seconds;
    const double nanoseconds_per_query = (result.elapsed_seconds * 1'000'000'000.0) / static_cast<double>(config.queries);

    std::cout << "mode=" << mode_to_string(config.mode) << '\n';
    std::cout << "search=" << search_to_string(config.search) << '\n';
    std::cout << "elements=" << config.size << '\n';
    std::cout << "queries=" << config.queries << '\n';
    std::cout << "hits=" << result.hits << '\n';
    std::cout << "elapsed_seconds=" << result.elapsed_seconds << '\n';
    std::cout << "queries_per_second=" << queries_per_second << '\n';
    std::cout << "nanoseconds_per_query=" << nanoseconds_per_query << '\n';
    return 0;
}