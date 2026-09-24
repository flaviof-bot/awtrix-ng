#pragma once
namespace awtrix::platform {
// Must run after the display claims PIO/DMA and before any WiFi call.
void beginRadio();
}
