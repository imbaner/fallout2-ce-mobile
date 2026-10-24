#ifndef FALLOUT_APP_UPDATE_H_
#define FALLOUT_APP_UPDATE_H_

#include <string>

namespace fallout {

// Values shared with Android's UpdateManager. Desktop builds are unsupported
// except inside a running dev autotest (no network in those tests).
enum class AppUpdateState {
    Unsupported = -1,
    Idle,
    Checking,
    UpToDate,
    Available,
    Downloading,
    Downloaded,
    Installing,
    Error,
    Permission,
    NoAsset,
};

enum class AppUpdateError {
    None,
    Check,
    Service,
    Download,
    InvalidApk,
    Install,
};

AppUpdateState appUpdateState();
AppUpdateError appUpdateError();
std::string appUpdateTag();
std::string appUpdateNotes();
void appUpdateProgress(long long* done, long long* total);
bool appUpdateReady();
bool appUpdateHasRelease();
void appUpdateCheck(bool manual);
void appUpdateDownload();
void appUpdateInstall();
void appUpdateCancel();
void appUpdateDelete();
// Only the update dialog opened from the main menu can launch an installer.
// Closing it revokes that intent, even if a worker finishes later.
void appUpdateSetActive(bool active);

void appUpdateTestState(AppUpdateState state, const char* tag, AppUpdateError error = AppUpdateError::Download);
int appUpdateTestInstalls();

} // namespace fallout

#endif /* FALLOUT_APP_UPDATE_H_ */
