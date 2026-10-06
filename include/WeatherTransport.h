#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <cstring>
#include <memory>
#include "WeatherDeadline.h"

namespace Weather {
// Use the context directly: the SDK wrapper's Stream timeout is not the TLS context's timeout.
class TlsClient: public BearSSL::WiFiClientSecureCtx {
 public:
  TlsClient(Deadline& deadline, void (*pump)(), const BearSSL::X509List* anchors)
      : deadline_(deadline), pump_(pump), anchors_(anchors) {
    setTrustAnchors(anchors);
    setSSLVersion(BR_TLS12, BR_TLS12);
    setBufferSizes(8192, 512); // oversized records fail closed, never disable trust
    setTimeout(250);
  }

  std::unique_ptr<WiFiClient> clone() const override {
    return std::unique_ptr<WiFiClient>(new TlsClient(deadline_, pump_, anchors_));
  }

  int connect(const char* host, uint16_t port) override {
    // HTTP redirects are disabled; preserve verified SNI/hostname through _connectSSL.
    if (std::strcmp(host, "api.open-meteo.com") || port != 443) return 0;
    IPAddress address;
    deadline_.stage = "DNS";
    if (!WiFi.hostByName(host, address, 1000)) return 0;
    setTimeout(1000);
    deadline_.stage = "TCP";
    if (!WiFiClient::connect(address, port)) return 0;
    deadline_.stage = "TLS handshake";
    int result = _connectSSL(host); // build-scoped SDK overlay caps this at four seconds
    if (!result) deadline_.tlsError = getLastSSLError();
    else deadline_.stage = "HTTP headers";
    deadline_.lastData = millis();
    if (static_cast<uint32_t>(millis() - deadline_.started) >= 8000) {
      deadline_.error = Error::TotalTimeout;
      stop();
      return 0;
    }
    return result;
  }

  uint8_t connected() override {
    return check() ? BearSSL::WiFiClientSecureCtx::connected() : 0;
  }
  int available() override {
    return check() ? BearSSL::WiFiClientSecureCtx::available() : 0;
  }
  int read() override {
    uint8_t byte;
    return read(&byte, 1) == 1 ? byte : -1;
  }
  int read(uint8_t* buffer, size_t size) override {
    if (!check()) return -1;
    int count = BearSSL::WiFiClientSecureCtx::read(buffer, size);
    if (count > 0 && !deadline_.received(millis(), count)) return -1;
    return count;
  }
  // Avoid the SDK stream-copy fast path bypassing the read/deadline checks.
  bool hasPeekBufferAPI() const override { return false; }

 private:
  Deadline& deadline_;
  void (*pump_)();
  const BearSSL::X509List* anchors_;
  uint32_t lastPump_ = 0;
  bool pumping_ = false;

  bool check() {
    if (!pumping_ && static_cast<uint32_t>(millis() - lastPump_) >= 10) {
      pumping_ = true;
      lastPump_ = millis();
      pump_();
      pumping_ = false;
    }
    if (deadline_.check(millis())) return true;
    BearSSL::WiFiClientSecureCtx::stop();
    setTimeout(0); // let inherited Stream operations exit immediately after cancellation
    return false;
  }
};

class Body: public Stream {
 public:
  Body(char* buffer, size_t capacity, Deadline& deadline)
      : buffer_(buffer, capacity), deadline_(deadline) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (!deadline_.check(millis())) return 0;
    if (!buffer_.append(data, size)) {
      deadline_.error = Error::TooLarge;
      return 0;
    }
    return size;
  }
  int available() override { return 0; }
  int availableForWrite() override {
    size_t remaining = buffer_.capacity - 1 - buffer_.used;
    if (!remaining) deadline_.error = Error::TooLarge;
    return static_cast<int>(remaining);
  }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t used() const { return buffer_.used; }

 private:
  Buffer buffer_;
  Deadline& deadline_;
};
}  // namespace Weather
