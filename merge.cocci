@@
type T;
identifier v;
expression e;
@@

- T *v;
+ T *v = e;
... when != v
- v = e;
