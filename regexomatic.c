#include <stdbool.h> // bool
#include <stdlib.h> // calloc()
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

typedef struct cmap_t_ cmap_t;

struct cmap_t_ {
  size_t num_keys,
         max_keys;
  uint32_t *keys;
  cmap_t **vals;
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

static bool
on_ruby_escape(
  const uint8_t byte,
  uint8_t * const buf,
  const size_t buf_len,
  size_t * const ret_len
) {
  // check buffer pointer
  if (!buf) {
    // null buffer, return failure
    return false;
  }

  switch (byte) {
  case '(':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
  case '.':
  case '*':
  case '+':
  case '?':
  case '|':
  case '\\':
  case '/':
    {
      // special character, escape it

      // check buffer length
      if (buf_len < 2) {
        // buffer too small, return failure
        return false;
      }

      // write escaped byte
      buf[0] = '\\';
      buf[1] = byte;

      if (ret_len) {
        // write length
        *ret_len = 2;
      }
    }

    break;
  default:
    {
      // not a special character, do not escape it

      // check buffer length
      if (buf_len < 1) {
        // buffer too small, return failure
        return false;
      }

      // write unescaped byte
      buf[0] = byte;

      if (ret_len) {
        // write length
        *ret_len = 1;
      }
    }
  }

  // never reached
  return true;
}

static bool
on_java_escape(
  const uint8_t byte,
  uint8_t * const buf,
  const size_t buf_len,
  size_t * const ret_len
) {
  // check buffer pointer
  if (!buf) {
    // null buffer, return failure
    return false;
  }

  switch (byte) {
  case '(':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
  case '.':
  case '*':
  case '+':
  case '?':
  case '|':
  case '\\':
  case '"':
    {
      // special character, escape it

      // check buffer length
      if (buf_len < 2) {
        // buffer too small, return failure
        return false;
      }

      // write escaped byte
      buf[0] = '\\';
      buf[1] = byte;

      if (ret_len) {
        // write length
        *ret_len = 2;
      }
    }

    break;
  default:
    {
      // not a special character, do not escape it

      // check buffer length
      if (buf_len < 1) {
        // buffer too small, return failure
        return false;
      }

      // write unescaped byte
      buf[0] = byte;

      if (ret_len) {
        // write length
        *ret_len = 1;
      }
    }
  }

  // return success
  return true;
}

static const struct {
  const uint8_t *head;
  const size_t head_len;

  const uint8_t *tail;
  const size_t tail_len;

  bool (*escape)(const uint8_t, uint8_t *, const size_t, size_t *);
} SYNTAXES[] = {{
  .head     = (uint8_t*) "\\A",
  .head_len = 2,

  .tail     = (uint8_t*) "\\Z",
  .tail_len = 2,

  .escape   = on_ruby_escape,
}, {
  .head     = (uint8_t*) "",
  .head_len = 0,

  .tail     = (uint8_t*) "",
  .tail_len = 0,

  .escape   = on_java_escape,
}};

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
  regexomatic_t ctx = {
    .on_error = on_error,
    .cb_data  = cb_data,
  };

  // allocate pool
  if (!pool_init(&(ctx.pool))) {
    // print error, fail
    ctx_die(&ctx, "pool_init() failed");
    return NULL;
  }

  // alloc root
  if (!pool_cmap_new(&(ctx.pool), &(ctx.root))) {
    // print error, fail
    ctx_die(&ctx, "pool_cmap_new() failed");
    return NULL;
  }

  // alloc result, check for error
  regexomatic_t * const r = calloc(1, sizeof(regexomatic_t));
  if (!r) {
    // print error, fail
    ctx_die(&ctx, "calloc():");
    return NULL;
  }

  // save context to result
  *r = ctx;

  // return result
  return r;
}

static void
cmap_fini(
  cmap_t * const cmap
) {
  UNUSED(cmap);
  return;

  // TODO
  for (size_t i = 0; i < cmap->num_keys; i++) {
    cmap_fini(cmap->vals[i]);
  }
  free(cmap->keys);
  free(cmap->vals);
}

void
regexomatic_fini(regexomatic_t * const ctx) {
  pool_fini(&(ctx->pool));
  cmap_fini(ctx->root);
  free(ctx);
}

static cmap_t *
cmap_find_key(
  cmap_t * const cmap,
  const uint32_t key
) {
  for (size_t i = 0; i < cmap->num_keys; i++) {
    if (cmap->keys[i] == key) {
      return cmap->vals[i];
    }
  }

  return NULL;
}

static cmap_t *
cmap_add_key(
  regexomatic_t * const ctx,
  cmap_t * const cmap,
  const uint32_t key
) {
  if (cmap->num_keys + 1 >= cmap->max_keys) {
    const size_t slab_size = (4096 / sizeof(cmap_t*)),
                 new_max_keys = cmap->max_keys + slab_size;

    // resize keys, check for error
    uint32_t *new_keys = realloc(cmap->keys, new_max_keys * sizeof(uint32_t));
    if (!new_keys) {
      ctx_die(ctx, "keys realloc():");
      return NULL;
    }

    // resize vals, check for error
    cmap_t **new_vals = realloc(cmap->vals, new_max_keys * sizeof(cmap_t*));
    if (!new_vals) {
      ctx_die(ctx, "vals realloc():");
      return NULL;
    }

    // update properties
    cmap->max_keys = new_max_keys;
    cmap->keys = new_keys;
    cmap->vals = new_vals;
  }

  // alloc cmap
  cmap_t *new_cmap = NULL;
  if (!pool_cmap_new(&(ctx->pool), &new_cmap)) {
    ctx_die(ctx, "pool_cmap_new() failed");
    return NULL;
  }

  // save key and val, increment count
  cmap->keys[cmap->num_keys] = key;
  cmap->vals[cmap->num_keys] = new_cmap;
  cmap->num_keys++;

  // return pointer to new cmap
  return new_cmap;
}

