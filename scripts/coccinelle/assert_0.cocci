// docker run --rm -v `pwd`:`pwd` -w `pwd` philmd/coccinelle --macro-file scripts/cocci-macro-file.h --sp-file scripts/coccinelle/assert_0.cocci --keep-comments
@@
expression retval;
@@
-   assert(0);
+   g_assert_not_reached();
(
-   return retval;
|
-   return;
)
