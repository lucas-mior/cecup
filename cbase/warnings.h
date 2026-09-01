#if !defined(WARNINGS_H)
#define WARNINGS_H

#include "platform_detection.h"

#if !defined(DEBUGGING)
#define DEBUGGING 0
#endif

#if !defined(TESTING)
#define TESTING 0
#endif

#if CBASE_CRT_MSVC
  #pragma warning(push, 4)
#endif

#if CC_GCC
  // -Wall
  #pragma GCC diagnostic error "-Waddress"
  #pragma GCC diagnostic error "-Warray-bounds"
  #pragma GCC diagnostic error "-Wbool-compare"
  #pragma GCC diagnostic error "-Wbool-operation"
  #pragma GCC diagnostic error "-Wchar-subscripts"
  #pragma GCC diagnostic error "-Wcomment"
  #pragma GCC diagnostic error "-Wformat"
  #pragma GCC diagnostic error "-Wformat-overflow"
  #pragma GCC diagnostic error "-Wformat-truncation"
  #pragma GCC diagnostic error "-Wint-in-bool-context"
  #pragma GCC diagnostic error "-Wlogical-not-parentheses"
  #pragma GCC diagnostic error "-Wmain"
  #pragma GCC diagnostic error "-Wmaybe-uninitialized"
  #pragma GCC diagnostic error "-Wmemset-elt-size"
  #pragma GCC diagnostic error "-Wmemset-transposed-args"
  #pragma GCC diagnostic error "-Wmisleading-indentation"
  #pragma GCC diagnostic error "-Wmissing-attributes"
  #pragma GCC diagnostic error "-Wmissing-braces"
  #pragma GCC diagnostic error "-Wmultistatement-macros"
  #pragma GCC diagnostic error "-Wnonnull"
  #pragma GCC diagnostic error "-Wnonnull-compare"
  #pragma GCC diagnostic error "-Wparentheses"
  #pragma GCC diagnostic error "-Wpointer-sign"
  #pragma GCC diagnostic error "-Wrestrict"
  #pragma GCC diagnostic error "-Wreturn-type"
  #pragma GCC diagnostic error "-Wsequence-point"
  #pragma GCC diagnostic error "-Wsizeof-pointer-div"
  #pragma GCC diagnostic error "-Wsizeof-pointer-memaccess"
  #pragma GCC diagnostic error "-Wstrict-aliasing"
  #pragma GCC diagnostic error "-Wstrict-overflow"
  #pragma GCC diagnostic error "-Wstringop-overflow"
  #pragma GCC diagnostic error "-Wstringop-truncation"
  #pragma GCC diagnostic error "-Wswitch"
  #pragma GCC diagnostic error "-Wswitch-bool"
  #pragma GCC diagnostic error "-Wtautological-compare"
  #pragma GCC diagnostic error "-Wtrigraphs"
  #pragma GCC diagnostic error "-Wuninitialized"
  #pragma GCC diagnostic error "-Wunknown-pragmas"
  #pragma GCC diagnostic error "-Wunused-function"
  #pragma GCC diagnostic error "-Wunused-label"
  #pragma GCC diagnostic error "-Wunused-value"
  #pragma GCC diagnostic error "-Wunused-variable"
  #pragma GCC diagnostic error "-Wvolatile-register-var"

  // -Wextra
  #pragma GCC diagnostic error "-Wclobbered"
  #pragma GCC diagnostic error "-Wcast-function-type"
  #pragma GCC diagnostic error "-Wempty-body"
  #pragma GCC diagnostic error "-Wignored-qualifiers"
  #pragma GCC diagnostic error "-Wimplicit-fallthrough"
  #pragma GCC diagnostic error "-Wmissing-field-initializers"
  #pragma GCC diagnostic error "-Wmissing-parameter-type"
  #pragma GCC diagnostic error "-Wold-style-declaration"
  #pragma GCC diagnostic error "-Woverride-init"
  #pragma GCC diagnostic error "-Wsign-compare"
  #pragma GCC diagnostic error "-Wstring-compare"
  #pragma GCC diagnostic error "-Wtype-limits"
  #pragma GCC diagnostic error "-Wshift-negative-value"
  #pragma GCC diagnostic error "-Wunused-parameter"
  #pragma GCC diagnostic error "-Wunused-but-set-parameter"
#endif

#if CC_CLANG
  #pragma clang diagnostic error "-Weverything"

  #pragma clang diagnostic ignored "-Wassign-enum"
  #pragma clang diagnostic ignored "-Wc++-keyword"
  #pragma clang diagnostic ignored "-Wc++98-compat"
  #pragma clang diagnostic ignored "-Wcast-function-type-strict"
  #pragma clang diagnostic ignored "-Wcast-qual"
  #pragma clang diagnostic ignored "-Wchar-subscripts"
  #pragma clang diagnostic ignored "-Wconstant-logical-operand"
  #pragma clang diagnostic ignored "-Wcovered-switch-default"
  #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
  #pragma clang diagnostic ignored "-Wfloat-equal"
  #pragma clang diagnostic ignored "-Wformat-nonliteral"
  #pragma clang diagnostic ignored "-Wimplicit-int-enum-cast"
  #pragma clang diagnostic ignored "-Wimplicit-void-ptr-cast"
  #pragma clang diagnostic ignored "-Wnrvo"
  #pragma clang diagnostic ignored "-Wpadded"
  #pragma clang diagnostic ignored "-Wpre-c11-compat"
  #pragma clang diagnostic ignored "-Wtentative-definition-compat"
  #pragma clang diagnostic ignored "-Wunknown-warning-option"
  #pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
  #pragma clang diagnostic ignored "-Wunused-macros"
  #pragma clang diagnostic ignored "-Wused-but-marked-unused"
  #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#if DEBUGGING || TESTING
  #if CC_GCC || CC_CLANG
    #pragma GCC diagnostic ignored "-Wunused-function"
  #endif
#endif

#endif
