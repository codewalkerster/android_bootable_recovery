#include "install/partition.h"

#include <unistd.h>

#include "recovery_ui/ui.h"
#include "recovery_utils/roots.h"

static void waitUntilAccess() {
  while (access( "/dev/block/by-name/userdata", F_OK) != 0) {
    usleep(200000);
  }
}

void CreateUserData(Device *device, const std::string &bootDevice) {
  if (access( "/dev/block/by-name/userdata", F_OK) != 0) {
    RecoveryUI* ui = device->GetUI();
    ui->Print("Create data partition.\n");
    create_userdata_volume(bootDevice);
    waitUntilAccess();
  }
}
