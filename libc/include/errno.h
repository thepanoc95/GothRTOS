#ifndef GOTH_LIBC_ERRNO_H
#define GOTH_LIBC_ERRNO_H

#define EPERM 1
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define EINVAL 22
#define EMFILE 24
#define ESPIPE 29
#define EDOM 33
#define ERANGE 34
#define ENOSYS 38
#define ENAMETOOLONG 36
#define ENOTEMPTY 39
#define ELOOP 40
#define EOVERFLOW 75
#define EISDIR 21
#define ENOTDIR 20

extern int errno;

#endif
