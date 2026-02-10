#define _POSIX_C_SOURCE 200809L
#include "foreign.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

/* ============================================================================
 * Breakout tags — HTML elements that force exit from foreign content
 * WHATWG §13.2.6.7 "Any other start tag"
 * Sorted alphabetically for bsearch().
 * ============================================================================ */

static const char *breakout_tags[] = {
    "a", "address", "applet", "area", "article", "aside",
    "b", "base", "basefont", "bgsound", "big", "blockquote", "body", "br", "button",
    "caption", "center", "code", "col", "colgroup",
    "dd", "details", "dir", "div", "dl", "dt",
    "em", "embed",
    "fieldset", "figcaption", "figure", "footer", "form", "frame", "frameset",
    "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hgroup", "hr", "html",
    "i", "iframe", "img", "input",
    "li", "link", "listing",
    "main", "marquee", "menu", "meta",
    "nav", "nobr", "noembed", "noframes", "noscript",
    "object", "ol",
    "p", "param", "plaintext", "pre",
    "s", "script", "section", "select", "small", "source", "span", "strike",
    "strong", "style", "sub", "summary", "sup",
    "table", "tbody", "td", "template", "textarea", "tfoot", "th", "thead",
    "title", "tr", "track", "tt",
    "u", "ul",
    "var",
    "wbr", "xmp"
};

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

int is_foreign_breakout_tag(const char *name) {
    if (!name) return 0;
    size_t count = sizeof(breakout_tags) / sizeof(breakout_tags[0]);
    return bsearch(&name, breakout_tags, count, sizeof(const char *), cmp_str) != NULL;
}

int font_has_breakout_attr(const token_attr *attrs, size_t count) {
    if (!attrs) return 0;
    for (size_t i = 0; i < count; ++i) {
        if (!attrs[i].name) continue;
        if (strcmp(attrs[i].name, "color") == 0 ||
            strcmp(attrs[i].name, "face") == 0 ||
            strcmp(attrs[i].name, "size") == 0) {
            return 1;
        }
    }
    return 0;
}

/* ============================================================================
 * SVG element name case corrections (33 entries)
 * Sorted by lowercase key for bsearch().
 * ============================================================================ */

typedef struct {
    const char *lower;
    const char *correct;
} name_map;

static int cmp_name_map(const void *a, const void *b) {
    return strcmp(((const name_map *)a)->lower, ((const name_map *)b)->lower);
}

static const name_map svg_element_map[] = {
    {"altglyph",            "altGlyph"},
    {"altglyphdef",         "altGlyphDef"},
    {"altglyphitem",        "altGlyphItem"},
    {"animatecolor",        "animateColor"},
    {"animatemotion",       "animateMotion"},
    {"animatetransform",    "animateTransform"},
    {"clippath",            "clipPath"},
    {"feblend",             "feBlend"},
    {"fecolormatrix",       "feColorMatrix"},
    {"fecomponenttransfer", "feComponentTransfer"},
    {"fecomposite",         "feComposite"},
    {"feconvolvematrix",    "feConvolveMatrix"},
    {"fediffuselighting",   "feDiffuseLighting"},
    {"fedisplacementmap",   "feDisplacementMap"},
    {"fedistantlight",      "feDistantLight"},
    {"fedropshadow",        "feDropShadow"},
    {"feflood",             "feFlood"},
    {"fefunca",             "feFuncA"},
    {"fefuncb",             "feFuncB"},
    {"fefuncg",             "feFuncG"},
    {"fefuncr",             "feFuncR"},
    {"fegaussianblur",      "feGaussianBlur"},
    {"feimage",             "feImage"},
    {"femerge",             "feMerge"},
    {"femergenode",         "feMergeNode"},
    {"femorphology",        "feMorphology"},
    {"feoffset",            "feOffset"},
    {"fepointlight",        "fePointLight"},
    {"fespecularlighting",  "feSpecularLighting"},
    {"fespotlight",         "feSpotLight"},
    {"fetile",              "feTile"},
    {"feturbulence",        "feTurbulence"},
    {"foreignobject",       "foreignObject"},
    {"glyphref",            "glyphRef"},
    {"lineargradient",      "linearGradient"},
    {"radialgradient",      "radialGradient"},
    {"textpath",            "textPath"},
};

