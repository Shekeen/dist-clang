#include <client/flag_set.h>

#include <base/assert.h>
#include <base/c_utils.h>
#include <base/string_utils.h>
#include <proto/remote.pb.h>

namespace {

const char* kEnvTempDir = "TMPDIR";
const char* kDefaultTempDir = "/tmp";

}  // namespace

namespace dist_clang {
namespace client {

// The clang output has following format:
//
// clang version 3.4 (...)
// Target: x86_64-unknown-linux-gnu
// Thread model: posix
//  "/path/to/clang" "-cc1" "-triple" ...
//  "/path/to/clang" "-cc1" "-triple" ...
//  ...
//
// Pay attention to the leading space in the fourth and further lines.

// static
bool FlagSet::ParseClangOutput(const String& output, String* version,
                               CommandList& commands) {
  List<String> lines;
  base::SplitString<'\n'>(output, lines);
  if (lines.size() < 4) {
    return false;
  }

  if (version) {
    version->assign(lines.front());
  }

  commands.clear();
  lines.pop_front();
  lines.pop_front();
  lines.pop_front();
  for (const auto& line : lines) {
    List<String> args;
    base::SplitString(line, " \"", args);
    if (!args.front().empty()) {
      // Something went wrong.
      return false;
    }

    // Escape from double-quotes.
    for (auto& arg : args) {
      if (!arg.empty()) {
        DCHECK(arg[arg.size() - 1] == '"');
        arg.erase(arg.size() - 1);
        base::Replace(arg, "\\\\", "\\");
        base::Replace(arg, "\\\"", "\"");
      }
    }
    commands.emplace_back(std::move(args));
  }

  return true;
}

// static
FlagSet::Action FlagSet::ProcessFlags(List<String> flags,
                                      proto::Flags* message) {
  Action action(UNKNOWN);
  String temp_dir = base::GetEnv(kEnvTempDir, kDefaultTempDir);

  for (auto it = flags.begin(); it != flags.end();) {
    if (it->empty()) {
      it = flags.erase(it);
    } else {
      ++it;
    }
  }

  // First non-empty argument is a path to executable.
  message->mutable_compiler()->set_path(flags.front());
  flags.pop_front();

  // Last argument is an input path.
  message->set_input(flags.back());
  flags.pop_back();

  for (auto it = flags.begin(); it != flags.end(); ++it) {
    String& flag = *it;

    if (flag == "-add-plugin") {
      message->add_other(flag);
      message->add_other(*(++it));
      message->mutable_compiler()->add_plugins()->set_name(*it);
    } else if (flag == "-dynamic-linker") {
      action = LINK;
      message->add_other(flag);
    } else if (flag == "-emit-obj") {
      action = COMPILE;
      message->set_action(flag);
    } else if (flag == "-E") {
      action = PREPROCESS;
      message->set_action(flag);
    } else if (flag == "-dependency-file") {
      message->set_deps_file(*(++it));
    } else if (flag == "-load") {
      ++it;
    } else if (flag == "-mrelax-all") {
      message->add_cc_only(flag);
    } else if (flag == "-o") {
      ++it;
      if (it->find(temp_dir) != String::npos) {
        action = UNKNOWN;
        break;
      } else {
        message->set_output(*it);
      }
    } else if (flag == "-x") {
      message->set_language(*(++it));
    }

    // Non-cacheable flags.
    // NOTICE: we should be very cautious here, since the local compilations are
    //         performed on a non-preprocessed file, but the result is saved
    //         using the hash from a preprocessed file.
    else if (flag == "-coverage-file") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-fdebug-compilation-dir") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-ferror-limit") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-include") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-internal-externc-isystem") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-internal-isystem") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-isysroot") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-main-file-name") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-MF") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-MMD") {
      message->add_non_cached(flag);
    } else if (flag == "-MT") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    } else if (flag == "-resource-dir") {
      message->add_non_cached(flag);
      message->add_non_cached(*(++it));
    }

    // By default all other flags are cacheable.
    else {
      message->add_other(flag);
    }
  }

  return action;
}

}  // namespace client
}  // namespace dist_clang