#include "PreviewBlockLocator.h"

#include <cstring>

PreviewBlockLocator::PreviewBlockLocator(const char* anchorId, const IsBlockTagFn isBlockTag)
    : anchorId_(anchorId), isBlockTag_(isBlockTag) {
  if (!anchorId_ || anchorId_[0] == '\0' || !isBlockTag_) return;
  parser_.init(this, handleStartElement, handleEndElement, nullptr, nullptr,
               /*htmlVoidTagRepair=*/true);
}

bool PreviewBlockLocator::feed(const uint8_t* data, const size_t len) {
  if (!ok() || done()) return ok();
  return parser_.feed(data, len);
}

bool PreviewBlockLocator::finalize() {
  if (!ok()) return false;
  return parser_.finalize();
}

void PreviewBlockLocator::handleStartElement(void* userData, const char* name, const char** atts) {
  auto* self = static_cast<PreviewBlockLocator*>(userData);
  self->ordinal_ += 1;

  const char* id = nullptr;
  for (int i = 0; atts && atts[i]; i += 2) {
    if (strcmp(atts[i], "id") == 0 || (strcmp(name, "a") == 0 && strcmp(atts[i], "name") == 0)) {
      id = atts[i + 1];
      break;
    }
  }

  if (id && strcmp(id, self->anchorId_) == 0) {
    self->startOrdinal_ = (self->isBlockTag_(name) || self->openBlockCount_ == 0)
                              ? self->ordinal_
                              : self->openBlockOrdinals_[self->openBlockCount_ - 1];
    self->parser_.stop();
    return;
  }

  if (self->isBlockTag_(name) && self->openBlockCount_ < MAX_BLOCK_NESTING) {
    self->openBlockOrdinals_[self->openBlockCount_] = self->ordinal_;
    self->openBlockDepths_[self->openBlockCount_] = self->depth_;
    self->openBlockCount_ += 1;
  }
  self->depth_ += 1;
}

void PreviewBlockLocator::handleEndElement(void* userData, const char* name) {
  (void)name;
  auto* self = static_cast<PreviewBlockLocator*>(userData);
  if (self->depth_ > 0) self->depth_ -= 1;
  if (self->openBlockCount_ > 0 && self->openBlockDepths_[self->openBlockCount_ - 1] == self->depth_) {
    self->openBlockCount_ -= 1;
  }
}
