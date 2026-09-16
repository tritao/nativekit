#include "prepare/skribidi_adapter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/resource.h>
#endif
#if defined(_MSC_VER)
#include <malloc.h>
#endif

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

namespace {

std::atomic<uint64_t> allocation_count{0};

void *allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
    bytes = std::max<std::size_t>(bytes, 1);
    void *result = nullptr;
#if defined(_MSC_VER)
    if (alignment > alignof(std::max_align_t))
        result = _aligned_malloc(bytes, alignment);
    else
        result = std::malloc(bytes);
#else
    if (alignment > alignof(std::max_align_t)) {
        if (posix_memalign(&result, alignment, bytes) != 0)
            result = nullptr;
    } else {
        result = std::malloc(bytes);
    }
#endif
    if (!result)
        throw std::bad_alloc();
    ++allocation_count;
    return result;
}

void deallocate(void *pointer, std::size_t alignment = alignof(std::max_align_t)) noexcept {
    if (!pointer)
        return;
#if defined(_MSC_VER)
    if (alignment > alignof(std::max_align_t))
        _aligned_free(pointer);
    else
        std::free(pointer);
#else
    (void)alignment;
    std::free(pointer);
#endif
}

} // namespace

void *operator new(std::size_t bytes) { return allocate(bytes); }
void *operator new[](std::size_t bytes) { return allocate(bytes); }
void *operator new(std::size_t bytes, const std::nothrow_t &) noexcept {
    try {
        return allocate(bytes);
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](std::size_t bytes, const std::nothrow_t &) noexcept {
    try {
        return allocate(bytes);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void *pointer) noexcept { deallocate(pointer); }
void operator delete[](void *pointer) noexcept { deallocate(pointer); }
void operator delete(void *pointer, std::size_t bytes) noexcept {
    (void)bytes;
    deallocate(pointer);
}
void operator delete[](void *pointer, std::size_t bytes) noexcept {
    (void)bytes;
    deallocate(pointer);
}
void operator delete(void *pointer, const std::nothrow_t &) noexcept { deallocate(pointer); }
void operator delete[](void *pointer, const std::nothrow_t &) noexcept { deallocate(pointer); }

#if __cpp_aligned_new
void *operator new(std::size_t bytes, std::align_val_t alignment) {
    return allocate(bytes, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t bytes, std::align_val_t alignment) {
    return allocate(bytes, static_cast<std::size_t>(alignment));
}
void *operator new(std::size_t bytes, std::align_val_t alignment,
                   const std::nothrow_t &) noexcept {
    try {
        return allocate(bytes, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](std::size_t bytes, std::align_val_t alignment,
                     const std::nothrow_t &) noexcept {
    try {
        return allocate(bytes, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void *pointer, std::align_val_t alignment) noexcept {
    deallocate(pointer, static_cast<std::size_t>(alignment));
}
void operator delete[](void *pointer, std::align_val_t alignment) noexcept {
    deallocate(pointer, static_cast<std::size_t>(alignment));
}
void operator delete(void *pointer, std::size_t bytes, std::align_val_t alignment) noexcept {
    (void)bytes;
    deallocate(pointer, static_cast<std::size_t>(alignment));
}
void operator delete[](void *pointer, std::size_t bytes, std::align_val_t alignment) noexcept {
    (void)bytes;
    deallocate(pointer, static_cast<std::size_t>(alignment));
}
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct Samples {
    std::vector<double> values;

    explicit Samples(std::size_t reserve) { values.reserve(reserve); }

    void add(double value) { values.push_back(value); }

    double percentile(double fraction) const {
        if (values.empty())
            return 0.0;
        std::vector<double> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const auto index = static_cast<std::size_t>(
            std::min<double>(sorted.size() - 1, fraction * (sorted.size() - 1)));
        return sorted[index];
    }

    double minimum() const {
        return values.empty() ? 0.0 : *std::min_element(values.begin(), values.end());
    }
};

double elapsed_us(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

struct Mapping {
    std::vector<uint32_t> utf16_by_codepoint;
    std::size_t codepoint_count = 0;

    explicit Mapping(const std::string &text) {
        utf16_by_codepoint.reserve(text.size() + 1);
        utf16_by_codepoint.push_back(0);
        std::size_t byte_offset = 0;
        uint32_t utf16_offset = 0;
        while (byte_offset < text.size()) {
            const auto first = static_cast<uint8_t>(text[byte_offset]);
            std::size_t sequence_length = 1;
            if (first >= 0xc2 && first <= 0xdf)
                sequence_length = 2;
            else if (first >= 0xe0 && first <= 0xef)
                sequence_length = 3;
            else if (first >= 0xf0 && first <= 0xf4)
                sequence_length = 4;
            if (byte_offset + sequence_length > text.size())
                sequence_length = 1;
            byte_offset += sequence_length;
            utf16_offset += sequence_length == 4 ? 2 : 1;
            utf16_by_codepoint.push_back(utf16_offset);
        }
        codepoint_count = utf16_by_codepoint.size() - 1;
    }

    uint32_t utf16_offset(std::size_t codepoint) const {
        return utf16_by_codepoint[std::min(codepoint, codepoint_count)];
    }
};

std::string make_paragraph(std::size_t target_bytes, std::size_t paragraph_number) {
    const std::string seed =
        "NativeKit editor paragraph " + std::to_string(paragraph_number) +
        ": text shaping, IME composition, Unicode selection, and retained layout. "
        "日本語 مرحبا שלום 👨‍👩‍👧‍👦 ";
    std::string paragraph;
    const std::size_t body_limit = std::max<std::size_t>(1, target_bytes - 1);
    while (paragraph.size() + seed.size() <= body_limit)
        paragraph += seed;
    if (paragraph.empty())
        paragraph = seed.substr(0, std::min(body_limit, seed.size()));
    paragraph.push_back('\n');
    return paragraph;
}

std::string make_document(std::size_t target_bytes, std::size_t paragraph_bytes) {
    std::string result;
    result.reserve(target_bytes);
    std::size_t paragraph_number = 0;
    while (result.size() < target_bytes) {
        std::string paragraph = make_paragraph(paragraph_bytes, paragraph_number++);
        if (result.size() + paragraph.size() > target_bytes && !result.empty())
            break;
        result += paragraph;
    }
    if (result.empty())
        result = make_paragraph(target_bytes, 0);
    return result;
}

std::vector<std::string> split_paragraphs(const std::string &document) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < document.size()) {
        const std::size_t end = document.find('\n', start);
        const std::size_t actual_end = end == std::string::npos ? document.size() : end;
        result.push_back(document.substr(start, actual_end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (result.empty())
        result.emplace_back();
    return result;
}

std::size_t find_edit_offset(const std::string &text) {
    const std::size_t middle = text.size() / 2;
    for (std::size_t distance = 0; distance < text.size(); ++distance) {
        const std::size_t candidates[] = {middle + distance, middle >= distance ? middle - distance : 0};
        for (std::size_t candidate : candidates) {
            if (candidate < text.size() &&
                std::isalpha(static_cast<unsigned char>(text[candidate])))
                return candidate;
        }
    }
    return 0;
}

std::string edited_text(const std::string &text, std::size_t offset, std::size_t iteration) {
    std::string result = text;
    if (result.empty())
        return result;
    result[offset] = static_cast<char>('a' + iteration % 26);
    if (offset + 1 < result.size() &&
        std::isalpha(static_cast<unsigned char>(result[offset + 1])))
        result[offset + 1] = static_cast<char>('a' + (iteration / 26) % 26);
    return result;
}

nkui::TextLayoutOptions benchmark_options() {
    nkui::TextLayoutOptions options;
    options.font_size = 16.0f;
    options.wrap = nkui::TextWrapMode::WordCharacter;
    return options;
}

std::shared_ptr<nkui::SkribidiFontCollection> benchmark_fonts() {
    auto fonts = std::make_shared<nkui::SkribidiFontCollection>();
    if (!fonts->valid() || !fonts->add_font(NKUI_TEST_FONT_PATH))
        return nullptr;
    return fonts;
}

struct WorkloadReport {
    std::size_t target_bytes = 0;
    std::size_t document_bytes = 0;
    std::size_t paragraph_count = 0;
    std::size_t codepoint_count = 0;
    Samples full_edit_us;
    Samples paragraph_edit_us;
    Samples caret_us;
    Samples selection_us;
    Samples hit_test_us;
    Samples paint_us;
    Samples utf16_build_us;
    Samples utf16_query_us;
    Samples full_edit_tracked_allocations;
    Samples paragraph_edit_tracked_allocations;
    uint64_t layout_builds = 0;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
    uint64_t peak_rss_kb = 0;

    explicit WorkloadReport(std::size_t iterations)
        : full_edit_us(iterations), paragraph_edit_us(iterations), caret_us(iterations),
          selection_us(iterations), hit_test_us(iterations), paint_us(iterations),
          utf16_build_us(iterations), utf16_query_us(iterations),
          full_edit_tracked_allocations(iterations), paragraph_edit_tracked_allocations(iterations) {}
};

uint64_t peak_rss_kb() {
#if defined(__linux__)
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return static_cast<uint64_t>(usage.ru_maxrss);
#endif
    return 0;
}

bool run_workload(std::size_t target_bytes, std::size_t iterations, WorkloadReport &report) {
    const std::string document = make_document(target_bytes, std::min<std::size_t>(target_bytes, 4096));
    const auto paragraphs = split_paragraphs(document);
    const auto options = benchmark_options();
    constexpr float width = 640.0f;
    report.document_bytes = document.size();
    report.paragraph_count = paragraphs.size();

    auto fonts = benchmark_fonts();
    if (!fonts)
        return false;

    nkui::SkribidiAdapter full_adapter(fonts);
    nkui::TextLayoutResult full_result;
    if (!full_adapter.layout_utf8(document.c_str(), width, options, &full_result))
        return false;
    Mapping document_mapping(document);
    report.codepoint_count = document_mapping.codepoint_count;
    const std::size_t full_edit_offset = find_edit_offset(document);
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const std::string next = edited_text(document, full_edit_offset, iteration);
        const uint64_t before_allocations = allocation_count.load();
        const auto start = Clock::now();
        nkui::TextLayoutResult edited_result;
        const bool laid_out = full_adapter.layout_utf8(next.c_str(), width, options, &edited_result);
        const auto end = Clock::now();
        if (!laid_out || !edited_result.id)
            return false;
        report.full_edit_us.add(elapsed_us(start, end));
        report.full_edit_tracked_allocations.add(
            static_cast<double>(allocation_count.load() - before_allocations));
        full_adapter.prune_layout_cache({edited_result.id}, 1);
        full_result = edited_result;
    }

    nkui::SkribidiAdapter paragraph_adapter(fonts);
    std::vector<nkui::TextLayoutId> paragraph_ids;
    paragraph_ids.reserve(paragraphs.size());
    for (const auto &paragraph : paragraphs) {
        nkui::TextLayoutResult paragraph_result;
        if (!paragraph_adapter.layout_utf8(paragraph.c_str(), width, options, &paragraph_result) ||
            !paragraph_result.id)
            return false;
        paragraph_ids.push_back(paragraph_result.id);
    }
    const std::size_t target_paragraph = paragraph_ids.size() / 2;
    const std::size_t paragraph_edit_offset = find_edit_offset(paragraphs[target_paragraph]);
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const std::string next = edited_text(paragraphs[target_paragraph], paragraph_edit_offset,
                                             iteration);
        const uint64_t before_allocations = allocation_count.load();
        const auto start = Clock::now();
        nkui::TextLayoutResult edited_result;
        const bool laid_out = paragraph_adapter.layout_utf8(next.c_str(), width, options,
                                                            &edited_result);
        const auto end = Clock::now();
        if (!laid_out || !edited_result.id)
            return false;
        report.paragraph_edit_us.add(elapsed_us(start, end));
        report.paragraph_edit_tracked_allocations.add(
            static_cast<double>(allocation_count.load() - before_allocations));
        paragraph_ids[target_paragraph] = edited_result.id;
        for (std::size_t paragraph_index = 0; paragraph_index < paragraph_ids.size();
             ++paragraph_index) {
            if (paragraph_index == target_paragraph)
                continue;
            nkui::TextLayoutResult retained_result;
            if (!paragraph_adapter.layout_utf8(paragraphs[paragraph_index].c_str(), width, options,
                                               &retained_result) ||
                retained_result.id != paragraph_ids[paragraph_index])
                return false;
        }
        paragraph_adapter.prune_layout_cache(paragraph_ids, paragraph_ids.size());
        for (nkui::TextLayoutId id : paragraph_ids)
            if (!paragraph_adapter.has_layout(id))
                return false;
    }

    const std::size_t query_count = std::max<std::size_t>(iterations, 8);
    for (std::size_t index = 0; index < query_count; ++index) {
        const int32_t offset = static_cast<int32_t>(
            std::min<std::size_t>(document_mapping.codepoint_count,
                                  document_mapping.codepoint_count * index / query_count));
        const nkui::TextPosition position{offset, 0};
        const auto caret_start = Clock::now();
        const auto caret = full_adapter.caret(position);
        const auto caret_end = Clock::now();
        report.caret_us.add(elapsed_us(caret_start, caret_end));

        const int32_t selection_end = static_cast<int32_t>(
            std::min<std::size_t>(document_mapping.codepoint_count, static_cast<std::size_t>(offset) + 8));
        const auto selection_start = Clock::now();
        const auto rectangles = full_adapter.selection_rects(position, {selection_end, 0});
        const auto selection_end_time = Clock::now();
        report.selection_us.add(elapsed_us(selection_start, selection_end_time));

        const auto hit_start = Clock::now();
        const auto hit = full_adapter.hit_test(caret.x, caret.y);
        const auto hit_end = Clock::now();
        report.hit_test_us.add(elapsed_us(hit_start, hit_end));

        const uint32_t line_count = static_cast<uint32_t>(full_result.lines.size());
        if (line_count > 0) {
            const uint32_t line = static_cast<uint32_t>(index % line_count);
            nkui::PreparedGlyphs glyphs;
            const auto paint_start = Clock::now();
            if (!full_adapter.prepare_glyphs_for_line(full_result.id, line, 0.0f, 0.0f, 1.0f,
                                                      nkui::GlyphMode::Alpha, glyphs))
                return false;
            const auto paint_end = Clock::now();
            report.paint_us.add(elapsed_us(paint_start, paint_end));
            if (glyphs.vertices.empty() && !rectangles.empty())
                return false;
        }
        if (hit.offset < 0)
            return false;
    }

    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const std::string next = edited_text(document, full_edit_offset, iteration);
        const auto build_start = Clock::now();
        Mapping mapping(next);
        const auto build_end = Clock::now();
        report.utf16_build_us.add(elapsed_us(build_start, build_end));
        const auto query_start = Clock::now();
        volatile uint32_t mapped = mapping.utf16_offset(
            mapping.codepoint_count * (iteration + 1) / (iterations + 1));
        (void)mapped;
        const auto query_end = Clock::now();
        report.utf16_query_us.add(elapsed_us(query_start, query_end));
    }

    const auto full_stats = full_adapter.stats();
    const auto paragraph_stats = paragraph_adapter.stats();
    report.layout_builds = static_cast<uint64_t>(full_adapter.layout_build_count()) +
                           paragraph_adapter.layout_build_count();
    report.cache_hits = full_stats.text_layout_cache_hits + paragraph_stats.text_layout_cache_hits;
    report.cache_misses = full_stats.text_layout_cache_misses + paragraph_stats.text_layout_cache_misses;
    report.peak_rss_kb = peak_rss_kb();
    return true;
}

void print_summary(const Samples &samples) {
    std::cout << "{\"min\":" << std::fixed << std::setprecision(2) << samples.minimum()
              << ",\"p50\":" << samples.percentile(0.50)
              << ",\"p95\":" << samples.percentile(0.95) << '}';
}

void print_report(const WorkloadReport &report) {
    std::cout << "{\"target_bytes\":" << report.target_bytes
              << ",\"document_bytes\":" << report.document_bytes
              << ",\"paragraph_count\":" << report.paragraph_count
              << ",\"codepoint_count\":" << report.codepoint_count
              << ",\"full_document_edit_us\":";
    print_summary(report.full_edit_us);
    std::cout << ",\"dirty_paragraph_edit_us\":";
    print_summary(report.paragraph_edit_us);
    std::cout << ",\"caret_query_us\":";
    print_summary(report.caret_us);
    std::cout << ",\"selection_geometry_us\":";
    print_summary(report.selection_us);
    std::cout << ",\"hit_test_us\":";
    print_summary(report.hit_test_us);
    std::cout << ",\"paint_line_us\":";
    print_summary(report.paint_us);
    std::cout << ",\"utf16_map_build_us\":";
    print_summary(report.utf16_build_us);
    std::cout << ",\"utf16_map_query_us\":";
    print_summary(report.utf16_query_us);
    std::cout << ",\"full_document_edit_tracked_allocations\":";
    print_summary(report.full_edit_tracked_allocations);
    std::cout << ",\"dirty_paragraph_edit_tracked_allocations\":";
    print_summary(report.paragraph_edit_tracked_allocations);
    std::cout << ",\"layout_builds\":" << report.layout_builds
              << ",\"cache_hits\":" << report.cache_hits
              << ",\"cache_misses\":" << report.cache_misses
              << ",\"peak_rss_kb\":" << report.peak_rss_kb << "}\n";
}

} // namespace

int main(int argc, char **argv) {
    std::size_t iterations = 5;
    if (argc == 3 && std::string(argv[1]) == "--iterations") {
        try {
            iterations = std::max<std::size_t>(1, std::stoull(argv[2]));
        } catch (...) {
            std::cerr << "usage: nativekit_ui_text_editor_benchmark [--iterations N]\n";
            return 2;
        }
    } else if (argc != 1) {
        std::cerr << "usage: nativekit_ui_text_editor_benchmark [--iterations N]\n";
        return 2;
    }

    constexpr std::size_t workloads[] = {1024, 100 * 1024, 1024 * 1024, 10 * 1024 * 1024};
    std::cout << "{\"benchmark\":\"nativekit_text_editor\",\"iterations\":"
              << iterations << "}\n";
    for (std::size_t target_bytes : workloads) {
        WorkloadReport report(iterations);
        report.target_bytes = target_bytes;
        if (!run_workload(target_bytes, iterations, report)) {
            std::cerr << "text editor benchmark failed for " << target_bytes << " bytes\n";
            return 1;
        }
        print_report(report);
    }
    return 0;
}
