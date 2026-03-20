#undef XENUM
#undef XENUM_1
#undef XENUM_2
#undef XENUM_3
#undef BEGIN_ENUM
#undef END_ENUM

#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ > 1)
#if !defined(ENUM_PREFIX_)
#error "enums.h included but ENUM_PREFIX_ is not defined"
#endif
#endif

// use 1234 and 4321 to decrease the chance of someone else using this macro
// (even though the compiler should emit a warning anyway)
#if !defined(ENUM_GENERATE_STRINGS)
#define ENUM_GENERATE_STRINGS 4321
#endif

#if ENUM_GENERATE_STRINGS == 1234
#undef ENUM_GENERATE_STRINGS
#define ENUM_GENERATE_STRINGS 4321
#elif ENUM_GENERATE_STRINGS == 4321
#undef ENUM_GENERATE_STRINGS
#define ENUM_GENERATE_STRINGS 1234
#else
#error "ENUM_GENERATE_STRINGS must alternate between 1234 and 4321"
#endif

#if !defined(CAT) || !defined(CAT3)
  #define CAT_(a, b)     a##b
  #define CAT3_(a, b, c) a##b##c
  #define CAT(a, b)      CAT_(a, b)
  #define CAT3(a, b, c)  CAT3_(a, b, c)
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

#if ENUM_GENERATE_STRINGS == 1234
  #define BEGIN_ENUM(EnumName) enum EnumName {

  #define XENUM_1(e)             CAT(ENUM_PREFIX_, e),
  #define XENUM_2(e, v)          CAT(ENUM_PREFIX_, e) = v, 
  #define XENUM_3(e, v, s)       CAT(ENUM_PREFIX_, e) = v,

  #define END_ENUM(EnumName)     CAT(ENUM_PREFIX_, LAST) \
                               }; \
                               char *CAT(ENUM_PREFIX_, str)(enum EnumName v);
#else
  #define BEGIN_ENUM(EnumName) char *CAT(ENUM_PREFIX_, str)(enum EnumName v) { \
                                   switch (v) {

  #define XENUM_1(e)               case CAT(ENUM_PREFIX_, e): \
                                       return QUOTE(ENUM_PREFIX_) #e;
  #define XENUM_2(e, v)            case CAT(ENUM_PREFIX_, e): \
                                       return QUOTE(ENUM_PREFIX_) #e;
  #define XENUM_3(e, v, s)         case CAT(ENUM_PREFIX_, e): \
                                       return s;

  #define END_ENUM(EnumName)       default: \
                                       return "Unknown value"; \
                                 } \
                               }
#endif
