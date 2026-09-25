#pragma once

// Private JSON support. Glaze stays out of installed headers.

#include <glaze/core/feature_test.hpp>
#include <glaze/glaze.hpp>

// Another project may have declared an older Glaze through FetchContent first.
#if !defined(glaze_v8_3_0_tuple)
#error "kalshi-cpp requires Glaze 8.3 or newer"
#endif
