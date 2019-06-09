#include <stdbool.h> // bool
#include <stdlib.h> // malloc()
#include <stdio.h> // fopen()
#include <errno.h> // errno, perror()
#include <err.h> // err(), errx()
#include <ctype.h> // isspace()
#include <string.h> // strlen()
#include <stdarg.h> // va_start
#include "regexomatic.h"

#define UNUSED(a) ((void) (a))

typedef struct {
  char *ptr;
  size_t len;
} word_t;

static void
word_strip_head(
  word_t * const word
) {
  if (word->len > 0) {
    // get pointer and length
    char *ptr = word->ptr;
    size_t len = word->len;

    // skip leading spaces
    while (((size_t) (ptr - word->ptr) < len) && isspace(*ptr)) {
      // increment pointer
      ptr++;
    }

    // decriment length and update ptr (order matters!)
    word->len -= ptr - word->ptr;
    word->ptr = ptr;
  }
}

static void
word_strip_tail(
  word_t * const word
) {
  if (word->len > 0) {
    // get length
    size_t len = word->len;

    // skip trailing spaces
    while ((len > 0) && isspace(*(word->ptr + len - 1))) {
      // decriment length
      len--;
    }

    // update length
    word->len = len;
  }
}

static bool
word_strip(
  word_t * const word
) {
  if (!word || !word->ptr) {
    // return failure
    return false;
  }

  // strip leading spaces
  word_strip_head(word);

  // strip trailing spaces
  word_strip_tail(word);

  // return success
  return true;
}

typedef struct cmap_t_ cmap_t;

struct cmap_t_ {
  size_t num_kids;
  cmap_t *kids[256];
};

typedef struct pool_slab_t_ pool_slab_t;

#define POOL_SLAB_MAX_CMAPS (4096 / sizeof(cmap_t))
struct pool_slab_t_ {
  pool_slab_t *next;
  cmap_t cmaps[POOL_SLAB_MAX_CMAPS];
  size_t num_cmaps;
};

typedef struct {
  pool_slab_t *root;
} pool_t;

struct regexomatic_t_ {
  pool_t pool;
  cmap_t *root;
  void (*on_error)(const char *, void *);
  void *cb_data;
};

static void
ctx_die(
  const regexomatic_t * const ctx,
  const char * const fmt,
  ...
) {
  char buf[1024];

  if (!ctx->on_error || !fmt) {
    return;
  }

  {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
  }

  // get format length and output buffer length
  const size_t fmt_len = strlen(fmt),
               buf_len = strlen(buf);

  if (fmt_len > 1 && fmt[fmt_len - 2] == ':') {
    // append error (FIXME: should check error here)
    snprintf(buf + buf_len, sizeof(buf) - buf_len, " %s", strerror(errno));
  }

  // call error handler
  ctx->on_error(buf, ctx->cb_data);
}

static bool
pool_grow(pool_t * const pool) {
  pool_slab_t * const slab = calloc(1, sizeof(pool_slab_t));
  if (!slab) {
    // return failure
    return false;
  }

  // update pool
  slab->next = pool->root;
  pool->root = slab;

  // return success
  return true;
}

static bool
pool_init(pool_t * const pool) {
  pool->root = NULL;
  return pool_grow(pool);
}

static void
pool_fini(pool_t * const pool) {
  for (pool_slab_t *slab = pool->root, *next; slab; slab = next) {
    next = slab->next;
    free(slab);
  }
}

#define pool_needs_grow(pool) ( \
  (pool)->root->num_cmaps == POOL_SLAB_MAX_CMAPS \
)

static bool
pool_cmap_new(
  pool_t * const pool, 
  cmap_t ** const cmap
) {
  if (!pool) {
    // return false
    return false;
  }

  // check for resize
  if (pool_needs_grow(pool) && !pool_grow(pool)) {
    // return failure
    return false;
  }

  if (cmap) {
    // save pointer to cmap
    *cmap = pool->root->cmaps + pool->root->num_cmaps;
  }

  // increment count
  pool->root->num_cmaps++;

  // return success
  return true;
}

regexomatic_t *
regexomatic_init(
  void (*on_error)(const char *, void *),
  void *cb_data
) {
  // alloc context, check for error
  regexomatic_t *ctx = calloc(1, sizeof(regexomatic_t));
  if (!ctx) {
    if (on_error) {
      // call error handler
      char buf[1024];
      snprintf(buf, sizeof(buf), "malloc(): %s", strerror(errno));
      on_error(buf, cb_data);
    }

    // return failure
    return NULL;
  }

  // save error handler and callback data
  ctx->on_error = on_error;
  ctx->cb_data = cb_data;

  // allocate pool
  if (!pool_init(&(ctx->pool))) {
    ctx_die(ctx, "pool_init() failed");
    return NULL;
  }

  // alloc root
  if (!pool_cmap_new(&(ctx->pool), &(ctx->root))) {
    // print error, exit with failure
    ctx_die(ctx, "pool_cmap_new() failed");
    return NULL;
  }
  memset(ctx->root->kids, 0, 256 * sizeof(cmap_t*));
  ctx->root->num_kids = 0;

  // return context
  return ctx;
}

