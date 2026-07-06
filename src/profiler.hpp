#pragma once

#include "config.hpp"

#if defined(TORRVIEW_HAVE_TRACY)
#include <tracy/Tracy.hpp>

#define TORRVIEW_PROFILE_FRAME() FrameMark
#define TORRVIEW_PROFILE_ZONE(name) ZoneScopedN(name)
#else
#define TORRVIEW_PROFILE_FRAME() ((void)0)
#define TORRVIEW_PROFILE_ZONE(name) ((void)0)
#endif
