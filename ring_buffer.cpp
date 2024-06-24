#include "ring_buffer.hpp"

RingBuffer::RingBuffer(size_t size)
    : size_(size), buffer_(size), index_(0), full_(false),
      total_duration_(std::chrono::microseconds(0)) {}

void RingBuffer::add(std::chrono::microseconds value) {
  if (full_) {
    total_duration_ -= buffer_[index_];
  }
  buffer_[index_] = value;
  total_duration_ += value;

  index_ = (index_ + 1) % size_;
  if (index_ == 0) {
    full_ = true;
  }
}

std::chrono::microseconds RingBuffer::average() const {
  size_t count = full_ ? size_ : index_;
  if (count == 0) {
    return std::chrono::microseconds(0); // Avoid division by zero
  }
  return total_duration_ / count;
}
