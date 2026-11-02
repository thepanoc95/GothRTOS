#ifndef GOTH_LIBC_SETJMP_H
#define GOTH_LIBC_SETJMP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GothLibC has no assembly setjmp/longjmp layer.  GCC provides the same
 * non-local jump primitives used by its own exception machinery, so Lua's
 * protected-call implementation can use them directly.
 */
typedef void *jmp_buf[5];

#define setjmp(environment) __builtin_setjmp(environment)
#define longjmp(environment, value) __builtin_longjmp(environment, 1)

#ifdef __cplusplus
}
#endif

#endif
