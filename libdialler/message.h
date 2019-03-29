#pragma once

#include <libdialler/exports.h>
#include <array>
#include <cassert>
#include <cstdint>

namespace dialler {
struct buffer {
  size_t size;
  uint8_t *data;
};

#pragma pack(push, 1)

class message {
public:
  using size_t = uint32_t;
  using kind_t = uint16_t;

  struct header_t {
    kind_t kind;
  };

  static const size_t MAX_MESSAGE_SIZE = 1024 * 4;
  static const size_t SIZE_OF_SIZE = sizeof(size_t);
  static const size_t SIZE_OF_HEADER = sizeof(header_t);
  static const size_t MAX_BUFFER_SIZE = MAX_MESSAGE_SIZE - SIZE_OF_HEADER;

  message(message &&other) = default;

  message(const message &other)
      : _data(other._data) {
    _size = (size_t *)_data.data();
    *_size = *other._size;
  }

  message(size_t sz) {
    assert((sz + SIZE_OF_SIZE + SIZE_OF_HEADER) < MAX_MESSAGE_SIZE);
    auto realSize = static_cast<size_t>(sz + SIZE_OF_SIZE);
    std::fill(std::begin(_data), std::end(_data), uint8_t(0));
    _size = (size_t *)_data.data();
    *_size = realSize;
  }

  message(size_t sz, const kind_t &kind_)
      : message(sz) {
    *_size += SIZE_OF_HEADER;
    get_header()->kind = kind_;
  }

  ~message() {}

  uint8_t *value() { return (_data.data() + SIZE_OF_SIZE + sizeof(header_t)); }
  size_t size() { return *_size; }

  buffer as_buffer() {
    uint8_t *v = reinterpret_cast<uint8_t *>(_data.data());
    auto buf_size = *_size;
    return buffer{buf_size, v};
  }

  header_t *get_header() {
    return reinterpret_cast<header_t *>(this->_data.data() + SIZE_OF_SIZE);
  }

private:
  size_t *_size;
  std::array<uint8_t, MAX_MESSAGE_SIZE> _data;
};

#pragma pack(pop)

using message_ptr = std::shared_ptr<message>;
} // namespace dialler
