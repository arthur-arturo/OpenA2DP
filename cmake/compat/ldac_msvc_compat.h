#pragma once

// The pinned AOSP libldac source uses __attribute__((unused)) only as a
// compiler annotation. MSVC does not support GCC attributes.
#ifndef __attribute__
#define __attribute__(attribute)
#endif
