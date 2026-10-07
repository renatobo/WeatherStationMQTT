#pragma once
// Project modifications and modernization by Renato Bonomini (renatobo); MIT, see LICENSE.
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace Weather {
enum class Error { None, TotalTimeout, IdleTimeout, TooLarge, Connect, Http, Json,
                   Schema, Clock, LowMemory, Encoding, Truncated, Cancelled };

inline const char* errorName(Error error) {
  switch (error) {
    case Error::None: return "none";
    case Error::TotalTimeout: return "total timeout";
    case Error::IdleTimeout: return "idle timeout";
    case Error::TooLarge: return "response too large";
    case Error::Connect: return "TLS/connect failure";
    case Error::Http: return "HTTP status";
    case Error::Json: return "invalid weather JSON/data";
    case Error::Schema: return "invalid weather data";
    case Error::Clock: return "clock not synchronized";
    case Error::LowMemory: return "insufficient heap";
    case Error::Encoding: return "unsupported encoding";
    case Error::Truncated: return "truncated response";
    case Error::Cancelled: return "cancelled";
  }
  return "unknown";
}

// A request has an eight-second total budget and a 1.5-second idle budget.
// First failure wins; wire-byte accounting also bounds header/body traffic.
struct Deadline {
  uint32_t started;
  uint32_t lastData;
  uint32_t bytes = 0;
  int tlsError = 0;
  const char* stage = "not started";
  Error error = Error::None;

  explicit Deadline(uint32_t now): started(now), lastData(now) {}

  bool check(uint32_t now) {
    if (error != Error::None) return false;
    if (static_cast<uint32_t>(now - started) >= 8000) error = Error::TotalTimeout;
    else if (static_cast<uint32_t>(now - lastData) >= 1500) error = Error::IdleTimeout;
    return error == Error::None;
  }

  bool received(uint32_t now, uint32_t count) {
    if (!check(now)) return false;
    if (!count) return true;
    if (count > 8192 - bytes) {
      error = Error::TooLarge;
      return false;
    }
    bytes += count;
    lastData = now;
    return true;
  }
};

struct Buffer {
  char* data;
  size_t capacity, used = 0;
  Buffer(char* storage, size_t size): data(storage), capacity(size) {
    if (capacity) data[0] = '\0';
  }
  bool append(const uint8_t* bytes, size_t count) {
    if (!capacity || count > capacity - 1 - used) return false;
    std::memcpy(data + used, bytes, count);
    used += count; data[used] = '\0';
    return true;
  }
};
}  // namespace Weather
