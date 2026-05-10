/**
 * tpl.h - Template renderer.
 * Summary: Public API for rendering scoped template strings.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_TPL_H
#define KC_TPL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_tpl kc_tpl_t;

#define KC_TPL_OK      0
#define KC_TPL_ERROR  -1

/**
 * Allocates a new renderer context.
 * @return Context pointer, or NULL on allocation failure.
 */
kc_tpl_t *kc_tpl_open(void);

/**
 * Releases a renderer context and its owned data.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_tpl_close(kc_tpl_t *ctx);

/**
 * Sets the include root used by include directives.
 * @param ctx Context pointer.
 * @param root Include root path.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_set_root(kc_tpl_t *ctx, const char *root);

/**
 * Stores or updates one renderer variable.
 * @param ctx Context pointer.
 * @param key Variable key.
 * @param value Variable value.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_set_var(kc_tpl_t *ctx, const char *key, const char *value);

/**
 * Renders one template string into an owned output buffer.
 * @param ctx Context pointer.
 * @param input Template input.
 * @param output Destination pointer for owned output.
 * @return KC_TPL_OK on success, or KC_TPL_ERROR on failure.
 */
int kc_tpl_render_string(kc_tpl_t *ctx, const char *input, char **output);

/**
 * Returns the latest context error.
 * @param ctx Context pointer.
 * @return Static or context-owned error text.
 */
const char *kc_tpl_strerror(const kc_tpl_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
