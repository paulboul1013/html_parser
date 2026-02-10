#ifndef HTML_PARSER_FOREIGN_H
#define HTML_PARSER_FOREIGN_H

#include "tree.h"
#include "token.h"

/* Returns 1 if the tag name is a "breakout" tag that should exit foreign content */
int is_foreign_breakout_tag(const char *name);

/* Returns 1 if a <font> tag has color/face/size attributes (triggers breakout) */
int font_has_breakout_attr(const token_attr *attrs, size_t count);

/* SVG element name case correction: "clippath" → "clipPath", etc.
   Returns corrected name if found, otherwise returns the input name. */
const char *svg_adjust_element_name(const char *lowered);

/* SVG attribute name case correction: "viewbox" → "viewBox", etc.
   Returns corrected name if found, otherwise returns the input name. */
const char *svg_adjust_attr_name(const char *lowered);

/* MathML attribute name correction: "definitionurl" → "definitionURL"
   Returns corrected name if found, otherwise returns the input name. */
const char *mathml_adjust_attr_name(const char *lowered);

/* MathML text integration points: mi, mo, mn, ms, mtext */
int is_mathml_text_integration_point(const char *name);

/* HTML integration points: SVG foreignObject/desc/title,
   MathML annotation-xml with encoding="text/html" or "application/xhtml+xml" */
int is_html_integration_point(const char *name, node_namespace ns,
                              const node_attr *attrs, size_t attr_count);

/* Namespace-aware special element check */
int is_special_element_ns(const char *name, node_namespace ns);

/* Namespace-aware scoping element check */
int is_scoping_element_ns(const char *name, node_namespace ns);

#endif
