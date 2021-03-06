#pragma once

#include <client/command.hh>

#include <llvm/Option/ArgList.h>

namespace dist_clang {
namespace client {

class CleanCommand : public Command {
 public:
  CleanCommand(const llvm::opt::ArgStringList& temp_files)
      : temp_files_(temp_files) {}

  base::ProcessPtr CreateProcess(Immutable current_dir,
                                 ui32 user_id) const override;
  String GetExecutable() const override { return rm_path; }
  String RenderAllArgs() const override;

 private:
  static constexpr const char* rm_path = "/bin/rm";
  const llvm::opt::ArgStringList& temp_files_;
};

}  // namespace client
}  // namespace dist_clang
