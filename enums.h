#undef ENUM_ELEMENT
#undef ENUM_ELEMENT_VAL
#undef ENUM_ELEMENT_STR
#undef ENUM_ELEMENT_VAL_STR
#undef BEGIN_ENUM
#undef END_ENUM

#if !defined(Q)
#define Q(x) #x
#define QUOTE(x) Q(x)
#endif

#ifndef GENERATE_ENUM_STRINGS
  #define BEGIN_ENUM(ENUM_NAME) enum ENUM_NAME {

  #define ENUM_ELEMENT(element)                       ENUM_NAME_LOCAL##_##element,
  #define ENUM_ELEMENT_VAL(element, value)            ENUM_NAME_LOCAL##_##element = value,
  #define ENUM_ELEMENT_STR(element, descr)            ENUM_NAME_LOCAL##_##element,
  #define ENUM_ELEMENT_VAL_STR(element, value, descr) ENUM_NAME_LOCAL##_##element = value,

  #define END_ENUM(ENUM_NAME) }; char* GetString##ENUM_NAME(int32 index);
#else
  #define BEGIN_ENUM(ENUM_NAME) char* GetString##ENUM_NAME(int32 index) {\
      switch (index) {

  #define ENUM_ELEMENT(element)                       case ENUM_NAME_LOCAL##_##element: return QUOTE(ENUM_NAME) #element;
  #define ENUM_ELEMENT_VAL(element, value)            case ENUM_NAME_LOCAL##_##element: return QUOTE(ENUM_NAME) #element;
  #define ENUM_ELEMENT_STR(element, descr)            case ENUM_NAME_LOCAL##_##element: return descr;
  #define ENUM_ELEMENT_VAL_STR(element, value, descr) case ENUM_NAME_LOCAL##_##element: return descr;

  #define END_ENUM(ENUM_NAME) default: return "Unknown value"; } return "Unknown value"; }
#endif
