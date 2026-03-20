#undef XENUM
#undef XENUM_1
#undef XENUM_2
#undef XENUM_3
#undef BEGIN_ENUM
#undef END_ENUM

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ > 1)
#if !defined(ENUM_NAME_LOCAL)
#error "enums.h included but ENUM_NAME_LOCAL is not defined"
#endif
#endif

#if !defined(INCLUDE_COUNT)
#define INCLUDE_COUNT 0
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"

#if INCLUDE_COUNT == 0
#define INCLUDE_COUNT 1
#define GENERATE_ENUM_STRINGS 0
#else
#define INCLUDE_COUNT 0
#define GENERATE_ENUM_STRINGS 1
#endif

_Static_assert((INCLUDE_COUNT == 0) || (INCLUDE_COUNT == 1),
               "INCLUDE_COUNT must alternate between 0 and 1");

#pragma clang diagnostic pop

#if !defined(CAT)
  #define CAT_(a, b) a##b
  #define CAT(a, b) CAT_(a, b)
  #define CAT3_(a, b, c) a##b##c
  #define CAT3(a, b, c) CAT3_(a, b, c)
#endif

#define NUM_ARGS_(_1, _2, _3, _4, _5, _6, _7, _8, n, ...) n
#define NUM_ARGS(...) NUM_ARGS_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, x)
#define SELECT_ON_NUM_ARGS(macro, ...) \
    CAT(macro, NUM_ARGS(__VA_ARGS__))(__VA_ARGS__)

#if !defined(Q)
#define Q(x) #x
#define QUOTE(x) Q(x)
#endif

#define XENUM(...) SELECT_ON_NUM_ARGS(XENUM_, __VA_ARGS__)

#if GENERATE_ENUM_STRINGS == 0
  #define BEGIN_ENUM(ENUM_NAME) enum ENUM_NAME {

  #define XENUM_1(e)            CAT3(ENUM_NAME_LOCAL, _, e),
  #define XENUM_2(e, v)         CAT3(ENUM_NAME_LOCAL, _, e) = v, 
  #define XENUM_3(e, v, s)      CAT3(ENUM_NAME_LOCAL, _, e) = v,

  #define END_ENUM(ENUM_NAME)   CAT3(ENUM_NAME_LOCAL, _, LAST) \
                                }; \
                                char *enum_string_##ENUM_NAME(int index);
#else
  #define BEGIN_ENUM(ENUM_NAME) char *enum_string_##ENUM_NAME(int index) { \
                                switch (index) {

  #define XENUM_1(e)            case CAT3(ENUM_NAME_LOCAL, _, e): \
                                    return QUOTE(ENUM_NAME_LOCAL) "_" #e;
  #define XENUM_2(e, v)         case CAT3(ENUM_NAME_LOCAL, _, e): \
                                    return QUOTE(ENUM_NAME_LOCAL) "_" #e;
  #define XENUM_3(e, v, s)      case CAT3(ENUM_NAME_LOCAL, _, e): \
                                    return s;

  #define END_ENUM(ENUM_NAME)   default: \
                                    return "Unknown value"; \
                                } \
  }
#endif
