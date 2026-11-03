// libwrap.so: not needed by anything. Only loaded if the loader preloads it,
// in which case its whoami should win over libfoo's and libbar's.

const char *whoami(void) {
    return "libwrap";
}
