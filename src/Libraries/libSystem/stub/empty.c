/* Intentionally near-empty, see CMakeLists.txt in this directory. A single
 * defined symbol/text section, not truly zero content: cctools' ld64 seems to
 * apply its "dynamic main executables must link with libSystem.dylib" check
 * to dylib outputs with no code/sections at all, not just true MH_EXECUTEs. */
void __pd_libSystem_B_stub_placeholder(void) {}

