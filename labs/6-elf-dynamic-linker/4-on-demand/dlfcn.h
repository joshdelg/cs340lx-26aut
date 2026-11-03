#ifndef __DLFCN_H__
#define __DLFCN_H__

// Runtime dynamic loading API
#define RTLD_LAZY    0x1
#define RTLD_NOW     0x2

#define RTLD_DEFAULT ((void *)0)    // dlsym: search the global lookup order
#define RTLD_NEXT    ((void *)-1)   // dlsym: next definition after the caller's object

void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);
char *dlerror(void);

#endif
