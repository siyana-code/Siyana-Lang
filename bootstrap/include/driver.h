/* driver.h -- the run pipeline: read, lex, parse, execute. */

#ifndef SIYANA_DRIVER_H
#define SIYANA_DRIVER_H

/* Compiles and executes a Siyana Lang source file.
 * Returns 0 on success, non-zero on any lex, parse, or runtime error. */
int driver_run(const char *path);

#endif /* SIYANA_DRIVER_H */
