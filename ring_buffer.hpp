#ifndef RINGBUFFER_HPP
#define RINGBUFFER_HPP

#include <chrono>
#include <vector>

class RingBuffer {
public:
  RingBuffer(size_t size);
  void add(std::chrono::microseconds value);
  std::chrono::microseconds average() const;

private:
  size_t size_;
  std::vector<std::chrono::microseconds> buffer_;
  size_t index_;
  bool full_;
  std::chrono::microseconds total_duration_;
};

#endif // RINGBUFFER_HPP
