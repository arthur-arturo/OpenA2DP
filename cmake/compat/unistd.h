#pragma once

// The pinned libldac source includes unistd.h unconditionally but does not use
// POSIX declarations. This target-private shim lets MSVC compile the source.
