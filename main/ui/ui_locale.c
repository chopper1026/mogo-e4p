/* 固定文案词典；堆栈、TF、网络和用户输入不参与匹配。/ Fixed-copy dictionary; never match stack, TF, network or user input. */
#include "ui_locale.h"
#include <stdint.h>
#include <string.h>
typedef struct {
    const char *key, *tw, *en;
} entry_t;
static const entry_t catalog[] = {
#include "locale_catalog.inc"
};
const char* ui_tr_for(ui_language_t lang, const char* key) {
    extern const char _rodata_start[], _rodata_end[];
    if (lang == UI_LANG_CN || !key || (uintptr_t)key < (uintptr_t)_rodata_start ||
        (uintptr_t)key >= (uintptr_t)_rodata_end)
        return key;
    size_t lo = 0, hi = sizeof(catalog) / sizeof(*catalog);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(key, catalog[mid].key);
        if (!c) return lang == UI_LANG_TW ? catalog[mid].tw : catalog[mid].en;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return key;
}
const char* ui_tr(const char* literal) {
    return ui_tr_for(ui_language(), literal);
}
