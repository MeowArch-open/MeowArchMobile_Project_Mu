#ifndef _SWITCH_SLOT_H_
#define _SWITCH_SLOT_H_

//
// Active Slot Attribute of a GPT Partition Entry
//
#define AB_SLOT_ACTIVE_ATTRIBUTE BIT50

//
// UFS Boot LUN Values
//
#define UFS_BOOT_LU_SLOT_A 1
#define UFS_BOOT_LU_SLOT_B 2

//
// GPT Limits
//
#define GPT_MAX_ENTRY_ARRAY_SIZE SIZE_1MB
#define GPT_MAX_NAME_LENGTH      36

//
// A/B Slots
//
typedef enum {
  AbSlotA = 0,
  AbSlotB = 1
} AB_SLOT;

//
// GPT Disk
//
typedef struct {
  EFI_BLOCK_IO_PROTOCOL      *BlockIo;
  EFI_DISK_IO_PROTOCOL       *DiskIo;
  EFI_PARTITION_TABLE_HEADER  PrimaryHeader;
  EFI_PARTITION_TABLE_HEADER  BackupHeader;
  EFI_PARTITION_TABLE_HEADER  OriginalPrimaryHeader;
  EFI_PARTITION_TABLE_HEADER  OriginalBackupHeader;
  EFI_PARTITION_ENTRY        *PrimaryEntries;
  EFI_PARTITION_ENTRY        *BackupEntries;
  EFI_PARTITION_ENTRY        *OriginalPrimaryEntries;
  EFI_PARTITION_ENTRY        *OriginalBackupEntries;
  UINTN                       EntryArraySize;
  BOOLEAN                     Modified;
} GPT_DISK;

#endif /* _SWITCH_SLOT_H_ */
