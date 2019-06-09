#include <stdbool.h> // bool
#include <stdlib.h> // exit();
#include <stdio.h> // for fwrite(), fputs()
#include <err.h> // err(), errx()
#include "regexomatic.h" // for regexomatic_init()

#define UNUSED(a) ((void) (a))

static void
on_error(
  const char * const err,
  void *cb_data
) {
  UNUSED(cb_data);
  errx(EXIT_FAILURE, "ERROR: %s", err);
}

static bool on_write(
  const uint8_t * const buf,
  const size_t len,
  void *cb_data
) {
  FILE *fh = cb_data;
  return fwrite(buf, len, 1, fh) != 0;
}

static void
print_usage_and_exit(
  const char * const app
) {
  errx(EXIT_FAILURE, "Usage: %s words.txt", app);
}

int main(int argc, char *argv[]) {
  // check args
  if (argc < 2) {
    // print usage and exit
    print_usage_and_exit(argv[0]);
  }

  // init context
  regexomatic_t * const ctx = regexomatic_init(on_error, NULL);

  // read words
  if (!regexomatic_read(ctx, argv[1])) {
    return EXIT_FAILURE;
  }

  // write to stdout
  if (!regexomatic_write(ctx, on_write, stdout)) {
    return EXIT_FAILURE;
  }

  // write trailing newline
  fputs("\n", stdout);

  // finalize context
  regexomatic_fini(ctx);

  // return success
  return EXIT_SUCCESS;
}
