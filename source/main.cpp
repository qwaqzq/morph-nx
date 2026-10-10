#include "app.hpp"
#include <switch.h>

extern "C" {
u32 __nx_applet_exit_mode = 0;
}

int main(int argc, char* argv[]) {
    morph::App app(argc > 0 ? argv[0] : "");
    app.Run();
    return 0;
}

extern "C" {

void userAppInit(void) {
    Result rc;
    if (R_FAILED(rc = appletLockExit()))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = plInitialize(PlServiceType_User)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = romfsInit()))
        diagAbortWithResult(rc);

    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);
}

void userAppExit(void) {
    romfsExit();
    plExit();

    if (auto fs = fsdevGetDeviceFileSystem("sdmc:")) {
        fsFsCommit(fs);
    }

    appletUnlockExit();
}

} // extern "C"
