#include "ProgressiveJpegDc.h"

#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

constexpr size_t INPUT_BUFFER_SIZE = 512;
constexpr uint8_t MAX_COMPONENTS = 4;
constexpr uint8_t MAX_TABLES = 4;

bool isStartOfFrame(uint8_t marker) {
  return (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
         (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
}

class BufferedInput {
 public:
  explicit BufferedInput(FsFile& file) : file_(file) {}

  int readByte() {
    if (position_ == available_) {
      const int count = file_.read(buffer_, sizeof(buffer_));
      if (count <= 0) {
        ioError_ = count < 0;
        return -1;
      }
      position_ = 0;
      available_ = static_cast<size_t>(count);
    }
    return buffer_[position_++];
  }

  bool read(void* destination, size_t size) {
    auto* output = static_cast<uint8_t*>(destination);
    for (size_t index = 0; index < size; ++index) {
      const int value = readByte();
      if (value < 0) return false;
      output[index] = static_cast<uint8_t>(value);
    }
    return true;
  }

  bool discard(uint32_t size) {
    const size_t buffered = available_ - position_;
    const size_t consume = std::min<size_t>(size, buffered);
    position_ += consume;
    size -= consume;
    if (size == 0) return true;

    position_ = 0;
    available_ = 0;
    if (file_.seek(file_.position() + size)) return true;
    ioError_ = true;
    return false;
  }

  bool ioError() const { return ioError_; }

 private:
  FsFile& file_;
  uint8_t buffer_[INPUT_BUFFER_SIZE] = {};
  size_t position_ = 0;
  size_t available_ = 0;
  bool ioError_ = false;
};

struct HuffmanTable {
  uint8_t lengthCount[17] = {};
  uint16_t firstCode[17] = {};
  uint16_t firstSymbol[17] = {};
  uint8_t symbols[256] = {};
  bool valid = false;

  bool build(const uint8_t counts[16], const uint8_t* values, uint16_t valueCount) {
    uint32_t code = 0;
    uint16_t symbolOffset = 0;
    for (uint8_t length = 1; length <= 16; ++length) {
      const uint8_t count = counts[length - 1];
      if (code + count > (1UL << length)) return false;
      lengthCount[length] = count;
      firstCode[length] = static_cast<uint16_t>(code);
      firstSymbol[length] = symbolOffset;
      code = (code + count) << 1;
      symbolOffset = static_cast<uint16_t>(symbolOffset + count);
    }
    if (symbolOffset != valueCount) return false;
    memcpy(symbols, values, valueCount);
    valid = true;
    return true;
  }
};

struct FrameComponent {
  uint8_t identifier = 0;
  uint8_t horizontalFactor = 0;
  uint8_t verticalFactor = 0;
  uint8_t quantizer = 0;
  uint8_t dcTable = 0;
  uint8_t acTable = 0;
};

struct FrameDefinition {
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t restartInterval = 0;
  uint16_t dcQuantizer[MAX_TABLES] = {1, 1, 1, 1};
  FrameComponent components[MAX_COMPONENTS] = {};
  uint8_t componentCount = 0;
  uint8_t scanOrder[MAX_COMPONENTS] = {};
  uint8_t scanCount = 0;
  uint8_t successiveLow = 0;
  HuffmanTable dcTables[MAX_TABLES];
  HuffmanTable acTables[MAX_TABLES];
  uint16_t quantizers[MAX_TABLES][64] = {};
  uint8_t spectralStart = 0, spectralEnd = 0, successiveHigh = 0;
};

struct DecodeState {
  explicit DecodeState(FsFile& file) : input(file) {}

  BufferedInput input;
  FrameDefinition frame;
  uint32_t entropyByte = 0;
  uint8_t entropyBits = 0;
  int pendingMarker = 0;

  int nextEntropyBit() {
    if (entropyBits == 0) {
      int value = input.readByte();
      if (value < 0) return -1;
      if (value == 0xFF) {
        int marker = input.readByte();
        while (marker == 0xFF) marker = input.readByte();
        if (marker < 0) return -1;
        if (marker != 0x00) {
          pendingMarker = marker;
          return -1;
        }
        value = 0xFF;
      }
      entropyByte = static_cast<uint32_t>(value);
      entropyBits = 8;
    }
    --entropyBits;
    return static_cast<int>((entropyByte >> entropyBits) & 1U);
  }

  int decodeSymbol(const HuffmanTable& table) {
    uint32_t code = 0;
    for (uint8_t length = 1; length <= 16; ++length) {
      const int bit = nextEntropyBit();
      if (bit < 0) return -1;
      code = (code << 1) | static_cast<uint32_t>(bit);
      const uint32_t relative = code - table.firstCode[length];
      if (code >= table.firstCode[length] && relative < table.lengthCount[length]) {
        return table.symbols[table.firstSymbol[length] + relative];
      }
    }
    return -1;
  }

  bool receiveSigned(uint8_t bitCount, int32_t& value) {
    value = 0;
    for (uint8_t index = 0; index < bitCount; ++index) {
      const int bit = nextEntropyBit();
      if (bit < 0) return false;
      value = (value << 1) | bit;
    }
    if (bitCount != 0 && value < (1L << (bitCount - 1))) value -= (1L << bitCount) - 1;
    return true;
  }

  bool consumeRestartMarker() {
    entropyBits = 0;
    int marker = pendingMarker;
    pendingMarker = 0;
    if (marker == 0) {
      int prefix = input.readByte();
      if (prefix != 0xFF) return false;
      do {
        marker = input.readByte();
      } while (marker == 0xFF);
    }
    return marker >= 0xD0 && marker <= 0xD7;
  }
};

ProgressiveJpegDc::Result inputFailure(const BufferedInput& input) {
  return input.ioError() ? ProgressiveJpegDc::Result::IoError : ProgressiveJpegDc::Result::InvalidData;
}

bool readSegmentSize(BufferedInput& input, uint32_t& payloadSize) {
  uint8_t bytes[2] = {};
  if (!input.read(bytes, sizeof(bytes))) return false;
  const uint16_t segmentSize = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
  if (segmentSize < 2) return false;
  payloadSize = segmentSize - 2;
  return true;
}

int nextMarker(BufferedInput& input) {
  int value = input.readByte();
  while (value >= 0 && value != 0xFF) value = input.readByte();
  if (value < 0) return -1;
  do {
    value = input.readByte();
  } while (value == 0xFF);
  return value;
}

ProgressiveJpegDc::Result parseQuantizers(DecodeState& state, uint32_t payloadSize) {
  while (payloadSize > 0) {
    const int descriptor = state.input.readByte();
    if (descriptor < 0) return inputFailure(state.input);
    const uint8_t precision = static_cast<uint8_t>(descriptor) >> 4;
    const uint8_t tableIndex = static_cast<uint8_t>(descriptor) & 0x0F;
    const uint32_t tableBytes = precision == 0 ? 64 : 128;
    if (precision > 1 || tableIndex >= MAX_TABLES || payloadSize < tableBytes + 1) {
      return ProgressiveJpegDc::Result::InvalidData;
    }
    for (unsigned k = 0; k < 64; ++k) {
      uint8_t value[2] = {};
      if (!state.input.read(value, precision == 0 ? 1 : 2)) return inputFailure(state.input);
      state.frame.quantizers[tableIndex][k] = precision == 0 ? value[0] : (value[0] << 8) | value[1];
      if (state.frame.quantizers[tableIndex][k] == 0) return ProgressiveJpegDc::Result::InvalidData;
    }
    state.frame.dcQuantizer[tableIndex] = state.frame.quantizers[tableIndex][0];
    payloadSize -= tableBytes + 1;
  }
  return ProgressiveJpegDc::Result::Ok;
}

ProgressiveJpegDc::Result parseHuffmanTables(DecodeState& state, uint32_t payloadSize) {
  while (payloadSize > 0) {
    uint8_t descriptor = 0;
    uint8_t counts[16] = {};
    if (payloadSize < 17 || !state.input.read(&descriptor, 1) || !state.input.read(counts, sizeof(counts))) {
      return inputFailure(state.input);
    }
    uint16_t symbolCount = 0;
    for (uint8_t count : counts) symbolCount = static_cast<uint16_t>(symbolCount + count);
    if (symbolCount > 256 || payloadSize < static_cast<uint32_t>(17 + symbolCount)) {
      return ProgressiveJpegDc::Result::InvalidData;
    }
    uint8_t symbols[256] = {};
    if (!state.input.read(symbols, symbolCount)) return inputFailure(state.input);

    const uint8_t tableClass = descriptor >> 4;
    const uint8_t tableIndex = descriptor & 0x0F;
    if (tableClass > 1 || tableIndex >= MAX_TABLES ||
        !(tableClass == 0 ? state.frame.dcTables[tableIndex] : state.frame.acTables[tableIndex])
             .build(counts, symbols, symbolCount)) {
      return ProgressiveJpegDc::Result::InvalidData;
    }
    payloadSize -= 17 + symbolCount;
  }
  return ProgressiveJpegDc::Result::Ok;
}

ProgressiveJpegDc::Result parseFrame(DecodeState& state, uint32_t payloadSize) {
  uint8_t fixed[6] = {};
  if (payloadSize < sizeof(fixed) || !state.input.read(fixed, sizeof(fixed))) return inputFailure(state.input);
  FrameDefinition& frame = state.frame;
  frame.height = static_cast<uint16_t>((fixed[1] << 8) | fixed[2]);
  frame.width = static_cast<uint16_t>((fixed[3] << 8) | fixed[4]);
  frame.componentCount = fixed[5];
  if (fixed[0] != 8 || frame.width == 0 || frame.height == 0 || frame.componentCount == 0 ||
      frame.componentCount > MAX_COMPONENTS || payloadSize != 6U + frame.componentCount * 3U) {
    return ProgressiveJpegDc::Result::Unsupported;
  }

  for (uint8_t index = 0; index < frame.componentCount; ++index) {
    uint8_t encoded[3] = {};
    if (!state.input.read(encoded, sizeof(encoded))) return inputFailure(state.input);
    FrameComponent& component = frame.components[index];
    component.identifier = encoded[0];
    component.horizontalFactor = encoded[1] >> 4;
    component.verticalFactor = encoded[1] & 0x0F;
    component.quantizer = encoded[2];
    if (component.horizontalFactor == 0 || component.verticalFactor == 0 || component.horizontalFactor > 4 ||
        component.verticalFactor > 4 || component.quantizer >= MAX_TABLES) {
      return ProgressiveJpegDc::Result::Unsupported;
    }
  }
  return ProgressiveJpegDc::Result::Ok;
}

ProgressiveJpegDc::Result parseScan(DecodeState& state, uint32_t payloadSize) {
  FrameDefinition& frame = state.frame;
  uint8_t scanCount = 0;
  if (!state.input.read(&scanCount, 1)) return inputFailure(state.input);
  if (frame.componentCount == 0 || scanCount == 0 || scanCount > MAX_COMPONENTS ||
      payloadSize != 1U + scanCount * 2U + 3U) {
    return ProgressiveJpegDc::Result::Unsupported;
  }
  frame.scanCount = scanCount;

  for (uint8_t scanIndex = 0; scanIndex < scanCount; ++scanIndex) {
    uint8_t selector[2] = {};
    if (!state.input.read(selector, sizeof(selector))) return inputFailure(state.input);
    uint8_t frameIndex = 0xFF;
    for (uint8_t candidate = 0; candidate < frame.componentCount; ++candidate) {
      if (frame.components[candidate].identifier == selector[0]) frameIndex = candidate;
    }
    const uint8_t dcTable = selector[1] >> 4;
    if (frameIndex == 0xFF || dcTable >= MAX_TABLES) return ProgressiveJpegDc::Result::Unsupported;
    frame.components[frameIndex].dcTable = dcTable;
    frame.components[frameIndex].acTable = selector[1] & 15;
    if (frame.components[frameIndex].acTable >= MAX_TABLES) return ProgressiveJpegDc::Result::Unsupported;
    frame.scanOrder[scanIndex] = frameIndex;
  }

  uint8_t progression[3] = {};
  if (!state.input.read(progression, sizeof(progression))) return inputFailure(state.input);
  const uint8_t successiveHigh = progression[2] >> 4;
  frame.successiveLow = progression[2] & 0x0F;
  frame.spectralStart = progression[0];
  frame.spectralEnd = progression[1];
  frame.successiveHigh = successiveHigh;
  if (frame.spectralStart > frame.spectralEnd || frame.spectralEnd > 63 ||
      (frame.spectralStart == 0 && frame.spectralEnd != 0) || (frame.spectralStart != 0 && scanCount != 1) ||
      frame.successiveLow > 13 || (successiveHigh != 0 && successiveHigh != frame.successiveLow + 1)) {
    return ProgressiveJpegDc::Result::Unsupported;
  }
  return ProgressiveJpegDc::Result::Ok;
}

ProgressiveJpegDc::Result parseHeaders(DecodeState& state) {
  uint8_t signature[2] = {};
  if (!state.input.read(signature, sizeof(signature)) || signature[0] != 0xFF || signature[1] != 0xD8) {
    return inputFailure(state.input);
  }

  bool progressiveFrameSeen = false;
  for (;;) {
    const int markerValue = nextMarker(state.input);
    if (markerValue < 0) return inputFailure(state.input);
    const uint8_t marker = static_cast<uint8_t>(markerValue);
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
    if (marker == 0xD9) return ProgressiveJpegDc::Result::InvalidData;

    uint32_t payloadSize = 0;
    if (!readSegmentSize(state.input, payloadSize)) return inputFailure(state.input);
    ProgressiveJpegDc::Result result = ProgressiveJpegDc::Result::Ok;
    if (marker == 0xDB) {
      result = parseQuantizers(state, payloadSize);
    } else if (marker == 0xC4) {
      result = parseHuffmanTables(state, payloadSize);
    } else if (isStartOfFrame(marker)) {
      if (marker != 0xC2) return ProgressiveJpegDc::Result::Unsupported;
      result = parseFrame(state, payloadSize);
      progressiveFrameSeen = result == ProgressiveJpegDc::Result::Ok;
    } else if (marker == 0xDD) {
      uint8_t interval[2] = {};
      if (payloadSize != 2 || !state.input.read(interval, sizeof(interval))) {
        return ProgressiveJpegDc::Result::InvalidData;
      }
      state.frame.restartInterval = static_cast<uint16_t>((interval[0] << 8) | interval[1]);
    } else if (marker == 0xDA) {
      if (!progressiveFrameSeen) return ProgressiveJpegDc::Result::InvalidData;
      return parseScan(state, payloadSize);
    } else if (!state.input.discard(payloadSize)) {
      return inputFailure(state.input);
    }
    if (result != ProgressiveJpegDc::Result::Ok) return result;
  }
}

class RowScaler {
 public:
  RowScaler(uint16_t sourceWidth, uint16_t sourceHeight, uint16_t outputWidth, uint16_t outputHeight, uint8_t* rows,
            ProgressiveJpegDc::RowCallback callback, void* callbackUser)
      : sourceWidth_(sourceWidth),
        sourceHeight_(sourceHeight),
        outputWidth_(outputWidth),
        outputHeight_(outputHeight),
        previous_(rows),
        current_(rows + outputWidth),
        blended_(rows + outputWidth * 2U),
        callback_(callback),
        callbackUser_(callbackUser) {}

  bool submit(const uint8_t* sourceRow, uint16_t sourceY) {
    resizeHorizontal(sourceRow, current_);
    while (nextOutputY_ < outputHeight_) {
      const uint32_t sourcePosition = mapCoordinate(nextOutputY_, outputHeight_, sourceHeight_);
      const uint16_t lowerY = static_cast<uint16_t>(sourcePosition >> 16);
      const uint16_t upperY = std::min<uint16_t>(lowerY + 1, sourceHeight_ - 1);
      if (upperY > sourceY) break;

      const uint8_t* lower = lowerY == sourceY ? current_ : previous_;
      const uint8_t* upper = upperY == sourceY ? current_ : previous_;
      const uint32_t fraction = sourcePosition & 0xFFFF;
      for (uint16_t x = 0; x < outputWidth_; ++x) {
        blended_[x] = interpolate(lower[x], upper[x], fraction);
      }
      if (!callback_(callbackUser_, nextOutputY_, blended_, outputWidth_)) return false;
      ++nextOutputY_;
    }
    std::swap(previous_, current_);
    submittedY_ = sourceY;
    return true;
  }

  bool finish() {
    while (nextOutputY_ < outputHeight_ && submittedY_ >= 0) {
      if (!callback_(callbackUser_, nextOutputY_, previous_, outputWidth_)) return false;
      ++nextOutputY_;
    }
    return nextOutputY_ == outputHeight_;
  }

 private:
  // 16.16 source position for one output row/column.
  //
  // The intermediate MUST be 64-bit. `*` binds tighter than `<<`, so the product is formed first
  // and then shifted; in 32 bits that wraps as soon as outputIndex * (sourceSize - 1) reaches
  // 65536 — for a 1920x2708 progressive cover scaled to 382x540 that is output row 194 of 540.
  // Past the wrap, lowerY collapses to ~0, submit()'s "have we reached this source row yet" test
  // passes for every remaining row at once, and they all get emitted from the last decoded row:
  // the top third of the image decodes correctly and the rest is that one row repeated.
  // The RESULT always fits in uint32 ((sourceSize - 1) << 16 <= 0xFFFF0000); only the
  // intermediate needs the wider type.
  static uint32_t mapCoordinate(uint16_t outputIndex, uint16_t outputSize, uint16_t sourceSize) {
    if (outputSize <= 1 || sourceSize <= 1) return 0;
    if (outputIndex + 1 == outputSize) return static_cast<uint32_t>(sourceSize - 1) << 16;
    return static_cast<uint32_t>((static_cast<uint64_t>(outputIndex) * (sourceSize - 1) << 16) / (outputSize - 1));
  }

  static uint8_t interpolate(uint8_t first, uint8_t second, uint32_t fraction) {
    return static_cast<uint8_t>((first * (0x10000U - fraction) + second * fraction) >> 16);
  }

  void resizeHorizontal(const uint8_t* source, uint8_t* output) const {
    for (uint16_t x = 0; x < outputWidth_; ++x) {
      const uint32_t sourcePosition = mapCoordinate(x, outputWidth_, sourceWidth_);
      const uint16_t left = static_cast<uint16_t>(sourcePosition >> 16);
      const uint16_t right = std::min<uint16_t>(left + 1, sourceWidth_ - 1);
      output[x] = interpolate(source[left], source[right], sourcePosition & 0xFFFF);
    }
  }

  uint16_t sourceWidth_;
  uint16_t sourceHeight_;
  uint16_t outputWidth_;
  uint16_t outputHeight_;
  uint8_t* previous_;
  uint8_t* current_;
  uint8_t* blended_;
  ProgressiveJpegDc::RowCallback callback_;
  void* callbackUser_;
  uint16_t nextOutputY_ = 0;
  int32_t submittedY_ = -1;
};

}  // namespace

namespace ProgressiveJpegDc {

Result probe(FsFile& file, ImageInfo& info) {
  info = {};
  if (!file || !file.seek(0)) return Result::InvalidData;

  auto finish = [&](Result result) {
    file.seek(0);
    return result;
  };
  uint8_t signature[2] = {};
  if (file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) || signature[0] != 0xFF ||
      signature[1] != 0xD8) {
    return finish(Result::InvalidData);
  }

  auto readByte = [&]() -> int {
    uint8_t value = 0;
    return file.read(&value, 1) == 1 ? value : -1;
  };
  for (;;) {
    int value = readByte();
    while (value >= 0 && value != 0xFF) value = readByte();
    if (value < 0) return finish(Result::InvalidData);
    do {
      value = readByte();
    } while (value == 0xFF);
    if (value < 0) return finish(Result::InvalidData);
    const uint8_t marker = static_cast<uint8_t>(value);
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
    if (marker == 0xD9 || marker == 0xDA) return finish(Result::InvalidData);

    uint8_t lengthBytes[2] = {};
    if (file.read(lengthBytes, sizeof(lengthBytes)) != static_cast<int>(sizeof(lengthBytes))) {
      return finish(Result::InvalidData);
    }
    const uint16_t segmentSize = static_cast<uint16_t>((lengthBytes[0] << 8) | lengthBytes[1]);
    if (segmentSize < 2) return finish(Result::InvalidData);
    if (isStartOfFrame(marker)) {
      if (marker != 0xC2) return finish(Result::Unsupported);
      uint8_t frame[5] = {};
      if (segmentSize < 8 || file.read(frame, sizeof(frame)) != static_cast<int>(sizeof(frame))) {
        return finish(Result::InvalidData);
      }
      info.height = static_cast<uint16_t>((frame[1] << 8) | frame[2]);
      info.width = static_cast<uint16_t>((frame[3] << 8) | frame[4]);
      return finish(info.width != 0 && info.height != 0 && frame[0] == 8 ? Result::Ok : Result::Unsupported);
    }
    if (!file.seek(file.position() + segmentSize - 2)) return finish(Result::InvalidData);
  }
}

Result decodeDc(FsFile& file, const DecodeOptions& options, RowCallback rowCallback, void* rowUser) {
  if (!file || options.outputWidth == 0 || options.outputHeight == 0 || rowCallback == nullptr || !file.seek(0)) {
    return Result::InvalidData;
  }

  // The canonical tables and buffered source are too large for the reader task
  // stack, but remain one bounded allocation for the duration of this cold path.
  auto state = makeUniqueNoThrow<DecodeState>(file);
  if (!state) return Result::OutOfMemory;
  const Result headerResult = parseHeaders(*state);
  if (headerResult != Result::Ok) return headerResult;

  FrameDefinition& frame = state->frame;
  if (frame.spectralStart != 0 || frame.successiveHigh != 0) return Result::Unsupported;
  uint8_t maxHorizontalFactor = 1;
  uint8_t maxVerticalFactor = 1;
  for (uint8_t index = 0; index < frame.componentCount; ++index) {
    maxHorizontalFactor = std::max(maxHorizontalFactor, frame.components[index].horizontalFactor);
    maxVerticalFactor = std::max(maxVerticalFactor, frame.components[index].verticalFactor);
  }

  const FrameComponent& luma = frame.components[0];
  if (luma.horizontalFactor != maxHorizontalFactor || luma.verticalFactor != maxVerticalFactor) {
    return Result::Unsupported;
  }
  const bool interleaved = frame.scanCount > 1;
  if (!interleaved && frame.scanOrder[0] != 0) return Result::Unsupported;

  const uint16_t lumaColumns = static_cast<uint16_t>((frame.width + 7) / 8);
  const uint16_t lumaRows = static_cast<uint16_t>((frame.height + 7) / 8);
  const uint16_t mcuColumns =
      static_cast<uint16_t>((frame.width + 8 * maxHorizontalFactor - 1) / (8 * maxHorizontalFactor));
  const uint16_t mcuRows = static_cast<uint16_t>((frame.height + 8 * maxVerticalFactor - 1) / (8 * maxVerticalFactor));
  const uint16_t paddedColumns = interleaved ? static_cast<uint16_t>(mcuColumns * luma.horizontalFactor) : lumaColumns;

  const size_t scaledRowsSize = static_cast<size_t>(options.outputWidth) * 3;
  const size_t decodedRowsSize = static_cast<size_t>(paddedColumns) * luma.verticalFactor;
  auto rowStorage = makeUniqueNoThrow<uint8_t[]>(scaledRowsSize + decodedRowsSize);
  if (!rowStorage) return Result::OutOfMemory;
  uint8_t* decodedRows = rowStorage.get() + scaledRowsSize;
  RowScaler scaler(lumaColumns, lumaRows, options.outputWidth, options.outputHeight, rowStorage.get(), rowCallback,
                   rowUser);

  int32_t predictors[MAX_COMPONENTS] = {};
  uint32_t unitsSinceRestart = 0;
  uint16_t emittedLumaRows = 0;
  const uint32_t encodedRows = interleaved ? mcuRows : lumaRows;
  const uint32_t encodedColumns = interleaved ? mcuColumns : lumaColumns;

  for (uint32_t row = 0; row < encodedRows; ++row) {
    if (options.shouldAbort && options.shouldAbort(options.abortUser)) return Result::Aborted;
    memset(decodedRows, 0, decodedRowsSize);

    for (uint32_t column = 0; column < encodedColumns; ++column) {
      if (frame.restartInterval != 0 && unitsSinceRestart == frame.restartInterval) {
        if (!state->consumeRestartMarker()) return inputFailure(state->input);
        memset(predictors, 0, sizeof(predictors));
        unitsSinceRestart = 0;
      }

      const uint8_t componentsInUnit = interleaved ? frame.scanCount : 1;
      for (uint8_t scanIndex = 0; scanIndex < componentsInUnit; ++scanIndex) {
        const uint8_t componentIndex = frame.scanOrder[scanIndex];
        const FrameComponent& component = frame.components[componentIndex];
        const HuffmanTable& table = frame.dcTables[component.dcTable];
        if (!table.valid) return Result::InvalidData;
        const uint8_t horizontalBlocks = interleaved ? component.horizontalFactor : 1;
        const uint8_t verticalBlocks = interleaved ? component.verticalFactor : 1;

        for (uint8_t blockY = 0; blockY < verticalBlocks; ++blockY) {
          for (uint8_t blockX = 0; blockX < horizontalBlocks; ++blockX) {
            const int category = state->decodeSymbol(table);
            if (category < 0 || category > 11) return inputFailure(state->input);
            int32_t difference = 0;
            if (!state->receiveSigned(static_cast<uint8_t>(category), difference)) return inputFailure(state->input);
            const int64_t prediction = int64_t(predictors[componentIndex]) + difference;
            if (prediction < INT32_MIN || prediction > INT32_MAX) return Result::InvalidData;
            predictors[componentIndex] = static_cast<int32_t>(prediction);

            if (componentIndex == 0) {
              const int64_t coefficient =
                  static_cast<int64_t>(predictors[0]) * (1L << frame.successiveLow) * frame.dcQuantizer[luma.quantizer];
              const int32_t grayscale = static_cast<int32_t>(std::clamp<int64_t>(128 + coefficient / 8, 0, 255));
              const uint32_t x = interleaved ? column * luma.horizontalFactor + blockX : column;
              if (x < paddedColumns) {
                decodedRows[static_cast<size_t>(blockY) * paddedColumns + x] = static_cast<uint8_t>(grayscale);
              }
            }
          }
        }
      }
      ++unitsSinceRestart;
    }

    const uint8_t rowsInUnit = interleaved ? luma.verticalFactor : 1;
    for (uint8_t localRow = 0; localRow < rowsInUnit && emittedLumaRows < lumaRows; ++localRow) {
      if (!scaler.submit(decodedRows + static_cast<size_t>(localRow) * paddedColumns, emittedLumaRows)) {
        return Result::Stopped;
      }
      ++emittedLumaRows;
    }
  }

  if (emittedLumaRows != lumaRows) return Result::InvalidData;
  return scaler.finish() ? Result::Ok : Result::Stopped;
}

// Full luminance reconstruction for small progressive covers. Re-read scans per
// block row; only nonzero masks survive outside that row. AC refinement needs
// those masks to consume bits, but not the discarded coefficients themselves.
// This trades bounded SD reads for a full-image 128-byte-per-block allocation.
static Result decodeDetailed(FsFile& file, const DecodeOptions& options, RowCallback callback, void* user) {
  if (!file.seek(0)) return Result::IoError;
  auto state = makeUniqueNoThrow<DecodeState>(file);
  if (!state) return Result::OutOfMemory;
  Result result = parseHeaders(*state);
  if (result != Result::Ok) return result;
  const FrameDefinition& initial = state->frame;
  const uint16_t width = initial.width, height = initial.height;
  uint8_t maxH = 1, maxV = 1;
  for (unsigned c = 0; c < initial.componentCount; ++c) {
    maxH = std::max(maxH, initial.components[c].horizontalFactor);
    maxV = std::max(maxV, initial.components[c].verticalFactor);
  }
  if (initial.components[0].horizontalFactor != maxH || initial.components[0].verticalFactor != maxV ||
      (initial.componentCount != 1 && initial.componentCount != 3))
    return Result::Unsupported;
  const unsigned columns = (width + 7) / 8, rows = (height + 7) / 8;
  const unsigned mcuColumns = (width + 8 * maxH - 1) / (8 * maxH);
  const unsigned mcuRows = (height + 8 * maxV - 1) / (8 * maxV);
  const unsigned stride = mcuColumns * maxH, paddedRows = mcuRows * maxV;
  // Bound repeat work as well as memory. Larger covers retain the DC preview.
  if (width > 512 || height > 768 || stride * paddedRows > 4096) return Result::Unsupported;
  auto nonzero = makeUniqueNoThrow<uint64_t[]>(stride * paddedRows);
  auto coefficients = makeUniqueNoThrow<int16_t[]>(stride * 64);
  auto pixels = makeUniqueNoThrow<uint8_t[]>(width * 8 + options.outputWidth * 3);
  auto basis = makeUniqueNoThrow<float[]>(64);
  if (!nonzero || !coefficients || !pixels || !basis) return Result::OutOfMemory;
  for (unsigned x = 0; x < 8; ++x)
    for (unsigned u = 0; u < 8; ++u)
      basis[x * 8 + u] = (u == 0 ? 0.70710678118f : 1.0f) * std::cos((2 * x + 1) * u * 0.19634954085f);
  constexpr uint8_t zigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                  58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
  RowScaler scaler(width, height, options.outputWidth, options.outputHeight, pixels.get() + width * 8, callback, user);
#ifdef ARDUINO
  unsigned long lastYield = millis();
#endif
  const auto cooperate = [&]() {
#ifdef ARDUINO
    if (millis() - lastYield >= 20) {
      delay(1);
      lastYield = millis();
    }
#endif
  };
  for (unsigned wantedRow = 0; wantedRow < rows; ++wantedRow) {
    cooperate();
    if (options.shouldAbort && options.shouldAbort(options.abortUser)) return Result::Aborted;
    memset(nonzero.get(), 0, stride * paddedRows * sizeof(uint64_t));
    memset(coefficients.get(), 0, stride * 64 * sizeof(int16_t));
    if (wantedRow != 0) {
      if (!file.seek(0)) return Result::IoError;
      state->~DecodeState();
      new (state.get()) DecodeState(file);
      result = parseHeaders(*state);
      if (result != Result::Ok) return result;
    }
    unsigned scanNumber = 0;
    bool complete = false, lumaDcSeen = false;
    while (!complete) {
      auto& f = state->frame;
      if (++scanNumber > 128) return Result::Unsupported;
      state->entropyBits = 0;
      state->pendingMarker = 0;
      const bool interleaved = f.scanCount > 1;
      const bool lumaScan = interleaved || f.scanOrder[0] == 0;
      if (interleaved && f.spectralStart != 0) return Result::InvalidData;
      if (lumaScan) {
        if (f.spectralStart == 0 && f.successiveHigh == 0) lumaDcSeen = true;
        int32_t predictors[MAX_COMPONENTS] = {};
        uint32_t eobRun = 0, units = 0;
        const unsigned scanRows = interleaved ? mcuRows : rows;
        const unsigned scanColumns = interleaved ? mcuColumns : columns;
        for (unsigned my = 0; my < scanRows; ++my) {
          cooperate();
          if (options.shouldAbort && options.shouldAbort(options.abortUser)) return Result::Aborted;
          for (unsigned mx = 0; mx < scanColumns; ++mx) {
            if (f.restartInterval && units == f.restartInterval) {
              if (!state->consumeRestartMarker()) return Result::InvalidData;
              memset(predictors, 0, sizeof(predictors));
              eobRun = 0;
              units = 0;
            }
            for (unsigned si = 0; si < f.scanCount; ++si) {
              const unsigned ci = f.scanOrder[si];
              const auto& component = f.components[ci];
              const unsigned h = interleaved ? component.horizontalFactor : 1;
              const unsigned v = interleaved ? component.verticalFactor : 1;
              for (unsigned by = 0; by < v; ++by)
                for (unsigned bx = 0; bx < h; ++bx) {
                  const unsigned x = interleaved ? mx * h + bx : mx;
                  const unsigned y = interleaved ? my * v + by : my;
                  int16_t* block = ci == 0 && y == wantedRow ? coefficients.get() + x * 64 : nullptr;
                  uint64_t ignoredMask = 0;
                  uint64_t& mask = ci == 0 ? nonzero[y * stride + x] : ignoredMask;
                  auto set = [&](unsigned k, int64_t value) -> bool {
                    if (value < -32768 || value > 32767) return false;
                    if (value) mask |= uint64_t(1) << k;
                    if (block) block[k] = static_cast<int16_t>(value);
                    return true;
                  };
                  auto correction = [&](unsigned k) -> bool {
                    const int bit = state->nextEntropyBit();
                    if (bit < 0) return false;
                    if (bit && block) {
                      const int delta = 1 << f.successiveLow;
                      if ((std::abs(int(block[k])) & delta) == 0)
                        return set(k, int(block[k]) + (block[k] > 0 ? delta : -delta));
                    }
                    return true;
                  };
                  if (f.spectralStart == 0) {
                    if (f.successiveHigh == 0) {
                      const auto& table = f.dcTables[component.dcTable];
                      if (!table.valid) return Result::InvalidData;
                      const int category = state->decodeSymbol(table);
                      int32_t difference = 0;
                      if (category < 0 || category > 11 || !state->receiveSigned(category, difference))
                        return Result::InvalidData;
                      predictors[ci] += difference;
                      if (!set(0, int64_t(predictors[ci]) * (1 << f.successiveLow))) return Result::InvalidData;
                    } else {
                      const int bit = state->nextEntropyBit();
                      if (bit < 0) return Result::InvalidData;
                      if (block && bit) block[0] |= 1 << f.successiveLow;
                    }
                    continue;
                  }
                  const auto& table = f.acTables[component.acTable];
                  if (!table.valid) return Result::InvalidData;
                  unsigned k = f.spectralStart;
                  if (f.successiveHigh == 0) {
                    if (eobRun) {
                      --eobRun;
                      continue;
                    }
                    while (k <= f.spectralEnd) {
                      const int symbol = state->decodeSymbol(table);
                      if (symbol < 0) return Result::InvalidData;
                      const unsigned run = symbol >> 4, size = symbol & 15;
                      if (size == 0 && run != 15) {
                        int32_t extra = 0;
                        // EOB-run bits are unsigned, unlike coefficient amplitudes.
                        for (unsigned b = 0; b < run; ++b) {
                          int bit = state->nextEntropyBit();
                          if (bit < 0) return Result::InvalidData;
                          extra = extra * 2 + bit;
                        }
                        eobRun = (1U << run) + extra - 1;
                        break;
                      }
                      k += run;
                      if (size == 0) {
                        ++k;
                        continue;
                      }
                      int32_t value = 0;
                      if (k > f.spectralEnd || size > 10 || !state->receiveSigned(size, value) ||
                          !set(k++, value * (1 << f.successiveLow)))
                        return Result::InvalidData;
                    }
                  } else {
                    if (eobRun == 0) {
                      while (k <= f.spectralEnd) {
                        const int symbol = state->decodeSymbol(table);
                        if (symbol < 0) return Result::InvalidData;
                        int run = symbol >> 4;
                        const unsigned size = symbol & 15;
                        int32_t value = 0;
                        if (size) {
                          if (size != 1) return Result::InvalidData;
                          const int sign = state->nextEntropyBit();
                          if (sign < 0) return Result::InvalidData;
                          value = sign ? 1 << f.successiveLow : -(1 << f.successiveLow);
                        } else if (run != 15) {
                          uint32_t extra = 0;
                          for (int b = 0; b < run; ++b) {
                            int bit = state->nextEntropyBit();
                            if (bit < 0) return Result::InvalidData;
                            extra = extra * 2 + bit;
                          }
                          eobRun = (1U << run) + extra;
                          break;
                        }
                        for (; k <= f.spectralEnd; ++k) {
                          if (mask & (uint64_t(1) << k)) {
                            if (!correction(k)) return Result::InvalidData;
                          } else if (--run < 0)
                            break;
                        }
                        if (value) {
                          if (k > f.spectralEnd || !set(k, value)) return Result::InvalidData;
                        }
                        ++k;
                      }
                    }
                    if (eobRun) {
                      for (; k <= f.spectralEnd; ++k)
                        if ((mask & (uint64_t(1) << k)) && !correction(k)) return Result::InvalidData;
                      --eobRun;
                    }
                  }
                }
            }
            ++units;
          }
        }
      }
      // Scan entropy can contain FF00 escapes and restart markers. Consume to
      // the next length-bearing marker, including when skipping chroma AC.
      state->entropyBits = 0;
      for (;;) {
        const int marker = state->pendingMarker ? std::exchange(state->pendingMarker, 0) : nextMarker(state->input);
        if (marker < 0) return inputFailure(state->input);
        if (marker == 0 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (marker == 0xD9) {
          complete = true;
          break;
        }
        uint32_t size = 0;
        if (!readSegmentSize(state->input, size)) return inputFailure(state->input);
        if (marker == 0xC4)
          result = parseHuffmanTables(*state, size);
        else if (marker == 0xDB)
          result = parseQuantizers(*state, size);
        else if (marker == 0xDA) {
          result = parseScan(*state, size);
          break;
        } else if (marker == 0xDD) {
          uint8_t interval[2];
          if (size != 2 || !state->input.read(interval, 2)) return Result::InvalidData;
          state->frame.restartInterval = (interval[0] << 8) | interval[1];
        } else if (!state->input.discard(size))
          return inputFailure(state->input);
        if (result != Result::Ok) return result;
      }
      if (result != Result::Ok) return result;
    }
    if (!lumaDcSeen) return Result::InvalidData;
    const auto& quant = state->frame.quantizers[state->frame.components[0].quantizer];
    for (unsigned bx = 0; bx < columns; ++bx) {
      float dequantized[64] = {}, intermediate[64] = {};
      const auto* block = coefficients.get() + bx * 64;
      for (unsigned k = 0; k < 64; ++k) dequantized[zigzag[k]] = float(block[k]) * quant[k];
      for (unsigned v = 0; v < 8; ++v)
        for (unsigned x = 0; x < 8; ++x) {
          float sum = 0;
          for (unsigned u = 0; u < 8; ++u) sum += dequantized[v * 8 + u] * basis[x * 8 + u];
          intermediate[v * 8 + x] = sum;
        }
      for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8 && bx * 8 + x < width; ++x) {
          float sum = 0;
          for (unsigned v = 0; v < 8; ++v) sum += intermediate[v * 8 + x] * basis[y * 8 + v];
          pixels[y * width + bx * 8 + x] =
              static_cast<uint8_t>(std::lround(std::clamp(sum * 0.25f + 128.0f, 0.0f, 255.0f)));
        }
    }
    for (unsigned y = 0; y < 8 && wantedRow * 8 + y < height; ++y)
      if (!scaler.submit(pixels.get() + y * width, wantedRow * 8 + y)) return Result::Stopped;
  }
  return scaler.finish() ? Result::Ok : Result::Stopped;
}

Result decode(FsFile& file, const DecodeOptions& options, RowCallback callback, void* user) {
  if (!file || !callback || !options.outputWidth || !options.outputHeight) return Result::InvalidData;
  struct Output {
    RowCallback callback;
    void* user;
    bool started = false;
  } output{callback, user};
  const auto forward = [](void* context, uint16_t y, const uint8_t* pixels, uint16_t width) {
    auto& output = *static_cast<Output*>(context);
    output.started = true;
    return output.callback(output.user, y, pixels, width);
  };
  const Result detailed = decodeDetailed(file, options, forward, &output);
  if (output.started || (detailed != Result::Unsupported && detailed != Result::OutOfMemory)) return detailed;
  return decodeDc(file, options, callback, user);
}

const char* resultName(Result result) {
  switch (result) {
    case Result::Ok:
      return "ok";
    case Result::Unsupported:
      return "unsupported";
    case Result::InvalidData:
      return "invalid data";
    case Result::IoError:
      return "I/O error";
    case Result::OutOfMemory:
      return "out of memory";
    case Result::Aborted:
      return "aborted";
    case Result::Stopped:
      return "stopped";
  }
  return "unknown";
}

}  // namespace ProgressiveJpegDc
