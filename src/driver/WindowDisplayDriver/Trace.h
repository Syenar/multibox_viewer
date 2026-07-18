/*++
Copyright (c) WindowDisplay project

Lightweight tracing stubs (WPP disabled for development builds).
--*/

#pragma once

#ifndef TRACE_LEVEL_INFORMATION
#define TRACE_LEVEL_CRITICAL    1
#define TRACE_LEVEL_ERROR       2
#define TRACE_LEVEL_WARNING     3
#define TRACE_LEVEL_INFORMATION 4
#define TRACE_LEVEL_VERBOSE     5
#endif

#define Trace(LEVEL, MSG, ...) ((void)0)
#define TraceEvents(LEVEL, FLAGS, MSG, ...) ((void)0)
