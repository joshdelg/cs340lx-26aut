// libdl.so: link-time stub -- we need this because our loader isn't a shared object yet :(
#include "dlfcn.h"

void *dlopen(const char *filename, int flags) { return 0; }
void *dlsym(void *handle, const char *symbol) { return 0; }
int dlclose(void *handle) { return -1; }
char *dlerror(void) { return "libdl.so stub: loader did not provide dlfcn"; }
