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
    uint8_t is_start_block : 1;
    uint8_t is_piece_block : 1;
    uint8_t is_end_block : 1;

    bool is_single_message() {
      return is_start_block == 0 && is_piece_block == 0 && is_end_block == 0;
    }
  };

  static const size_t MAX_MESSAGE_SIZE = 1024 * 4;
  static const size_t SIZE_OF_SIZE = sizeof(size_t);
  static const size_t SIZE_OF_HEADER = sizeof(header_t);
  static const size_t MAX_BUFFER_SIZE = MAX_MESSAGE_SIZE - SIZE_OF_HEADER - SIZE_OF_SIZE;

  message(message &&other)
      : _data(std::move(other._data)) {
    _size = (size_t *)_data.data();
    *_size = *other._size;
  }

  message(const message &other)
      : _data(other._data) {
    _size = (size_t *)_data.data();
    *_size = *other._size;
  }

  message(size_t sz) { init_for_size(sz); }

  message(size_t sz, const kind_t &kind_) {
    auto full_size = static_cast<size_t>(sz + SIZE_OF_SIZE + SIZE_OF_HEADER);
    assert(full_size <= MAX_MESSAGE_SIZE);
    init_for_size(full_size);
    get_header()->kind = kind_;
  }

  ~message() {}

  uint8_t *value() { return (_data.data() + SIZE_OF_SIZE + sizeof(header_t)); }
  size_t values_size() const { return *_size - SIZE_OF_SIZE - SIZE_OF_HEADER; }
  size_t size() const { return *_size; }

  buffer as_buffer() {
    uint8_t *v = reinterpret_cast<uint8_t *>(_data.data());
    auto buf_size = *_size;
    return buffer{buf_size, v};
  }

  header_t *get_header() {
    return reinterpret_cast<header_t *>(this->_data.data() + SIZE_OF_SIZE);
  }

private:
  void init_for_size(size_t sz) {
    std::fill(std::begin(_data), std::end(_data), uint8_t(0));
    _size = (size_t *)_data.data();
    *_size = sz;
    auto hdr = get_header();
    hdr->is_end_block = hdr->is_piece_block = hdr->is_start_block = uint8_t(0);
  }

private:
  size_t *_size;
  std::array<uint8_t, MAX_MESSAGE_SIZE> _data;
};

#pragma pack(pop)

using message_ptr = std::shared_ptr<message>;
} // namespace dialler
