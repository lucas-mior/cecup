// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(GTK_INCLUDE_H)
#define GTK_INCLUDE_H

#include "cbase.h"

#if CC_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier" 
#endif

#if defined(CLAMP)
#pragma push_macro("CLAMP")
#define GTK_INCLUDE_RESTORE_CLAMP 1
#undef CLAMP
#endif
#include <gtk/gtk.h>
#if defined(CLAMP)
#undef CLAMP
#endif
#if defined(GTK_INCLUDE_RESTORE_CLAMP)
#pragma pop_macro("CLAMP")
#undef GTK_INCLUDE_RESTORE_CLAMP
#endif

#if CC_CLANG
#pragma clang diagnostic pop
#endif

#endif
