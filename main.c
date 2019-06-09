#include <stdbool.h> // bool
#include <stdio.h> // for fprintf()
#include <err.h> // err(), errx()
#include "regexomatic.h" // for regexomatic_init()

static void
on_error(
  const char * const err,
  void *cb_data
) {
  UNUSED(cb_data);
  fprintf(stderr, "ERROR: %s", err);
  exit(EXIT_FAILURE);
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
  fprintf(stderr, "Usage: %s words.txt\n", app);
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
  // check args
  if (argc < 2) {
    // print usage and exit
    print_usage_and_exit(argv[0]);
  }

  // init context
  ctx_t * const ctx = ctx_init(on_error, NULL);

  // read words
  if (!ctx_read(ctx, argv[1])) {
    return EXIT_FAILURE;
  }

  // read words
  // read_words(argv[1], on_word, ctx);

  if (!ctx_write(ctx, on_write, stdout)) {
    return EXIT_FAILURE;
  }

  // finalize context
  ctx_fini(ctx);

  // return success
  return EXIT_SUCCESS;
}
