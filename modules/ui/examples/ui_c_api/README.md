# NativeKit UI C ABI example

`showcase.cpp` is deliberately low-level. It demonstrates the complete public
UI ABI lifecycle in one translation unit:

1. create opaque resources;
2. encode native-layout command records;
3. submit a transaction to a retained display list;
4. render it into a NativeKit surface; and
5. destroy the renderer, list, and resources.

The flagship example lives beside this one in `../ui_showcase` and uses the
typed Haxe graphics API instead.
