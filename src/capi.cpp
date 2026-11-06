#include <dux-qt-base/dux_qt_base.h>

#include "build.hpp"
#include "launch/main.hpp"

extern "C" {

int dqb_main(int argc, char** argv) { return qs::launch::main(argc, argv); }

const char* dqb_version(void) { return QS_VERSION; }

const char* dqb_git_revision(void) { return GIT_REVISION; }
}
