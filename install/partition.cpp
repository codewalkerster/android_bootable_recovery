#include "install/partition.h"

#include <unistd.h>

#include "recovery_ui/ui.h"
#include "recovery_utils/roots.h"

void CreateUserData(Device *device, int isSdBoot) {
  if (access( "/dev/block/by-name/userdata", F_OK) != 0) {
    RecoveryUI* ui = device->GetUI();
    ui->Print("Create data partition.\n");
    create_userdata_volume(isSdBoot);
  }
}