static cmap_t *
cmap_find_or_add_key(
  regexomatic_t * const ctx,
  cmap_t * const cmap,
  const uint32_t key
) {
  // find key
  cmap_t *r = cmap_find_key(cmap, key);

  if (!r) {
    // key does not exist, add it
    r = cmap_add_key(ctx, cmap, key);
  }

  // return result
  return r;
}

bool
regexomatic_add_word(
  regexomatic_t * const ctx,
  const uint8_t * const buf,
  const size_t len
) {
  cmap_t *curr = ctx->root;
  for (size_t i = 0; i < len; i++) {
    // get current byte
    // TODO: add support for codepoints
    const uint8_t byte = buf[i];

    cmap_t *next = cmap_find_or_add_key(ctx, curr, byte);
    if (!next) {
      ctx_die(ctx, "pool_cmap_new() failed");
      return false;
    }

    // walk to kid
    curr = next;
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
ctx_write_buf(
  regexomatic_t * const ctx,
  const uint8_t * const buf,
  const size_t len,
  const regexomatic_write_config_t * const write_cfg,
  void *cb_data
) {
  UNUSED(ctx);
  return !write_cfg->on_write || !len || write_cfg->on_write(buf, len, cb_data);
}

static bool
ctx_write_byte(
  regexomatic_t * const ctx,
  const uint8_t byte,
  const regexomatic_write_config_t * const write_cfg,
  void *cb_data
) {
  return ctx_write_buf(ctx, &byte, 1, write_cfg, cb_data);
}

static bool
ctx_write_escaped_byte(
  regexomatic_t * const ctx,
  const uint8_t byte,
  const regexomatic_write_config_t * const write_cfg,
  void *cb_data
) {
  uint8_t buf[2];
  size_t len;

  // escape byte
  if (!SYNTAXES[write_cfg->syntax].escape(byte, buf, sizeof(buf), &len)) {
    // write failure
    ctx_die(ctx, "escape byte failed");
    return false;
  }

  // write buffer
  return ctx_write_buf(ctx, buf, len, write_cfg, cb_data);
}

static bool
ctx_write_cmap(
  regexomatic_t * const ctx,
  const cmap_t * const cmap,
  const regexomatic_write_config_t * const write_cfg,
  void *cb_data
) {
  const size_t num_keys = cmap->num_keys;

  if (num_keys > 0) {
    if (num_keys > 1) {
      // write open paren
      if (!ctx_write_buf(ctx, (uint8_t*) "(?:", 3, write_cfg, cb_data)) {
        ctx_die(ctx, "ctx_write_buf() failed");
        return false;
      }
    }

    // walk children and write each one
    for (size_t i = 0, num_out = 0; i < num_keys; i++) {
      if (num_out > 0) {
        // write delimiter
        if (!ctx_write_byte(ctx, '|', write_cfg, cb_data)) {
          ctx_die(ctx, "ctx_write_byte() failed");
          return false;
        }
      }

      // write escaped byte
      // FIXME: handle codepoints instead of bytes
      if (!ctx_write_escaped_byte(ctx, cmap->keys[i], write_cfg, cb_data)) {
        ctx_die(ctx, "ctx_escaped_write_byte() failed");
        return false;
      }

      // write cmap
      if (!ctx_write_cmap(ctx, cmap->vals[i], write_cfg, cb_data)) {
        return false;
      }

      // increment output count
      num_out++;
    }

    if (num_keys > 1) {
      // write close paren
      if (!ctx_write_byte(ctx, ')', write_cfg, cb_data)) {
        ctx_die(ctx, "ctx_write_byte() failed");
        return false;
      }
    }
  }

  // return success
  return true;
}

static bool
ctx_write_head_anchor(
  regexomatic_t * const ctx,
  const regexomatic_write_config_t * const write_cfg,
  void *cb_data
) {
  // get head anchor
  const uint8_t * const buf = SYNTAXES[write_cfg->syntax].head;
  const size_t len = SYNTAXES[write_cfg->syntax].head_len;

  // write anchor
  return ctx_write_buf(ctx, buf, len, write_cfg, cb_data);
}

static bool
ctx_write_tail_anchor(
  regexomatic_t * const ctx,
  const regexomatic_write_config_t * const write_cfg,
  void *cb_data
) {
  // get tail anchor
  const uint8_t * const buf = SYNTAXES[write_cfg->syntax].tail;
  const size_t len = SYNTAXES[write_cfg->syntax].tail_len;

  // write anchor
  return ctx_write_buf(ctx, buf, len, write_cfg, cb_data);
}

bool
regexomatic_write(
  regexomatic_t * const ctx,
  const regexomatic_write_config_t * const write_cfg,
  void *cb_data
) {
  // check write config
  if (!write_cfg) {
    ctx_die(ctx, "null write config");
    return false;
  }

  // check syntax
  if (write_cfg->syntax >= REGEXOMATIC_SYNTAX_LAST) {
    ctx_die(ctx, "unknown syntax");
    return false;
  }

  // write head anchor
  if (!ctx_write_head_anchor(ctx, write_cfg, cb_data)) {
    return false;
  }

  // write root cmap
  if (!ctx_write_cmap(ctx, ctx->root, write_cfg, cb_data)) {
    return false;
  }

  // write tail anchor
  if (!ctx_write_tail_anchor(ctx, write_cfg, cb_data)) {
    return false;
  }

  // return success
  return true;
}
