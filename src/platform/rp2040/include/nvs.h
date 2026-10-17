#pragma once
// The handful of ESP-IDF NVS calls DeviceConfig::save() makes, over the same LittleFS namespace
// files as Preferences.h. One handle is open at a time; nvs_commit() publishes the namespace with
// a temporary file and a rename. A value wider than four bytes is rejected, never truncated.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "persistence/KvPreferences.h"

namespace awtrix::storage { KvPreferences& nvsHandle(); }

using esp_err_t = int;
using nvs_handle_t = uint32_t;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 0x1102;

inline esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* handle) {
  if (!awtrix::storage::nvsHandle().begin(ns, mode == NVS_READONLY)) return ESP_FAIL;
  *handle = 1;
  return ESP_OK;
}
namespace awtrix::storage::detail {
inline esp_err_t nvsNumber(const char* key, KvPreferences::Type type, uint32_t value, unsigned width) {
  return nvsHandle().putNumber(key, type, value, width) ? ESP_OK : ESP_FAIL;
}
}
inline esp_err_t nvs_set_u8(nvs_handle_t, const char* k, uint8_t v) {
  return awtrix::storage::detail::nvsNumber(k, awtrix::storage::KvPreferences::UChar, v, 1);
}
inline esp_err_t nvs_set_u16(nvs_handle_t, const char* k, uint16_t v) {
  return awtrix::storage::detail::nvsNumber(k, awtrix::storage::KvPreferences::UShort, v, 2);
}
inline esp_err_t nvs_set_i32(nvs_handle_t, const char* k, int32_t v) {
  return awtrix::storage::detail::nvsNumber(k, awtrix::storage::KvPreferences::Int,
                                            static_cast<uint32_t>(v), 4);
}
inline esp_err_t nvs_set_u32(nvs_handle_t, const char* k, uint32_t v) {
  return awtrix::storage::detail::nvsNumber(k, awtrix::storage::KvPreferences::UInt, v, 4);
}
inline esp_err_t nvs_set_str(nvs_handle_t, const char* k, const char* v) {
  return v && awtrix::storage::nvsHandle().put(k, awtrix::storage::KvPreferences::String, v) == std::strlen(v) &&
                 awtrix::storage::nvsHandle().isKey(k)
             ? ESP_OK : ESP_FAIL;
}
// Preferences stores a float as a 4-byte blob; that is the only blob AWTRIX writes.
inline esp_err_t nvs_set_blob(nvs_handle_t, const char* k, const void* v, std::size_t n) {
  if (!v || n != 4) return ESP_FAIL;
  uint32_t bits = 0;
  for (std::size_t i = 0; i < 4; ++i)
    bits |= uint32_t(static_cast<const uint8_t*>(v)[i]) << (8 * i);
  return awtrix::storage::detail::nvsNumber(k, awtrix::storage::KvPreferences::Float, bits, 4);
}
inline esp_err_t nvs_erase_key(nvs_handle_t, const char* k) {
  auto& store = awtrix::storage::nvsHandle();
  if (!store.isKey(k)) return ESP_ERR_NVS_NOT_FOUND;
  return store.remove(k) ? ESP_OK : ESP_FAIL;
}
inline esp_err_t nvs_commit(nvs_handle_t) {
  return awtrix::storage::nvsHandle().commit() ? ESP_OK : ESP_FAIL;
}
inline void nvs_close(nvs_handle_t) { awtrix::storage::nvsHandle().end(); }
