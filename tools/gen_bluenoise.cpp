// gen_bluenoise — regenerates lib/Epub/Epub/converters/BlueNoise64.h.
//
// Independent, clean-room implementation of Ulichney's void-and-cluster method
// ("The void-and-cluster method for dither array generation", SPIE 1993). It
// produces a 64x64 blue-noise threshold matrix used for fast single-pass 1-bit
// image dithering on the e-ink panel.
//
// The IDEA of using a blue-noise dither for fast BW image rendering comes from
// crosspoint-reader PR #2461 (derived from Jeremy Klein's PR #2179); no upstream
// code or table data was copied — only the published algorithm is implemented here.
//
// Build & run:
//   g++ -O2 -std=c++17 tools/gen_bluenoise.cpp -o /tmp/gen_bluenoise
//   /tmp/gen_bluenoise lib/Epub/Epub/converters/BlueNoise64.h
//
// Deterministic: fixed seed + sigma, so re-running reproduces the committed table.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <utility>
#include <vector>

static constexpr int N = 64;
static constexpr int AREA = N * N;
static constexpr float SIGMA = 1.9f;
static constexpr unsigned SEED = 1234567u;

// Toroidal Gaussian energy field; offsets packed as (dy+8)*256 + (dx+8).
static void splat(std::vector<float>& field, int idx, float sign, const std::vector<std::pair<int, float>>& kern) {
  int px = idx % N, py = idx / N;
  for (auto& kv : kern) {
    int ox = kv.first % 256 - 8, oy = kv.first / 256 - 8;
    int x = (px + ox + N) % N, y = (py + oy + N) % N;
    field[y * N + x] += sign * kv.second;
  }
}

int main(int argc, char** argv) {
  const char* outPath = argc > 1 ? argv[1] : "BlueNoise64.h";

  const int R = 8;
  std::vector<std::pair<int, float>> kern;
  for (int dy = -R; dy <= R; dy++)
    for (int dx = -R; dx <= R; dx++) {
      float w = std::exp(-(dx * dx + dy * dy) / (2.0f * SIGMA * SIGMA));
      if (w > 1e-4f) kern.push_back({(dy + 8) * 256 + (dx + 8), w});
    }

  std::vector<uint8_t> pat(AREA, 0);
  std::vector<float> field(AREA, 0.0f);
  std::mt19937 rng(SEED);
  const int ones = AREA / 10;
  {
    std::vector<int> idx(AREA);
    for (int i = 0; i < AREA; i++) idx[i] = i;
    std::shuffle(idx.begin(), idx.end(), rng);
    for (int i = 0; i < ones; i++) {
      pat[idx[i]] = 1;
      splat(field, idx[i], +1.0f, kern);
    }
  }

  auto tightestCluster = [&]() {
    int best = -1;
    float bv = -1e30f;
    for (int i = 0; i < AREA; i++)
      if (pat[i] && field[i] > bv) {
        bv = field[i];
        best = i;
      }
    return best;
  };
  auto largestVoid = [&]() {
    int best = -1;
    float bv = 1e30f;
    for (int i = 0; i < AREA; i++)
      if (!pat[i] && field[i] < bv) {
        bv = field[i];
        best = i;
      }
    return best;
  };

  // Prototype: relocate until the tightest cluster and largest void coincide.
  for (int it = 0; it < AREA * 4; it++) {
    int c = tightestCluster();
    pat[c] = 0;
    splat(field, c, -1.0f, kern);
    int v = largestVoid();
    pat[v] = 1;
    splat(field, v, +1.0f, kern);
    if (v == c) break;
  }
  std::vector<uint8_t> proto = pat;
  std::vector<float> protoField = field;
  std::vector<int> rank(AREA, 0);

  // Phase I: remove tightest clusters, ranking ones-1 .. 0.
  for (int r = ones - 1; r >= 0; r--) {
    int c = tightestCluster();
    pat[c] = 0;
    splat(field, c, -1.0f, kern);
    rank[c] = r;
  }
  // Phases II+III: reload prototype, insert into largest void, ranking ones .. AREA-1.
  pat = proto;
  field = protoField;
  for (int r = ones; r < AREA; r++) {
    int v = largestVoid();
    pat[v] = 1;
    splat(field, v, +1.0f, kern);
    rank[v] = r;
  }

  std::vector<uint8_t> thr(AREA);
  for (int i = 0; i < AREA; i++) thr[i] = (uint8_t)((rank[i] * 255) / (AREA - 1));

  FILE* f = fopen(outPath, "wb");
  if (!f) {
    perror("open");
    return 1;
  }
  fprintf(f,
          "#pragma once\n\n"
          "#include <cstdint>\n\n"
          "// 64x64 void-and-cluster blue-noise threshold matrix for ordered 1-bit dithering\n"
          "// of decoded images on the e-ink panel.\n"
          "//\n"
          "// IDEA / ATTRIBUTION: using a blue-noise dither for fast single-pass BW image\n"
          "// rendering on e-ink comes from crosspoint-reader PR #2461 (\"Fast\" grayscale\n"
          "// render mode), itself derived from Jeremy Klein's PR #2179. This is an\n"
          "// INDEPENDENT, CLEAN-ROOM implementation: the table below was generated from the\n"
          "// published void-and-cluster algorithm (Ulichney, \"The void-and-cluster method\n"
          "// for dither array generation\", SPIE 1993) by tools/gen_bluenoise (sigma=1.9,\n"
          "// fixed seed) -- no upstream code or table data was copied.\n"
          "//\n"
          "// Each entry is a threshold in [0,255]; a source gray sample is white when it is\n"
          "// strictly brighter than the tiled threshold at its (x,y). Tiling is keyed to\n"
          "// image-local coordinates so a cached image dithers identically wherever drawn.\n\n"
          "inline constexpr uint8_t kBlueNoise64[64][64] = {\n");
  for (int y = 0; y < N; y++) {
    fprintf(f, "  {");
    for (int x = 0; x < N; x++) fprintf(f, "%d%s", thr[y * N + x], x < N - 1 ? "," : "");
    fprintf(f, "},\n");
  }
  fprintf(f,
          "};\n\n"
          "// Returns 1 when the sample should be white, 0 when it should be ink (black),\n"
          "// matching Atkinson1BitDitherer::processPixel's convention so it is a drop-in.\n"
          "inline uint8_t blueNoise1Bit(int gray, int x, int y) {\n"
          "  if (gray < 0) gray = 0; else if (gray > 255) gray = 255;\n"
          "  return gray > kBlueNoise64[y & 63][x & 63] ? 1 : 0;\n"
          "}\n");
  fclose(f);
  printf("wrote %s\n", outPath);
  return 0;
}
