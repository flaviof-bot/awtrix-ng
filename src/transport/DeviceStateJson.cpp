#include "transport/DeviceStateJson.h"

#include <Arduino.h>
#if defined(AWTRIX_PLATFORM_RP2040)
#include <WiFi.h>
#include <malloc.h>
#include <unistd.h>

#include <algorithm>
#else
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <esp_system.h>
#endif

#include <cmath>

#include "AppConfig.h"
#include "core/CoreEngine.h"
#include "core/SocProfile.h"
#include "core/render/Color.h"
#include "hal/IBoard.h"
#if defined(AWTRIX_PLATFORM_RP2040)
#include "platform/rp2040/ResetReason.h"
#include "platform/rp2040/WifiCompat.h"
#else
#include "system/HeapCaps.h"
#endif
#include "system/MonotonicClock.h"
#include "transport/http/UpdateImage.h"

namespace awtrix {

namespace {
const char* kBoardType = "awtrixng";

#if !defined(AWTRIX_PLATFORM_RP2040)
const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_EXT:      return "external";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "interruptWatchdog";
    case ESP_RST_TASK_WDT: return "taskWatchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deepSleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "sdio";
    default:               return "unknown";
  }
}
#endif

}

std::string buildDeviceStateJson(CoreEngine& engine, IBoard& board, const std::string& uid,
                                 bool scriptingRunning) {
  ISensorBus& sensors = board.sensors();
  DeviceFacts facts;
  facts.boardType = kBoardType;
  facts.soc = pins::activeProfile().id;
  facts.updateImage = kUpdateImageName;
#if defined(AWTRIX_PLATFORM_RP2040)
  facts.ipAddress = platform::pico::addressText(WiFi.localIP());
#else
  facts.ipAddress = std::string(WiFi.localIP().toString().c_str());
#endif
  WiFi.macAddress(facts.macAddress.data());
  facts.hasMacAddress = true;
  const char* hn = WiFi.getHostname();
  facts.hostname = hn ? hn : "";
  facts.wifiRssi = WiFi.RSSI();
  facts.uptimeSeconds = static_cast<long>(monotonicMs() / 1000);
  // Internal RAM only. PSRAM is reported separately, because it is the internal heap that runs out
  // first and it is the number worth watching.
#if defined(AWTRIX_PLATFORM_RP2040)
  facts.freeHeapBytes = static_cast<uint64_t>(rp2040.getFreeHeap());
  // arduino-pico has no low-water API; this low water is sampled by device requests.
  static uint64_t sampledMin = facts.freeHeapBytes;
  sampledMin = std::min(sampledMin, facts.freeHeapBytes);
  facts.minFreeHeapBytes = sampledMin;
  // newlib cannot walk its free list, so this is the block at the top of the heap: the free chunk
  // malloc keeps there plus what sbrk has not handed out yet, up to the stack. Holes freed further
  // down are not counted, so the true largest block is at least this, less malloc's page rounding.
  const ptrdiff_t unclaimed = &__StackLimit - static_cast<char*>(sbrk(0));
  facts.largestFreeBlockBytes = static_cast<uint64_t>(mallinfo().keepcost) +
                                static_cast<uint64_t>(std::max<ptrdiff_t>(unclaimed, 0));
  facts.hasLargestFreeBlock = true;
  facts.resetReason = platform::resetReasonName();
#else
  facts.freeHeapBytes = heap_caps_get_free_size(kGuardHeapCaps);
  facts.minFreeHeapBytes = heap_caps_get_minimum_free_size(kGuardHeapCaps);
  facts.largestFreeBlockBytes = heap_caps_get_largest_free_block(kGuardHeapCaps);
  facts.hasLargestFreeBlock = true;
  facts.psramTotalBytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  facts.psramFreeBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  facts.resetReason = resetReasonName();
#endif
  facts.hasBattery = board.hasBattery();
  facts.hasBatteryDivider = true;
  facts.hasLightSensor = board.hasLightSensor();
  facts.hasTemperature = sensors.hasSensor();
  facts.hasHumidity = sensors.hasHumidity();
  facts.hasPressure = sensors.hasPressure();
  facts.scriptingRunning = scriptingRunning;
  return buildDeviceJson(engine, uid, facts);
}

}
