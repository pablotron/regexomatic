#ifndef REGEXOMATIC_H
#define REGEXOMATIC_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h> /* uint8_t */

typedef struct regexomatic_t_ regexomatic_t;

regexomatic_t *
regexomatic_init(
  void (*on_error)(const char *, void *),
  void *cb_data
);

void regexomatic_fini(regexomatic_t *);

_Bool
regexomatic_add_word(
  regexomatic_t *,
  const uint8_t *,
  const size_t len
);

_Bool
regexomatic_read(
  regexomatic_t *,
  const char *
);

_Bool
regexomatic_write(
  regexomatic_t *,
  bool (*)(const uint8_t *, const size_t, void *),
  void *
);

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* REGEXOMATIC_H */