void
regexomatic_fini(regexomatic_t * const ctx) {
  pool_fini(&(ctx->pool));
  free(ctx);
}

bool
regexomatic_add_word(
  regexomatic_t * const ctx,
  const uint8_t * const buf,
  const size_t len
) {
  cmap_t *curr = ctx->root;
  for (size_t i = 0; i < len; i++) {
    const uint8_t byte = buf[i];

    if (!curr->kids[byte]) {
      // alloc new cmap
      if (!pool_cmap_new(&(ctx->pool), &(curr->kids[byte]))) {
        // log error, return failure
        ctx_die(ctx, "pool_cmap_new() failed");
        return false;
      }

      curr->kids[byte]->num_kids = 0;
      memset(curr->kids[byte]->kids, 0, 256 * sizeof(cmap_t*));

      curr->num_kids++;
    }

    curr = curr->kids[byte];
  }

  // return success
  return true;
}

bool
regexomatic_read(
  regexomatic_t * const ctx,
  const char * const path
) {
  char buf[1024];

  // open input file
  FILE *fh = fopen(path, "rb");
  if (!fh) {
    // print error, exit with failure
    ctx_die(ctx, "fopen(\"%s\"):", path);
    return false;
  }

  // read words
  while (fgets(buf, sizeof(buf), fh) && !feof(fh)) {
    // init word
    word_t word = {
      .ptr = buf,
      .len = strlen(buf),
    };

    // strip whitespace
    if (!word_strip(&word)) {
      // print error, exit with failure
      ctx_die(ctx, "word_strip() failed");
      return false;
    }

    // invoke callback
    if (word.len > 0 && !regexomatic_add_word(ctx, (uint8_t*) word.ptr, word.len)) {
      // print error, exit with failure
      ctx_die(ctx, "ctx_add_word() failed");
      return false;
    }
  }

  // close input file
  if (fclose(fh)) {
    // print warning, continue
    // warn("fclose(\"%s\")", path);
  }

  // return success
  return true;
}

static bool
ctx_write_byte(
  regexomatic_t * const ctx,
  const uint8_t byte,
  bool (*on_write)(const uint8_t *, const size_t, void *),
  void *cb_data
) {
  UNUSED(ctx);
  return !on_write || on_write(&byte, 1, cb_data);
}

static bool
ctx_write_buf(
  regexomatic_t * const ctx,
  const uint8_t * const buf,
  const size_t len,
  bool (*on_write)(const uint8_t *, const size_t, void *),
  void *cb_data
) {
  UNUSED(ctx);
  return !on_write || on_write(buf, len, cb_data);
}

static bool
ctx_write_cmap(
  regexomatic_t * const ctx,
  const cmap_t * const cmap,
  bool (*on_write)(const uint8_t *, const size_t, void *),
  void *cb_data
) {
  const size_t num_kids = cmap->num_kids;

  if (num_kids > 0) {
    if (num_kids > 1) {
      // write open paren
      if (!ctx_write_buf(ctx, (uint8_t*) "(?:", 3, on_write, cb_data)) {
        ctx_die(ctx, "ctx_write_buf() failed");
        return false;
      }
    }

    // walk children and write each one
    for (size_t i = 0, num_out = 0; (num_out < num_kids) && (i < 256); i++) {
      if (!cmap->kids[i]) {
        continue;
      }

      if (num_out > 0) {
        // write delimiter
        if (!ctx_write_byte(ctx, '|', on_write, cb_data)) {
          ctx_die(ctx, "ctx_write_byte() failed");
          return false;
        }
      }

      // write byte (TODO: escape me)
      if (!ctx_write_byte(ctx, i, on_write, cb_data)) {
        ctx_die(ctx, "ctx_write_byte() failed");
        return false;
      }

      // write cmap
      if (!ctx_write_cmap(ctx, cmap->kids[i], on_write, cb_data)) {
        return false;
      }

      // increment output count
      num_out++;
    }

    if (num_kids > 1) {
      // write close paren
      if (!ctx_write_byte(ctx, ')', on_write, cb_data)) {
        ctx_die(ctx, "ctx_write_byte() failed");
        return false;
      }
    }
  }

  // return success
  return true;
}


bool
regexomatic_write(
  regexomatic_t * const ctx,
  bool (*on_write)(const uint8_t *, const size_t, void *),
  void *cb_data
) {
  // write head anchor
  if (!ctx_write_buf(ctx, (uint8_t*) "\\A", 2, on_write, cb_data)) {
    return false;
  }

  // write root cmap
  if (!ctx_write_cmap(ctx, ctx->root, on_write, cb_data)) {
    return false;
  }

  // write tail anchor
  if (!ctx_write_buf(ctx, (uint8_t*) "\\Z", 2, on_write, cb_data)) {
    return false;
  }

  // return success
  return true;
}
