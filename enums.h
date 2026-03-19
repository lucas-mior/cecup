#undef ENUM_ELEMENT
#undef ENUM_ELEMENT_HANDLER_1
#undef ENUM_ELEMENT_HANDLER_2
#undef ENUM_ELEMENT_HANDLER_3
#undef BEGIN_ENUM
#undef END_ENUM

#if !defined(CAT)
#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#endif

#define NUM_ARGS_(_1, _2, _3, _4, _5, _6, _7, _8, n, ...) n
#define NUM_ARGS(...) NUM_ARGS_(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, x)
#define SELECT_ON_NUM_ARGS(macro, ...) \
    CAT(macro, NUM_ARGS(__VA_ARGS__))(__VA_ARGS__)

#if !defined(Q)
#define Q(x) #x
#define QUOTE(x) Q(x)
#endif

#define ENUM_ELEMENT(...) SELECT_ON_NUM_ARGS(ENUM_ELEMENT_HANDLER_, __VA_ARGS__)

#if !defined(GENERATE_ENUM_STRINGS)
  #define BEGIN_ENUM(ENUM_NAME)           enum ENUM_NAME {

  #define ENUM_ELEMENT_HANDLER_1(e)       CAT(CAT(ENUM_NAME_LOCAL, _), e),
  #define ENUM_ELEMENT_HANDLER_2(e, v)    CAT(CAT(ENUM_NAME_LOCAL, _), e), 
  #define ENUM_ELEMENT_HANDLER_3(e, v, s) CAT(CAT(ENUM_NAME_LOCAL, _), e) = v,

  #define END_ENUM(ENUM_NAME)             CAT(CAT(ENUM_NAME_LOCAL, _), LAST) \
                                          }; \
                                          char *enum_string_##ENUM_NAME(int index);
#else
  #define BEGIN_ENUM(ENUM_NAME)           char *enum_string_##ENUM_NAME(int index) { \
                                          switch (index) {

  #define ENUM_ELEMENT_HANDLER_1(e)       case CAT(CAT(ENUM_NAME_LOCAL, _), e): \
                                              return QUOTE(ENUM_NAME_LOCAL) "_" #e;
  #define ENUM_ELEMENT_HANDLER_2(e, v)    case CAT(CAT(ENUM_NAME_LOCAL, _), e): \
                                              return _Generic((v), \
                                                  char*: (char*)(v), \
                                                  default: QUOTE(ENUM_NAME_LOCAL) "_" #e \
                                              );
  #define ENUM_ELEMENT_HANDLER_3(e, v, s) case CAT(CAT(ENUM_NAME_LOCAL, _), e): \
                                              return s;

  #define END_ENUM(ENUM_NAME)             default: \
                                              return "Unknown value"; \
                                          } \
  }
#endif
