#ifndef SHAKTI_IOS_H
#define SHAKTI_IOS_H

/* Run a .ie file through shakti_lang_main and capture stdout/stderr.
 * out and err may be NULL. Caps include the trailing NUL. Returns the
 * interpreter exit code, or 2 if the host could not start the run. */
int shakti_ios_run_file(const char *path, char *out, int out_cap, char *err, int err_cap);

#endif
