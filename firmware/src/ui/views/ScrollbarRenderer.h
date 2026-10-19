#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"
#include <stdint.h>

// Stateless scrollbar renderer. Call after painting the content area.
// The caller owns navigation, clipping and content width.
namespace ScrollbarRenderer {
inline void draw(int x, int y, int width, int height,
                 uint32_t total, uint32_t visible, uint32_t offset,
                 int minThumb, uint16_t trackColor,
                 uint16_t thumbColor) {
  if (total <= visible || visible == 0 || height <= 0 || width <= 0) return;
  int thumbH = (int)((uint64_t)height * visible / total);
  if (thumbH < minThumb) thumbH = minThumb;
  if (thumbH > height) thumbH = height;
  const uint32_t maxOffset = total - visible;
  if (offset > maxOffset) offset = maxOffset;
  const int thumbY = y + (int)((uint64_t)(height - thumbH) * offset / maxOffset);
  auto& lcd = Uni.Lcd;
  lcd.fillRect(x, y, width, height, trackColor);
  lcd.fillRect(x, thumbY, width, thumbH,
               thumbColor);
}
} // namespace ScrollbarRenderer
