#undef ENUM_ELEMENT
#undef ENUM_ELEMENT_VAL
#undef ENUM_ELEMENT_STR
#undef ENUM_ELEMENT_VAL_STR
#undef BEGIN_ENUM
#undef END_ENUM

#ifndef GENERATE_ENUM_STRINGS
    #define BEGIN_ENUM(ENUM_NAME) enum ENUM_NAME {

    #define ENUM_ELEMENT(element)                       element,
    #define ENUM_ELEMENT_VAL(element, value)            element = value,
    #define ENUM_ELEMENT_STR(element, descr)            element,
    #define ENUM_ELEMENT_VAL_STR(element, value, descr) element = value,
    
    #define END_ENUM(ENUM_NAME) }; char* GetString##ENUM_NAME(int32 index);
#else
    #define BEGIN_ENUM(ENUM_NAME) char* GetString##ENUM_NAME(int32 index) { \
        switch (index) {
    #define ENUM_ELEMENT(element)                       case element: return #element;
    #define ENUM_ELEMENT_VAL(element, value)            case element: return #element;
    #define ENUM_ELEMENT_STR(element, descr)            case element: return descr;
    #define ENUM_ELEMENT_VAL_STR(element, value, descr) case element: return descr;

    #define END_ENUM(ENUM_NAME) default: return "Unknown value"; \
        } \
    }
#endif
