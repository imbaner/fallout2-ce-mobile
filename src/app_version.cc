#include "app_version.h"

#ifdef __EMSCRIPTEN__
#include <string>

#include <emscripten.h>
#else
#include "platform/git_version.h"
#endif

#ifndef FALLOUT_APP_VERSION
#define FALLOUT_APP_VERSION "1.3.0"
#endif

namespace fallout {

#ifdef __EMSCRIPTEN__

namespace {

    // clang-format off
    EM_JS(void, webPageTextJs, (const char* key, char* buffer, int capacity), {
        stringToUTF8(String(Module[UTF8ToString(key)] || ''), buffer, capacity);
    });
    // clang-format on

    // Module[key] as the page set it, "development" without a page.
    const char* pageText(const char* key, std::string* text)
    {
        if (text->empty()) {
            char buffer[96] = {};
            webPageTextJs(key, buffer, sizeof(buffer));
            *text = buffer[0] != '\0' ? buffer : "development";
        }
        return text->c_str();
    }

} // namespace

const char* appVersion()
{
    static std::string version;
    return pageText("webAppVersion", &version);
}

const char* appBuild()
{
    static std::string build;
    return pageText("webAppBuild", &build);
}

#else

const char* appVersion()
{
    return FALLOUT_APP_VERSION;
}

const char* appBuild()
{
    return _BUILD_HASH;
}

#endif

} // namespace fallout
