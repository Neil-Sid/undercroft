#pragma once
// Key codes. Browsers report the same numbers as Windows virtual keys.
#ifdef _WIN32
#include <windows.h>
#else
constexpr int VK_TAB = 9, VK_RETURN = 13, VK_ESCAPE = 27, VK_SPACE = 32, VK_LEFT = 37, VK_UP = 38, VK_RIGHT = 39, VK_DOWN = 40,
              VK_F1 = 112, VK_F11 = 122, VK_OEM_PERIOD = 190;
#endif
