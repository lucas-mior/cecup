#undef ENUM_ELEMENT
#undef ENUM_ELEMENT_VAL
#undef ENUM_ELEMENT_STR
#undef ENUM_ELEMENT_VAL_STR
#undef BEGIN_ENUM
#undef END_ENUM
#undef CONCAT_INTERNAL
#undef CONCAT

#define CONCAT_INTERNAL(a, b) a##_##b
#define CONCAT(a, b) CONCAT_INTERNAL(a, b)

#if !defined(Q)
#define Q(x) #x
#define QUOTE(x) Q(x)
#endif

#ifndef GENERATE_ENUM_STRINGS
    #define BEGIN_ENUM(ENUM_NAME) enum ENUM_NAME {

    #define ENUM_ELEMENT(element)                       CONCAT(ENUM_NAME_LOCAL, element),
    #define ENUM_ELEMENT_VAL(element, value)            CONCAT(ENUM_NAME_LOCAL, element) = value,
    #define ENUM_ELEMENT_STR(element, descr)            CONCAT(ENUM_NAME_LOCAL, element),
    #define ENUM_ELEMENT_VAL_STR(element, value, descr) CONCAT(ENUM_NAME_LOCAL, element) = value,

    #define END_ENUM(ENUM_NAME) }; char* GetString##ENUM_NAME(int32 index);
#else
    #define BEGIN_ENUM(ENUM_NAME) char* GetString##ENUM_NAME(int32 index) {\
        switch (index) {

    #define ENUM_ELEMENT(element)                       case CONCAT(ENUM_NAME_LOCAL, element): return QUOTE(ENUM_NAME_LOCAL) "_" #element;
    #define ENUM_ELEMENT_VAL(element, value)            case CONCAT(ENUM_NAME_LOCAL, element): return QUOTE(ENUM_NAME_LOCAL) "_" #element;
    #define ENUM_ELEMENT_STR(element, descr)            case CONCAT(ENUM_NAME_LOCAL, element): return descr;
    #define ENUM_ELEMENT_VAL_STR(element, value, descr) case CONCAT(ENUM_NAME_LOCAL, element): return descr;

    #define END_ENUM(ENUM_NAME) default: return "Unknown value"; \
        } \
    }
#endif
