@@
expression x, cond, a, b;
position p;
@@

- x = cond ? a : b;
+ if (cond) {
+   x = a;
+ } else {
+   x = b;
+ }