const char *svg_adjust_element_name(const char *lowered) {
    if (!lowered) return lowered;
    name_map key = {lowered, NULL};
    size_t count = sizeof(svg_element_map) / sizeof(svg_element_map[0]);
    name_map *found = (name_map *)bsearch(&key, svg_element_map, count, sizeof(name_map), cmp_name_map);
    return found ? found->correct : lowered;
}

/* ============================================================================
 * SVG attribute name case corrections
 * Sorted by lowercase key for bsearch().
 * ============================================================================ */

static const name_map svg_attr_map[] = {
    {"attributename",       "attributeName"},
    {"attributetype",       "attributeType"},
    {"basefrequency",       "baseFrequency"},
    {"baseprofile",         "baseProfile"},
    {"calcmode",            "calcMode"},
    {"clippathunits",       "clipPathUnits"},
    {"diffuseconstant",     "diffuseConstant"},
    {"edgemode",            "edgeMode"},
    {"filterunits",         "filterUnits"},
    {"glyphref",            "glyphRef"},
    {"gradienttransform",   "gradientTransform"},
    {"gradientunits",       "gradientUnits"},
    {"kernelmatrix",        "kernelMatrix"},
    {"kernelunitlength",    "kernelUnitLength"},
    {"keypoints",           "keyPoints"},
    {"keysplines",          "keySplines"},
    {"keytimes",            "keyTimes"},
    {"lengthadjust",        "lengthAdjust"},
    {"limitingconeangle",   "limitingConeAngle"},
    {"markerheight",        "markerHeight"},
    {"markerunits",         "markerUnits"},
    {"markerwidth",         "markerWidth"},
    {"maskcontentunits",    "maskContentUnits"},
    {"maskunits",           "maskUnits"},
    {"numoctaves",          "numOctaves"},
    {"pathlength",          "pathLength"},
    {"patterncontentunits", "patternContentUnits"},
    {"patterntransform",    "patternTransform"},
    {"patternunits",        "patternUnits"},
    {"pointsatx",           "pointsAtX"},
    {"pointsaty",           "pointsAtY"},
    {"pointsatz",           "pointsAtZ"},
    {"preservealpha",       "preserveAlpha"},
    {"preserveaspectratio", "preserveAspectRatio"},
    {"primitiveunits",      "primitiveUnits"},
    {"refx",                "refX"},
    {"refy",                "refY"},
    {"repeatcount",         "repeatCount"},
    {"repeatdur",           "repeatDur"},
    {"requiredextensions",  "requiredExtensions"},
    {"requiredfeatures",    "requiredFeatures"},
    {"specularconstant",    "specularConstant"},
    {"specularexponent",    "specularExponent"},
    {"spreadmethod",        "spreadMethod"},
    {"startoffset",         "startOffset"},
    {"stddeviation",        "stdDeviation"},
    {"stitchtiles",         "stitchTiles"},
    {"surfacescale",        "surfaceScale"},
    {"systemlanguage",      "systemLanguage"},
    {"tablevalues",         "tableValues"},
    {"targetx",             "targetX"},
    {"targety",             "targetY"},
    {"textlength",          "textLength"},
    {"viewbox",             "viewBox"},
    {"viewtarget",          "viewTarget"},
    {"xchannelselector",    "xChannelSelector"},
    {"ychannelselector",    "yChannelSelector"},
    {"zoomandpan",          "zoomAndPan"},
};

