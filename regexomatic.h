#ifndef REGEXOMATIC_H
#define REGEXOMATIC_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h> // uint8_t

/**
 * output syntax for regexomatic_write()
 */
typedef enum {
  REGEXOMATIC_SYNTAX_RUBY,
  REGEXOMATIC_SYNTAX_JAVA,
  REGEXOMATIC_SYNTAX_LAST,
} regexomatic_syntax_t;

/**
 * opaque regexomatic_t definition.
 */
typedef struct regexomatic_t_ regexomatic_t;

/**
 * create a new regexomatic_t instance.
 *
 * returns NULL on error or a newly allocated instance on success.
 */
regexomatic_t *regexomatic_init(
  void (*on_error)(const char *, void *),
  void *cb_data
);

/**
 * finalize and free a regexomatic_t instance.
 */
void regexomatic_fini(regexomatic_t *);

/**
 * add a single word.
 *
 * returns false on error.
 */
_Bool regexomatic_add_word(
  regexomatic_t *,
  const uint8_t *,
  const size_t
);

/**
 * read a set of words from a file (one word per line).
 *
 * returns false on error.
 */
_Bool regexomatic_read(
  regexomatic_t *,
  const char *
);

/**
 * configuration for regexomatic_write()
 */
typedef struct {
  regexomatic_syntax_t syntax;
  bool (*on_write)(const uint8_t *, const size_t, void *);
} regexomatic_write_config_t;

/**
 * serialize regexomatic instance as a regular expression.
 *
 * return false on error.
 */
_Bool regexomatic_write(
  regexomatic_t *,
  const regexomatic_write_config_t *,
  void *
);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* REGEXOMATIC_H */
