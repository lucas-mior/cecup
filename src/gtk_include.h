// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(GTK_INCLUDE_H)
#define GTK_INCLUDE_H

#include "platform_detection.h"
#if CC_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier" 
#endif

#include <gtk/gtk.h>
#undef CLAMP

#if CC_CLANG
#pragma clang diagnostic pop
#endif

#endif
