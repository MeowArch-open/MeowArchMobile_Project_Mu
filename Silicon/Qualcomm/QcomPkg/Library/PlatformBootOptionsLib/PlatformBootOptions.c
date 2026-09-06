#include <Library/DebugLib.h>
#include <Library/PlatformBootOptionsLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>

VOID
GetPlatformBootOptions (
  OUT EFI_PLATFORM_BOOT_OPTION **BootOption,
  OUT UINT8                     *BootOptionCount)
{
  EFI_PLATFORM_BOOT_OPTION *LocalBootOption      = NULL;
  UINT8                     LocalBootOptionCount = 0;

  // Allocate Memory
  LocalBootOption = AllocateZeroPool (2 * sizeof (EFI_PLATFORM_BOOT_OPTION));
  if (LocalBootOption == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Allocate Memory for Boot Options!\n", __FUNCTION__));
    goto exit;
  }

  // Add Mass Storage Boot Option
  LocalBootOption[0].AppName       = L"Mass Storage";
  LocalBootOption[0].AppGuid       = FixedPcdGetPtr (PcdMassStorageFile);
  LocalBootOption[0].LoadAttribute = LOAD_OPTION_HIDDEN;

  // Add Switch Slot Boot Option
  LocalBootOption[1].AppName       = L"Switch Slot";
  LocalBootOption[1].AppGuid       = FixedPcdGetPtr (PcdSwitchSlotFile);
  LocalBootOption[1].LoadAttribute = LOAD_OPTION_HIDDEN;

  // Set Boot Option Count
  LocalBootOptionCount = 2;

exit:
  // Pass Data
  *BootOption      = LocalBootOption;
  *BootOptionCount = LocalBootOptionCount;
}
