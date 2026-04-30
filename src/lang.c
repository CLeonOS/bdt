#include "bdt.h"

#include <string.h>

static BdtLanguage parse_lang(const char *s) {
    if (!s) return BDT_LANG_EN;
    if (!strcmp(s, "zh") || !strcmp(s, "zh-CN") || !strcmp(s, "cn")) return BDT_LANG_ZH;
    return BDT_LANG_EN;
}

BdtLanguage bdt_lang_from_text(const char *s) {
    return parse_lang(s);
}
