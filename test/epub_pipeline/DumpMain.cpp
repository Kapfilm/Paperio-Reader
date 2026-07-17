// CLI companion to the EpubPipeline gtest: compile one EPUB through the real
// pipeline and print the canonical layout dump plus a BENCHMARK timing line
// (same "BENCHMARK <label> time=<us>us" format as EpubParserBenchmark).
//
// Usage: epub_pipeline_dump <book.epub> [cacheDir]
// A missing cacheDir uses a fresh temp dir (cold build). Passing the same
// cacheDir twice exercises the warm path.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>

#include "PipelineRunner.h"

int main(const int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <book.epub> [cacheDir]\n", argv[0]);
    return 2;
  }
  const std::string epubPath = argv[1];
  std::string cacheDir;
  if (argc > 2) {
    cacheDir = argv[2];
  } else {
    const auto dir = std::filesystem::temp_directory_path() / "epub_pipeline_dump";
    std::filesystem::remove_all(dir);
    cacheDir = dir.string();
  }
  std::filesystem::create_directories(cacheDir);

  const auto start = std::chrono::steady_clock::now();
  const bool ok = pipeline_harness::runAndDump(epubPath, cacheDir, pipeline_harness::Profile{}, std::cout);
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
  std::fprintf(stderr, "BENCHMARK pipeline_%s time=%lldus\n", ok ? "ok" : "FAILED", static_cast<long long>(us.count()));
  return ok ? 0 : 1;
}
