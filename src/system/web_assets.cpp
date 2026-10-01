#include <stddef.h>
#include <stdint.h>

struct StaticAsset {
    const uint8_t* data;
    size_t length;
};

namespace {

const uint8_t kIndexData[] = {
#include "index_html.inc"
};
const uint8_t kJavaScriptData[] = {
#include "app_js.inc"
};
const uint8_t kStylesheetData[] = {
#include "index_css.inc"
};

}  // namespace

extern const StaticAsset kAppHtml{kIndexData, sizeof(kIndexData)};
extern const StaticAsset kAppJavaScript{kJavaScriptData, sizeof(kJavaScriptData)};
extern const StaticAsset kAppStylesheet{kStylesheetData, sizeof(kStylesheetData)};
