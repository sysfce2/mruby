/*
** io.h - IO class
*/

#ifndef MRUBY_IO_H
#define MRUBY_IO_H

#include <mruby.h>

#ifdef MRB_NO_STDIO
# error IO and File conflicts 'MRB_NO_STDIO' in your build configuration
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(MRB_NO_IO_PREAD_PWRITE) || defined(MRB_WITHOUT_IO_PREAD_PWRITE)
# undef MRB_USE_IO_PREAD_PWRITE
#elif !defined(MRB_USE_IO_PREAD_PWRITE)
# if defined(__unix__) || defined(__MACH__) || defined(MRB_WITH_IO_PREAD_PWRITE)
#  define MRB_USE_IO_PREAD_PWRITE
# endif
#endif

#define MRB_IO_BUF_SIZE 4096

struct mrb_io_buf {
  short start;
  short len;
  char mem[MRB_IO_BUF_SIZE];
};

struct mrb_io {
  unsigned int readable:1,
               writable:1,
               eof:1,
               sync:1,
               is_socket:1,
               close_fd:1,
               close_fd2:1;
  int fd;   /* file descriptor, or -1 */
  int fd2;  /* file descriptor to write if it's different from fd, or -1 */
  int pid;  /* child's pid (for pipes)  */
  struct mrb_io_buf *buf;
};

#define MRB_O_RDONLY            0x0000
#define MRB_O_WRONLY            0x0001
#define MRB_O_RDWR              0x0002
#define MRB_O_ACCMODE           (MRB_O_RDONLY | MRB_O_WRONLY | MRB_O_RDWR)
#define MRB_O_NONBLOCK          0x0004
#define MRB_O_APPEND            0x0008
#define MRB_O_SYNC              0x0010
#define MRB_O_NOFOLLOW          0x0020
#define MRB_O_CREAT             0x0040
#define MRB_O_TRUNC             0x0080
#define MRB_O_EXCL              0x0100
#define MRB_O_NOCTTY            0x0200
#define MRB_O_DIRECT            0x0400
#define MRB_O_BINARY            0x0800
#define MRB_O_SHARE_DELETE      0x1000
#define MRB_O_TMPFILE           0x2000
#define MRB_O_NOATIME           0x4000
#define MRB_O_DSYNC             0x00008000
#define MRB_O_RSYNC             0x00010000

#define E_IO_ERROR              mrb_exc_get_id(mrb, MRB_ERROR_SYM(IOError))
#define E_EOF_ERROR             mrb_exc_get_id(mrb, MRB_ERROR_SYM(EOFError))

int mrb_io_fileno(mrb_state *mrb, mrb_value io);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif /* MRUBY_IO_H */
