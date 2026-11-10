#include "storage.h"

#include <stdlib.h>

#include <emscripten.h>

namespace fallout {

// clang-format off
EM_JS(char*, webGameUserRootJs, (), {
    const root = Module['webGameUserRoot'];
    return typeof root === 'string' && root ? stringToNewUTF8(root) : 0;
});
// clang-format on

const std::string& webGameUserRoot()
{
    static std::string root;
    if (root.empty()) {
        char* value = webGameUserRootJs();
        root = value != nullptr ? value : kWebUserRoot;
        free(value);
    }
    return root;
}

std::string webPortDataPath(const char* name)
{
    return webGameUserRoot() + "/port/" + name;
}

// clang-format off
EM_ASYNC_JS(int, webCommitUserFilesJs, (), {
    if (Module['webStorageMode'] === 'temporary') return 1;
    try {
        if (!Module['webUserFiles']) throw new Error('User storage is not mounted');
        await Module['webUserFiles'].commit();
        return 1;
    } catch (error) {
        console.error('Persistent save failed', error);
        return 0;
    }
});

EM_ASYNC_JS(int, webRollbackUserFilesJs, (), {
    if (Module['webStorageMode'] === 'temporary') return 1;
    try {
        if (!Module['webUserFiles']) throw new Error('User storage is not mounted');
        await Module['webUserFiles'].rollback();
        return 1;
    } catch (error) {
        console.error('User storage could not be restored', error);
        return 0;
    }
});

EM_JS(void, webUserWriteFailedJs, (), {
    try {
        Module['onUserWriteFailure']?.({kind:'IOError',fatal:!!Module['webUserFiles']?.status().blocked});
    } catch (error) { console.error('Storage notice failed',error); }
});
// clang-format on

bool webCommitUserFiles() { return webCommitUserFilesJs() != 0; }
bool webRollbackUserFiles() { return webRollbackUserFilesJs() != 0; }
void webUserWriteFailed() { webUserWriteFailedJs(); }

} // namespace fallout
