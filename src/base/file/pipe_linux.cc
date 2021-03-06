#include <base/file/pipe.h>

#include <base/c_utils.h>

namespace dist_clang {
namespace base {

Pipe::Pipe(bool blocking) {
  int fds[2];

  int flags = O_CLOEXEC;
  if (!blocking) {
    flags |= O_NONBLOCK;
  }

  if (pipe2(fds, flags) == -1) {
    GetLastError(&error_);
    return;
  }

  fds_[0] = Data(fds[0]);
  fds_[1] = Data(fds[1]);
}

}  // namespace base
}  // namespace dist_clang
