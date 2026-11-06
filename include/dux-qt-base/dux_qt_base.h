#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the quickshell command line (same argv contract as the qs binary,
 * argv[0] included) and returns its exit code. Call once per process from
 * the main thread. */
int dqb_main(int argc, char** argv);

const char* dqb_version(void);
const char* dqb_git_revision(void);

#ifdef __cplusplus
}
#endif
