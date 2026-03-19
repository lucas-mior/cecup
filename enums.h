#undef DECL_ENUM_ELEMENT
#undef DECL_ENUM_ELEMENT_VAL
#undef DECL_ENUM_ELEMENT_STR
#undef DECL_ENUM_ELEMENT_VAL_STR
#undef BEGIN_ENUM
#undef END_ENUM

#ifndef GENERATE_ENUM_STRINGS
    #define BEGIN_ENUM(ENUM_NAME) enum ENUM_NAME {

    #define DECL_ENUM_ELEMENT(element)                       element,
    #define DECL_ENUM_ELEMENT_VAL(element, value)            element = value,
    #define DECL_ENUM_ELEMENT_STR(element, descr)            element,
    #define DECL_ENUM_ELEMENT_VAL_STR(element, value, descr) element = value,
    
    #define END_ENUM(ENUM_NAME) }; char* GetString##ENUM_NAME(int32 index);
#else
    #define BEGIN_ENUM(ENUM_NAME) char* GetString##ENUM_NAME(int32 index) { \
        switch (index) {
    #define DECL_ENUM_ELEMENT(element)                       case element: return #element;
    #define DECL_ENUM_ELEMENT_VAL(element, value)            case element: return #element;
    #define DECL_ENUM_ELEMENT_STR(element, descr)            case element: return descr;
    #define DECL_ENUM_ELEMENT_VAL_STR(element, value, descr) case element: return descr;

    #define END_ENUM(ENUM_NAME) default: return "Unknown value"; \
        } \
    }
#endif
