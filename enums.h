#undef ENUM_ELEMENT
#undef ENUM_ELEMENT_VAL
#undef ENUM_ELEMENT_STR
#undef ENUM_ELEMENT_VAL_STR
#undef BEGIN_ENUM
#undef END_ENUM

#ifndef GENERATE_ENUM_STRINGS
  #define BEGIN_ENUM(ENUM_NAME) enum ENUM_NAME {

  #define ENUM_ELEMENT(ENUM_NAME, element)                       ENUM_NAME##_##element,
  #define ENUM_ELEMENT_VAL(ENUM_NAME, element, value)            ENUM_NAME##_##element = value,
  #define ENUM_ELEMENT_STR(ENUM_NAME, element, descr)            ENUM_NAME##_##element,
  #define ENUM_ELEMENT_VAL_STR(ENUM_NAME, element, value, descr) ENUM_NAME##_##element = value,

  #define END_ENUM(ENUM_NAME) }; char* GetString##ENUM_NAME(int32 index);
#else
  #define BEGIN_ENUM(ENUM_NAME) char* GetString##ENUM_NAME(int32 index) {\
      switch (index) {

  #define ENUM_ELEMENT(ENUM_NAME, element)                       case ENUM_NAME##_##element: return #element;
  #define ENUM_ELEMENT_VAL(ENUM_NAME, element, value)            case ENUM_NAME##_##element: return #element;
  #define ENUM_ELEMENT_STR(ENUM_NAME, element, descr)            case ENUM_NAME##_##element: return descr;
  #define ENUM_ELEMENT_VAL_STR(ENUM_NAME, element, value, descr) case ENUM_NAME##_##element: return descr;

  #define END_ENUM(ENUM_NAME) default: return "Unknown value"; } return "Unknown value"; }
#endif