const char *svg_adjust_attr_name(const char *lowered) {
    if (!lowered) return lowered;
    name_map key = {lowered, NULL};
    size_t count = sizeof(svg_attr_map) / sizeof(svg_attr_map[0]);
    name_map *found = (name_map *)bsearch(&key, svg_attr_map, count, sizeof(name_map), cmp_name_map);
    return found ? found->correct : lowered;
}

/* ============================================================================
 * MathML attribute name correction
 * ============================================================================ */

const char *mathml_adjust_attr_name(const char *lowered) {
    if (!lowered) return lowered;
    if (strcmp(lowered, "definitionurl") == 0) return "definitionURL";
    return lowered;
}

/* ============================================================================
 * Integration point detection
 * ============================================================================ */

int is_mathml_text_integration_point(const char *name) {
    if (!name) return 0;
    return strcmp(name, "mi") == 0 ||
           strcmp(name, "mo") == 0 ||
           strcmp(name, "mn") == 0 ||
           strcmp(name, "ms") == 0 ||
           strcmp(name, "mtext") == 0;
}

int is_html_integration_point(const char *name, node_namespace ns,
                              const node_attr *attrs, size_t attr_count) {
    if (!name) return 0;
    /* SVG: foreignObject, desc, title */
    if (ns == NS_SVG) {
        return strcmp(name, "foreignObject") == 0 ||
               strcmp(name, "desc") == 0 ||
               strcmp(name, "title") == 0;
    }
    /* MathML: annotation-xml with encoding="text/html" or "application/xhtml+xml" */
    if (ns == NS_MATHML && strcmp(name, "annotation-xml") == 0) {
        for (size_t i = 0; i < attr_count; ++i) {
            if (attrs && attrs[i].name && strcmp(attrs[i].name, "encoding") == 0 && attrs[i].value) {
                /* case-insensitive comparison */
                const char *v = attrs[i].value;
                if (strcasecmp(v, "text/html") == 0 ||
                    strcasecmp(v, "application/xhtml+xml") == 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* ============================================================================
 * Namespace-aware special/scoping element checks
 * ============================================================================ */

/* Forward-declare the HTML-only version from tree_builder.c — we inline the logic here
   instead of depending on tree_builder's static function. */
static int is_html_special(const char *name) {
    if (!name) return 0;
    return strcmp(name, "address") == 0 ||
           strcmp(name, "applet") == 0 ||
           strcmp(name, "area") == 0 ||
           strcmp(name, "article") == 0 ||
           strcmp(name, "aside") == 0 ||
           strcmp(name, "base") == 0 ||
           strcmp(name, "basefont") == 0 ||
           strcmp(name, "blockquote") == 0 ||
           strcmp(name, "body") == 0 ||
           strcmp(name, "br") == 0 ||
           strcmp(name, "button") == 0 ||
           strcmp(name, "caption") == 0 ||
           strcmp(name, "center") == 0 ||
           strcmp(name, "col") == 0 ||
           strcmp(name, "colgroup") == 0 ||
           strcmp(name, "dd") == 0 ||
           strcmp(name, "details") == 0 ||
           strcmp(name, "dir") == 0 ||
           strcmp(name, "div") == 0 ||
           strcmp(name, "dl") == 0 ||
           strcmp(name, "dt") == 0 ||
           strcmp(name, "embed") == 0 ||
           strcmp(name, "fieldset") == 0 ||
           strcmp(name, "figcaption") == 0 ||
           strcmp(name, "figure") == 0 ||
           strcmp(name, "footer") == 0 ||
           strcmp(name, "form") == 0 ||
           strcmp(name, "frame") == 0 ||
           strcmp(name, "frameset") == 0 ||
           strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
           strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0 ||
           strcmp(name, "h5") == 0 || strcmp(name, "h6") == 0 ||
           strcmp(name, "head") == 0 ||
           strcmp(name, "header") == 0 ||
           strcmp(name, "hgroup") == 0 ||
           strcmp(name, "hr") == 0 ||
           strcmp(name, "html") == 0 ||
           strcmp(name, "iframe") == 0 ||
           strcmp(name, "img") == 0 ||
           strcmp(name, "input") == 0 ||
           strcmp(name, "li") == 0 ||
           strcmp(name, "link") == 0 ||
           strcmp(name, "listing") == 0 ||
           strcmp(name, "main") == 0 ||
           strcmp(name, "marquee") == 0 ||
           strcmp(name, "menu") == 0 ||
           strcmp(name, "meta") == 0 ||
           strcmp(name, "nav") == 0 ||
           strcmp(name, "noembed") == 0 ||
           strcmp(name, "noframes") == 0 ||
           strcmp(name, "noscript") == 0 ||
           strcmp(name, "object") == 0 ||
           strcmp(name, "ol") == 0 ||
           strcmp(name, "p") == 0 ||
           strcmp(name, "param") == 0 ||
           strcmp(name, "plaintext") == 0 ||
           strcmp(name, "pre") == 0 ||
           strcmp(name, "script") == 0 ||
           strcmp(name, "section") == 0 ||
           strcmp(name, "select") == 0 ||
           strcmp(name, "source") == 0 ||
           strcmp(name, "style") == 0 ||
           strcmp(name, "summary") == 0 ||
           strcmp(name, "table") == 0 ||
           strcmp(name, "tbody") == 0 ||
           strcmp(name, "td") == 0 ||
           strcmp(name, "template") == 0 ||
           strcmp(name, "textarea") == 0 ||
           strcmp(name, "tfoot") == 0 ||
           strcmp(name, "th") == 0 ||
           strcmp(name, "thead") == 0 ||
           strcmp(name, "title") == 0 ||
           strcmp(name, "tr") == 0 ||
           strcmp(name, "track") == 0 ||
           strcmp(name, "ul") == 0 ||
           strcmp(name, "wbr") == 0 ||
           strcmp(name, "xmp") == 0;
}

int is_special_element_ns(const char *name, node_namespace ns) {
    if (!name) return 0;
    if (ns == NS_HTML) return is_html_special(name);
    if (ns == NS_MATHML) {
        return strcmp(name, "mi") == 0 ||
               strcmp(name, "mo") == 0 ||
               strcmp(name, "mn") == 0 ||
               strcmp(name, "ms") == 0 ||
               strcmp(name, "mtext") == 0 ||
               strcmp(name, "annotation-xml") == 0;
    }
    if (ns == NS_SVG) {
        return strcmp(name, "foreignObject") == 0 ||
               strcmp(name, "desc") == 0 ||
               strcmp(name, "title") == 0;
    }
    return 0;
}

static int is_html_scoping(const char *name) {
    if (!name) return 0;
    return strcmp(name, "applet") == 0 ||
           strcmp(name, "caption") == 0 ||
           strcmp(name, "html") == 0 ||
           strcmp(name, "table") == 0 ||
           strcmp(name, "td") == 0 ||
           strcmp(name, "th") == 0 ||
           strcmp(name, "marquee") == 0 ||
           strcmp(name, "object") == 0 ||
           strcmp(name, "template") == 0;
}

int is_scoping_element_ns(const char *name, node_namespace ns) {
    if (!name) return 0;
    if (ns == NS_HTML) return is_html_scoping(name);
    if (ns == NS_MATHML) {
        return strcmp(name, "mi") == 0 ||
               strcmp(name, "mo") == 0 ||
               strcmp(name, "mn") == 0 ||
               strcmp(name, "ms") == 0 ||
               strcmp(name, "mtext") == 0 ||
               strcmp(name, "annotation-xml") == 0;
    }
    if (ns == NS_SVG) {
        return strcmp(name, "foreignObject") == 0 ||
               strcmp(name, "desc") == 0 ||
               strcmp(name, "title") == 0;
    }
    return 0;
}
