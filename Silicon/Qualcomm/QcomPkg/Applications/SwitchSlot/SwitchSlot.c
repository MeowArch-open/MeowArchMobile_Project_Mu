#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/EFICardInfo.h>

#include <Uefi/UefiGpt.h>

#include "SwitchSlot.h"

STATIC
EFI_STATUS
GptLbaToOffset (
  IN  EFI_BLOCK_IO_PROTOCOL *BlockIo,
  IN  EFI_LBA                Lba,
  OUT UINT64                *Offset)
{
  UINT64 BlockSize;

  // Verify Parameters
  if (BlockIo == NULL || BlockIo->Media == NULL || Offset == NULL || BlockIo->Media->BlockSize == 0) {
    return EFI_INVALID_PARAMETER;
  }

  // Get Block Size
  BlockSize = BlockIo->Media->BlockSize;

  // Check LBA
  if (Lba > (MAX_UINT64 / BlockSize)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // Pass Offset
  *Offset = Lba * BlockSize;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ValidateGptHeader (
  IN EFI_PARTITION_TABLE_HEADER *Header,
  IN EFI_LBA                     ExpectedLba,
  IN UINTN                       BlockSize)
{
  EFI_PARTITION_TABLE_HEADER TempHeader;
  EFI_STATUS                 Status;
  UINT32                     Crc;

  // Verify Parameter
  if (Header == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Check GPT Signature
  if (Header->Header.Signature != EFI_PTAB_HEADER_ID) {
    return EFI_NOT_FOUND;
  }

  // Check GPT Header Size
  if (Header->Header.HeaderSize != sizeof (EFI_PARTITION_TABLE_HEADER) || Header->Header.HeaderSize > BlockSize) {
    return EFI_COMPROMISED_DATA;
  }

  // Check GPT Header Location
  if (Header->MyLBA != ExpectedLba) {
    return EFI_COMPROMISED_DATA;
  }

  // Copy GPT Header
  CopyMem (&TempHeader, Header, sizeof (TempHeader));

  // Clear GPT Header CRC32
  TempHeader.Header.CRC32 = 0;

  // Calculate GPT Header CRC32
  Status = gBS->CalculateCrc32 (&TempHeader, Header->Header.HeaderSize, &Crc);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Compare GPT Header CRC32
  if (Crc != Header->Header.CRC32) {
    return EFI_COMPROMISED_DATA;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReadGptHeader (
  IN  EFI_BLOCK_IO_PROTOCOL      *BlockIo,
  IN  EFI_DISK_IO_PROTOCOL       *DiskIo,
  IN  EFI_LBA                     Lba,
  OUT EFI_PARTITION_TABLE_HEADER *Header)
{
  EFI_STATUS Status;
  UINT64     Offset;
  VOID      *Buffer;
  UINTN      BlockSize;

  // Verify Parameters
  if (BlockIo == NULL || DiskIo == NULL || Header == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Get Block Size
  BlockSize = BlockIo->Media->BlockSize;

  // Allocate Block Buffer
  Buffer = AllocateZeroPool (BlockSize);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  // Get GPT Header Offset
  Status = GptLbaToOffset (BlockIo, Lba, &Offset);
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return Status;
  }

  // Read GPT Header
  Status = DiskIo->ReadDisk (DiskIo, BlockIo->Media->MediaId, Offset, BlockSize, Buffer);
  if (EFI_ERROR (Status)) {
    FreePool (Buffer);
    return Status;
  }

  // Copy GPT Header
  CopyMem (Header, Buffer, sizeof (*Header));

  // Free Buffer
  FreePool (Buffer);

  // Validate GPT Header
  return ValidateGptHeader (Header, Lba, BlockSize);
}

STATIC
EFI_STATUS
ValidateGptEntryArray (
  IN EFI_PARTITION_TABLE_HEADER *Header,
  IN EFI_PARTITION_ENTRY        *Entries,
  IN UINTN                       EntryArraySize)
{
  EFI_STATUS Status;
  UINT32     Crc;

  // Verify Parameters
  if (Header == NULL || Entries == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Calculate Partition Entry Array CRC32
  Status = gBS->CalculateCrc32 (Entries, EntryArraySize, &Crc);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Compare Partition Entry Array CRC32
  if (Crc != Header->PartitionEntryArrayCRC32) {
    return EFI_COMPROMISED_DATA;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReadGptEntryArray (
  IN  GPT_DISK                   *Disk,
  IN  EFI_PARTITION_TABLE_HEADER *Header,
  OUT EFI_PARTITION_ENTRY       **Entries)
{
  EFI_STATUS Status;
  UINT64     Offset;

  // Verify Parameters
  if (Disk == NULL || Header == NULL || Entries == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Allocate Partition Entry Array Buffer
  *Entries = AllocateZeroPool (Disk->EntryArraySize);
  if (*Entries == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  // Get Partition Entry Array Offset
  Status = GptLbaToOffset (Disk->BlockIo, Header->PartitionEntryLBA, &Offset);
  if (EFI_ERROR (Status)) {
    goto exit;
  }

  // Read Partition Entry Array
  Status = Disk->DiskIo->ReadDisk (Disk->DiskIo, Disk->BlockIo->Media->MediaId, Offset, Disk->EntryArraySize, *Entries);
  if (EFI_ERROR (Status)) {
    goto exit;
  }

  // Validate Partition Entry Array
  Status = ValidateGptEntryArray (Header, *Entries, Disk->EntryArraySize);
  if (EFI_ERROR (Status)) {
    goto exit;
  }

  return EFI_SUCCESS;

exit:
  // Free Buffer
  FreePool (*Entries);

  *Entries = NULL;

  return Status;
}

STATIC
VOID
FreeGptDisk (IN OUT GPT_DISK *Disk)
{
  // Verify Parameter
  if (Disk == NULL) {
    return;
  }

  // Free Partition Entry Array Buffers
  if (Disk->PrimaryEntries != NULL) {
    FreePool (Disk->PrimaryEntries);
  }

  if (Disk->BackupEntries != NULL) {
    FreePool (Disk->BackupEntries);
  }

  if (Disk->OriginalPrimaryEntries != NULL) {
    FreePool (Disk->OriginalPrimaryEntries);
  }

  if (Disk->OriginalBackupEntries != NULL) {
    FreePool (Disk->OriginalBackupEntries);
  }

  // Reset Disk
  ZeroMem (Disk, sizeof (*Disk));
}

STATIC
VOID
FreeGptDisks (
  IN OUT GPT_DISK *Disks,
  IN     UINTN     DiskCount)
{
  // Verify Parameter
  if (Disks == NULL) {
    return;
  }

  // Free every Disk
  for (UINTN i = 0; i < DiskCount; i++) {
    FreeGptDisk (&Disks[i]);
  }

  // Free Disk Array
  FreePool (Disks);
}

STATIC
EFI_STATUS
LoadGptDisk (
  IN  EFI_HANDLE Handle,
  OUT GPT_DISK  *Disk)
{
  EFI_STATUS Status;
  UINTN      EntryArraySize;

  // Verify Parameter
  if (Disk == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Reset Disk
  ZeroMem (Disk, sizeof (*Disk));

  // Get Block IO Protocol
  Status = gBS->HandleProtocol (Handle, &gEfiBlockIoProtocolGuid, (VOID **)&Disk->BlockIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Skip Logical Partitions and unusable Storages
  if (Disk->BlockIo == NULL || Disk->BlockIo->Media == NULL || Disk->BlockIo->Media->LogicalPartition || !Disk->BlockIo->Media->MediaPresent) {
    return EFI_NOT_FOUND;
  }

  // Get Disk IO Protocol
  Status = gBS->HandleProtocol (Handle, &gEfiDiskIoProtocolGuid, (VOID **)&Disk->DiskIo);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  // Read Primary GPT Header
  Status = ReadGptHeader (Disk->BlockIo, Disk->DiskIo, PRIMARY_PART_HEADER_LBA, &Disk->PrimaryHeader);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Check Primary GPT Header Details
  if (Disk->PrimaryHeader.NumberOfPartitionEntries == 0 ||
      Disk->PrimaryHeader.SizeOfPartitionEntry < sizeof (EFI_PARTITION_ENTRY) ||
      Disk->PrimaryHeader.NumberOfPartitionEntries > (GPT_MAX_ENTRY_ARRAY_SIZE / Disk->PrimaryHeader.SizeOfPartitionEntry) ||
      Disk->PrimaryHeader.AlternateLBA > Disk->BlockIo->Media->LastBlock)
  {
    return EFI_COMPROMISED_DATA;
  }

  // Get Partition Entry Array Size
  EntryArraySize = Disk->PrimaryHeader.NumberOfPartitionEntries * Disk->PrimaryHeader.SizeOfPartitionEntry;
  if (EntryArraySize == 0) {
    return EFI_COMPROMISED_DATA;
  }

  // Set Partition Entry Array Size
  Disk->EntryArraySize = EntryArraySize;

  // Read Backup GPT Header
  Status = ReadGptHeader (Disk->BlockIo, Disk->DiskIo, Disk->PrimaryHeader.AlternateLBA, &Disk->BackupHeader);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Compare Primary and Backup GPT Header Details
  if (Disk->BackupHeader.NumberOfPartitionEntries != Disk->PrimaryHeader.NumberOfPartitionEntries ||
      Disk->BackupHeader.SizeOfPartitionEntry != Disk->PrimaryHeader.SizeOfPartitionEntry)
  {
    return EFI_COMPROMISED_DATA;
  }

  // Read Primary Partition Entry Array
  Status = ReadGptEntryArray (Disk, &Disk->PrimaryHeader, &Disk->PrimaryEntries);
  if (EFI_ERROR (Status)) {
    FreeGptDisk (Disk);
    return Status;
  }

  // Read Backup Partition Entry Array
  Status = ReadGptEntryArray (Disk, &Disk->BackupHeader, &Disk->BackupEntries);
  if (EFI_ERROR (Status)) {
    FreeGptDisk (Disk);
    return Status;
  }

  // Backup the Partition Entry Arrays for a possible Rollback
  Disk->OriginalPrimaryEntries = AllocateCopyPool (Disk->EntryArraySize, Disk->PrimaryEntries);
  Disk->OriginalBackupEntries  = AllocateCopyPool (Disk->EntryArraySize, Disk->BackupEntries);

  if (Disk->OriginalPrimaryEntries == NULL || Disk->OriginalBackupEntries == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Allocate Memory for the Original Partition Entries!\n", __FUNCTION__));

    FreeGptDisk (Disk);

    return EFI_OUT_OF_RESOURCES;
  }

  // Backup the GPT Headers for a possible Rollback
  CopyMem (&Disk->OriginalPrimaryHeader, &Disk->PrimaryHeader, sizeof (Disk->OriginalPrimaryHeader));
  CopyMem (&Disk->OriginalBackupHeader,  &Disk->BackupHeader,  sizeof (Disk->OriginalBackupHeader));

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
LoadGptDisks (
  OUT GPT_DISK **Disks,
  OUT UINTN     *DiskCount)
{
  EFI_STATUS  Status;
  EFI_HANDLE *Handles     = NULL;
  GPT_DISK   *LoadedDisks = NULL;
  UINTN       HandleCount = 0;
  UINTN       LoadedCount = 0;

  // Verify Parameters
  if (Disks == NULL || DiskCount == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Reset Output
  *Disks     = NULL;
  *DiskCount = 0;

  // Get every Block IO Handle
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Allocate Disk Array
  LoadedDisks = AllocateZeroPool (HandleCount * sizeof (GPT_DISK));
  if (LoadedDisks == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  // Go thru every Block IO Handle
  for (UINTN i = 0; i < HandleCount; i++) {
    // Load GPT of the Storage
    Status = LoadGptDisk (Handles[i], &LoadedDisks[LoadedCount]);
    if (!EFI_ERROR (Status)) {
      LoadedCount++;
      continue;
    }

    // Abort on Memory Errors
    if (Status == EFI_OUT_OF_RESOURCES) {
      break;
    }

    DEBUG ((EFI_D_INFO, "%a: Skipping Non-GPT Block IO Handle %u! Status = %r\n", __FUNCTION__, i, Status));

    Status = EFI_SUCCESS;
  }

  // Free Handle Buffer
  FreePool (Handles);

  // Check for Errors
  if (EFI_ERROR (Status)) {
    for (UINTN i = 0; i < LoadedCount; i++) {
      FreeGptDisk (&LoadedDisks[i]);
    }

    FreePool (LoadedDisks);

    return Status;
  }

  // Verify Disk Count
  if (LoadedCount == 0) {
    FreePool (LoadedDisks);
    return EFI_NOT_FOUND;
  }

  // Pass Disks
  *Disks     = LoadedDisks;
  *DiskCount = LoadedCount;

  return EFI_SUCCESS;
}

STATIC
EFI_PARTITION_ENTRY *
GetGptEntry (
  IN GPT_DISK *Disk,
  IN BOOLEAN   Backup,
  IN UINTN     EntryIndex)
{
  UINT8 *Entries;

  // Verify Parameters
  if (Disk == NULL || EntryIndex >= Disk->PrimaryHeader.NumberOfPartitionEntries) {
    return NULL;
  }

  // Get Partition Entry Array
  Entries = Backup ? (UINT8 *)Disk->BackupEntries : (UINT8 *)Disk->PrimaryEntries;

  // Get Partition Entry
  return (EFI_PARTITION_ENTRY *)(Entries + (EntryIndex * Disk->PrimaryHeader.SizeOfPartitionEntry));
}

STATIC
UINTN
GetPartitionNameLength (IN CONST CHAR16 PartitionName[GPT_MAX_NAME_LENGTH])
{
  // Search String Terminator
  for (UINTN i = 0; i < GPT_MAX_NAME_LENGTH; i++) {
    if (PartitionName[i] == L'\0') {
      return i;
    }
  }

  return GPT_MAX_NAME_LENGTH;
}

STATIC
BOOLEAN
IsGptEntryUnused (IN EFI_PARTITION_ENTRY *Entry)
{
  return (BOOLEAN)(Entry == NULL || IsZeroGuid (&Entry->PartitionTypeGUID));
}

STATIC
BOOLEAN
DoesPartitionNameEndWithSlot (
  IN CONST EFI_PARTITION_ENTRY *Entry,
  IN CHAR16                     Slot)
{
  UINTN NameLength;

  // Verify Parameter
  if (Entry == NULL) {
    return FALSE;
  }

  // Get Partition Name Length
  NameLength = GetPartitionNameLength (Entry->PartitionName);
  if (NameLength < 3) {
    return FALSE;
  }

  // Check Slot Suffix
  return (BOOLEAN)(Entry->PartitionName[NameLength - 2] == L'_' && Entry->PartitionName[NameLength - 1] == Slot);
}

STATIC
BOOLEAN
IsPartitionNameEqual (
  IN CONST EFI_PARTITION_ENTRY *Entry,
  IN CONST CHAR16              *PartitionName)
{
  UINTN EntryNameLength;
  UINTN NameLength;

  // Verify Parameters
  if (Entry == NULL || PartitionName == NULL) {
    return FALSE;
  }

  // Compare Partition Name Lengths
  EntryNameLength = GetPartitionNameLength (Entry->PartitionName);
  NameLength      = StrLen (PartitionName);

  if (EntryNameLength != NameLength) {
    return FALSE;
  }

  // Compare Partition Names
  return (BOOLEAN)(CompareMem (Entry->PartitionName, PartitionName, NameLength * sizeof (CHAR16)) == 0);
}

STATIC
BOOLEAN
AreEntryNamesEqual (
  IN CONST EFI_PARTITION_ENTRY *EntryA,
  IN CONST EFI_PARTITION_ENTRY *EntryB)
{
  UINTN NameLengthA;
  UINTN NameLengthB;

  // Verify Parameters
  if (EntryA == NULL || EntryB == NULL) {
    return FALSE;
  }

  // Compare Partition Name Lengths
  NameLengthA = GetPartitionNameLength (EntryA->PartitionName);
  NameLengthB = GetPartitionNameLength (EntryB->PartitionName);

  if (NameLengthA != NameLengthB) {
    return FALSE;
  }

  // Compare Partition Names
  return (BOOLEAN)(CompareMem (EntryA->PartitionName, EntryB->PartitionName, NameLengthA * sizeof (CHAR16)) == 0);
}

STATIC
BOOLEAN
AreEntriesSlotPair (
  IN CONST EFI_PARTITION_ENTRY *EntryA,
  IN CONST EFI_PARTITION_ENTRY *EntryB)
{
  UINTN NameLengthA;
  UINTN NameLengthB;

  // Verify Parameters
  if (EntryA == NULL || EntryB == NULL) {
    return FALSE;
  }

  // Get Partition Name Lengths
  NameLengthA = GetPartitionNameLength (EntryA->PartitionName);
  NameLengthB = GetPartitionNameLength (EntryB->PartitionName);

  // Check Partition Name Lengths and Slot Suffixes
  if (NameLengthA != NameLengthB || NameLengthA < 3 ||
      !DoesPartitionNameEndWithSlot (EntryA, L'a') ||
      !DoesPartitionNameEndWithSlot (EntryB, L'b'))
  {
    return FALSE;
  }

  // Compare Partition Names without the Slot Suffix
  return (BOOLEAN)(CompareMem (EntryA->PartitionName, EntryB->PartitionName, (NameLengthA - 2) * sizeof (CHAR16)) == 0);
}

STATIC
EFI_PARTITION_ENTRY *
FindGptEntryByName (
  IN GPT_DISK      *Disk,
  IN CONST CHAR16  *PartitionName)
{
  // Verify Parameters
  if (Disk == NULL || PartitionName == NULL) {
    return NULL;
  }

  // Go thru every Partition Entry
  for (UINTN i = 0; i < Disk->PrimaryHeader.NumberOfPartitionEntries; i++) {
    EFI_PARTITION_ENTRY *Entry = GetGptEntry (Disk, FALSE, i);

    // Skip unused Partition Entries
    if (IsGptEntryUnused (Entry)) {
      continue;
    }

    // Compare Partition Name
    if (IsPartitionNameEqual (Entry, PartitionName)) {
      return Entry;
    }
  }

  return NULL;
}

STATIC
BOOLEAN
FindSlotBEntry (
  IN  GPT_DISK            *Disks,
  IN  UINTN                DiskCount,
  IN  EFI_PARTITION_ENTRY *EntryA,
  OUT UINTN               *DiskIndexB,
  OUT UINTN               *EntryIndexB)
{
  // Verify Parameters
  if (Disks == NULL || EntryA == NULL || DiskIndexB == NULL || EntryIndexB == NULL) {
    return FALSE;
  }

  //
  // Go thru every Disk. The Slot B Partition can be Located on another
  // Storage than the Slot A Partition, like the XBL Partitions are.
  //
  for (UINTN i = 0; i < DiskCount; i++) {
    // Go thru every Partition Entry
    for (UINTN j = 0; j < Disks[i].PrimaryHeader.NumberOfPartitionEntries; j++) {
      EFI_PARTITION_ENTRY *EntryB = GetGptEntry (&Disks[i], FALSE, j);

      // Skip unused Partition Entries
      if (IsGptEntryUnused (EntryB)) {
        continue;
      }

      // Check A/B Partition Pair
      if (AreEntriesSlotPair (EntryA, EntryB)) {
        *DiskIndexB  = i;
        *EntryIndexB = j;

        return TRUE;
      }
    }
  }

  return FALSE;
}

STATIC
EFI_STATUS
DetectActiveSlotFromBoot (
  IN  GPT_DISK *Disks,
  IN  UINTN     DiskCount,
  OUT AB_SLOT  *ActiveSlot)
{
  // Verify Parameters
  if (Disks == NULL || ActiveSlot == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Go thru every Disk
  for (UINTN i = 0; i < DiskCount; i++) {
    EFI_PARTITION_ENTRY *BootA;
    EFI_PARTITION_ENTRY *BootB;
    BOOLEAN              BootAActive;
    BOOLEAN              BootBActive;

    // Find the Boot Partitions
    BootA = FindGptEntryByName (&Disks[i], L"boot_a");
    BootB = FindGptEntryByName (&Disks[i], L"boot_b");

    if (BootA == NULL || BootB == NULL) {
      continue;
    }

    // Get Active Slot Attributes
    BootAActive = (BOOLEAN)((BootA->Attributes & AB_SLOT_ACTIVE_ATTRIBUTE) != 0);
    BootBActive = (BOOLEAN)((BootB->Attributes & AB_SLOT_ACTIVE_ATTRIBUTE) != 0);

    // Verify Active Slot Attributes
    if (BootAActive == BootBActive) {
      return EFI_COMPROMISED_DATA;
    }

    // Pass Active Slot
    *ActiveSlot = BootAActive ? AbSlotA : AbSlotB;

    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
DetectActiveSlotFromPairs (
  IN  GPT_DISK *Disks,
  IN  UINTN     DiskCount,
  OUT AB_SLOT  *ActiveSlot)
{
  BOOLEAN FoundSlot     = FALSE;
  AB_SLOT CandidateSlot = AbSlotA;

  // Verify Parameters
  if (Disks == NULL || ActiveSlot == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Go thru every Disk
  for (UINTN i = 0; i < DiskCount; i++) {
    // Go thru every Partition Entry
    for (UINTN j = 0; j < Disks[i].PrimaryHeader.NumberOfPartitionEntries; j++) {
      EFI_PARTITION_ENTRY *EntryA = GetGptEntry (&Disks[i], FALSE, j);
      EFI_PARTITION_ENTRY *EntryB;
      BOOLEAN              EntryAActive;
      BOOLEAN              EntryBActive;
      AB_SLOT              PairSlot;
      UINTN                DiskIndexB;
      UINTN                EntryIndexB;

      // Check Slot A Partition
      if (IsGptEntryUnused (EntryA) || !DoesPartitionNameEndWithSlot (EntryA, L'a')) {
        continue;
      }

      // Find matching Slot B Partition
      if (!FindSlotBEntry (Disks, DiskCount, EntryA, &DiskIndexB, &EntryIndexB)) {
        continue;
      }

      // Get Slot B Partition Entry
      EntryB = GetGptEntry (&Disks[DiskIndexB], FALSE, EntryIndexB);

      // Get Active Slot Attributes
      EntryAActive = (BOOLEAN)((EntryA->Attributes & AB_SLOT_ACTIVE_ATTRIBUTE) != 0);
      EntryBActive = (BOOLEAN)((EntryB->Attributes & AB_SLOT_ACTIVE_ATTRIBUTE) != 0);

      // Skip Pairs without a clear Active Slot
      if (EntryAActive == EntryBActive) {
        continue;
      }

      // Get Active Slot of the Pair
      PairSlot = EntryAActive ? AbSlotA : AbSlotB;

      // Remember the first found Active Slot
      if (!FoundSlot) {
        CandidateSlot = PairSlot;
        FoundSlot     = TRUE;
        continue;
      }

      // Verify that every Pair uses the same Active Slot
      if (CandidateSlot != PairSlot) {
        return EFI_COMPROMISED_DATA;
      }
    }
  }

  // Verify Active Slot
  if (!FoundSlot) {
    return EFI_NOT_FOUND;
  }

  // Pass Active Slot
  *ActiveSlot = CandidateSlot;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
DetectActiveSlot (
  IN  GPT_DISK *Disks,
  IN  UINTN     DiskCount,
  OUT AB_SLOT  *ActiveSlot)
{
  EFI_STATUS Status;

  // Detect the Active Slot from the Boot Partitions
  Status = DetectActiveSlotFromBoot (Disks, DiskCount, ActiveSlot);
  if (!EFI_ERROR (Status)) {
    return Status;
  }

  // Detect the Active Slot from every A/B Partition Pair
  return DetectActiveSlotFromPairs (Disks, DiskCount, ActiveSlot);
}

STATIC
VOID
SetGptEntrySlotState (
  IN OUT EFI_PARTITION_ENTRY *Entry,
  IN     CONST EFI_GUID      *TypeGuid,
  IN     BOOLEAN              Active)
{
  // Verify Parameters
  if (Entry == NULL || TypeGuid == NULL) {
    return;
  }

  // Set Partition Type GUID
  CopyGuid (&Entry->PartitionTypeGUID, TypeGuid);

  //
  // Only Toggle the Active Slot Attribute. The Priority, Retry Count and
  // Successful Attributes are Managed by the Bootloader and Android.
  //
  if (Active) {
    Entry->Attributes |= AB_SLOT_ACTIVE_ATTRIBUTE;
  } else {
    Entry->Attributes &= ~AB_SLOT_ACTIVE_ATTRIBUTE;
  }
}

STATIC
EFI_STATUS
SwitchGptPair (
  IN OUT GPT_DISK *DiskA,
  IN     UINTN     EntryIndexA,
  IN OUT GPT_DISK *DiskB,
  IN     UINTN     EntryIndexB,
  IN     AB_SLOT   CurrentSlot,
  IN     AB_SLOT   TargetSlot)
{
  EFI_PARTITION_ENTRY *EntryA;
  EFI_PARTITION_ENTRY *EntryB;
  EFI_PARTITION_ENTRY *BackupEntryA;
  EFI_PARTITION_ENTRY *BackupEntryB;
  EFI_GUID             ActiveGuid;
  EFI_GUID             InactiveGuid;
  BOOLEAN              EntryAActive;
  BOOLEAN              EntryBActive;
  AB_SLOT              PairSlot;

  // Get the Primary and Backup Partition Entries
  EntryA       = GetGptEntry (DiskA, FALSE, EntryIndexA);
  EntryB       = GetGptEntry (DiskB, FALSE, EntryIndexB);
  BackupEntryA = GetGptEntry (DiskA, TRUE,  EntryIndexA);
  BackupEntryB = GetGptEntry (DiskB, TRUE,  EntryIndexB);

  if (EntryA == NULL || EntryB == NULL || BackupEntryA == NULL || BackupEntryB == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Verify that the Primary and Backup Partition Entries Match
  if (!AreEntriesSlotPair (EntryA, EntryB) ||
      !AreEntryNamesEqual (BackupEntryA, EntryA) ||
      !AreEntryNamesEqual (BackupEntryB, EntryB))
  {
    return EFI_COMPROMISED_DATA;
  }

  // Get Active Slot Attributes
  EntryAActive = (BOOLEAN)((EntryA->Attributes & AB_SLOT_ACTIVE_ATTRIBUTE) != 0);
  EntryBActive = (BOOLEAN)((EntryB->Attributes & AB_SLOT_ACTIVE_ATTRIBUTE) != 0);

  // Skip Pairs without an Active Slot
  if (!EntryAActive && !EntryBActive) {
    return EFI_NOT_READY;
  }

  // Verify that only one Slot is Active
  if (EntryAActive && EntryBActive) {
    return EFI_COMPROMISED_DATA;
  }

  // Verify that the Pair uses the Detected Active Slot
  PairSlot = EntryAActive ? AbSlotA : AbSlotB;
  if (PairSlot != CurrentSlot) {
    return EFI_COMPROMISED_DATA;
  }

  //
  // The Bootloader identifies the Partitions of the Active Slot by their Type GUID,
  // so both Type GUIDs have to be Swapped along with the Active Slot Attribute.
  //
  CopyGuid (&ActiveGuid,   EntryAActive ? &EntryA->PartitionTypeGUID : &EntryB->PartitionTypeGUID);
  CopyGuid (&InactiveGuid, EntryAActive ? &EntryB->PartitionTypeGUID : &EntryA->PartitionTypeGUID);

  // Update the Primary Partition Entries
  SetGptEntrySlotState (EntryA, (TargetSlot == AbSlotA) ? &ActiveGuid : &InactiveGuid, (BOOLEAN)(TargetSlot == AbSlotA));
  SetGptEntrySlotState (EntryB, (TargetSlot == AbSlotB) ? &ActiveGuid : &InactiveGuid, (BOOLEAN)(TargetSlot == AbSlotB));

  // Update the Backup Partition Entries
  SetGptEntrySlotState (BackupEntryA, (TargetSlot == AbSlotA) ? &ActiveGuid : &InactiveGuid, (BOOLEAN)(TargetSlot == AbSlotA));
  SetGptEntrySlotState (BackupEntryB, (TargetSlot == AbSlotB) ? &ActiveGuid : &InactiveGuid, (BOOLEAN)(TargetSlot == AbSlotB));

  // Mark the Disks as Modified
  DiskA->Modified = TRUE;
  DiskB->Modified = TRUE;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SwitchAllGptPairs (
  IN OUT GPT_DISK *Disks,
  IN     UINTN     DiskCount,
  IN     AB_SLOT   CurrentSlot,
  IN     AB_SLOT   TargetSlot,
  IN OUT UINTN    *PairCount)
{
  EFI_STATUS Status;

  // Verify Parameters
  if (Disks == NULL || PairCount == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Go thru every Disk
  for (UINTN i = 0; i < DiskCount; i++) {
    // Go thru every Partition Entry
    for (UINTN j = 0; j < Disks[i].PrimaryHeader.NumberOfPartitionEntries; j++) {
      EFI_PARTITION_ENTRY *EntryA = GetGptEntry (&Disks[i], FALSE, j);
      UINTN                DiskIndexB;
      UINTN                EntryIndexB;

      // Check Slot A Partition
      if (IsGptEntryUnused (EntryA) || !DoesPartitionNameEndWithSlot (EntryA, L'a')) {
        continue;
      }

      // Find matching Slot B Partition
      if (!FindSlotBEntry (Disks, DiskCount, EntryA, &DiskIndexB, &EntryIndexB)) {
        continue;
      }

      // Switch the A/B Partition Pair
      Status = SwitchGptPair (&Disks[i], j, &Disks[DiskIndexB], EntryIndexB, CurrentSlot, TargetSlot);
      if (Status == EFI_NOT_READY) {
        DEBUG ((EFI_D_INFO, "%a: Skipping A/B Partition Pair without an Active Slot!\n", __FUNCTION__));
        continue;
      }

      // Check for Errors
      if (EFI_ERROR (Status)) {
        return Status;
      }

      // Increase Partition Pair Count
      (*PairCount)++;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
UpdateGptHeaderCrc (
  IN OUT EFI_PARTITION_TABLE_HEADER *Header,
  IN     EFI_PARTITION_ENTRY        *Entries,
  IN     UINTN                       EntryArraySize)
{
  EFI_STATUS Status;
  UINT32     Crc;

  // Verify Parameters
  if (Header == NULL || Entries == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Calculate Partition Entry Array CRC32
  Header->PartitionEntryArrayCRC32 = 0;

  Status = gBS->CalculateCrc32 (Entries, EntryArraySize, &Crc);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Set Partition Entry Array CRC32
  Header->PartitionEntryArrayCRC32 = Crc;

  // Calculate GPT Header CRC32
  Header->Header.CRC32 = 0;

  Status = gBS->CalculateCrc32 (Header, Header->Header.HeaderSize, &Crc);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Set GPT Header CRC32
  Header->Header.CRC32 = Crc;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
WriteGptEntryArray (
  IN GPT_DISK                   *Disk,
  IN EFI_PARTITION_TABLE_HEADER *Header,
  IN EFI_PARTITION_ENTRY        *Entries)
{
  EFI_STATUS Status;
  UINT64     Offset;

  // Get Partition Entry Array Offset
  Status = GptLbaToOffset (Disk->BlockIo, Header->PartitionEntryLBA, &Offset);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Write Partition Entry Array
  return Disk->DiskIo->WriteDisk (Disk->DiskIo, Disk->BlockIo->Media->MediaId, Offset, Disk->EntryArraySize, Entries);
}

STATIC
EFI_STATUS
WriteGptHeader (
  IN GPT_DISK                   *Disk,
  IN EFI_PARTITION_TABLE_HEADER *Header)
{
  EFI_STATUS Status;
  UINT64     Offset;
  VOID      *Buffer;
  UINTN      BlockSize;

  // Get GPT Header Offset
  Status = GptLbaToOffset (Disk->BlockIo, Header->MyLBA, &Offset);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Get Block Size
  BlockSize = Disk->BlockIo->Media->BlockSize;

  // Allocate Block Buffer
  Buffer = AllocateZeroPool (BlockSize);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  // Copy GPT Header
  CopyMem (Buffer, Header, sizeof (*Header));

  // Write GPT Header
  Status = Disk->DiskIo->WriteDisk (Disk->DiskIo, Disk->BlockIo->Media->MediaId, Offset, BlockSize, Buffer);

  // Free Buffer
  FreePool (Buffer);

  return Status;
}

STATIC
EFI_STATUS
CommitGptDisk (
  IN GPT_DISK *Disk,
  IN BOOLEAN   RestoreOriginal)
{
  EFI_STATUS                 Status;
  EFI_PARTITION_TABLE_HEADER PrimaryHeader;
  EFI_PARTITION_TABLE_HEADER BackupHeader;
  EFI_PARTITION_ENTRY       *PrimaryEntries;
  EFI_PARTITION_ENTRY       *BackupEntries;

  // Verify Parameter
  if (Disk == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Copy the GPT Headers
  CopyMem (&PrimaryHeader, RestoreOriginal ? &Disk->OriginalPrimaryHeader : &Disk->PrimaryHeader, sizeof (PrimaryHeader));
  CopyMem (&BackupHeader,  RestoreOriginal ? &Disk->OriginalBackupHeader  : &Disk->BackupHeader,  sizeof (BackupHeader));

  // Get the Partition Entry Arrays
  PrimaryEntries = RestoreOriginal ? Disk->OriginalPrimaryEntries : Disk->PrimaryEntries;
  BackupEntries  = RestoreOriginal ? Disk->OriginalBackupEntries  : Disk->BackupEntries;

  // Update the GPT Header CRC32s
  Status = UpdateGptHeaderCrc (&PrimaryHeader, PrimaryEntries, Disk->EntryArraySize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UpdateGptHeaderCrc (&BackupHeader, BackupEntries, Disk->EntryArraySize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Update the Backup GPT first, so that the Primary GPT stays valid,
  // if the Storage can't be Written completely.
  //
  Status = WriteGptEntryArray (Disk, &BackupHeader, BackupEntries);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WriteGptHeader (Disk, &BackupHeader);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Update the Primary GPT
  Status = WriteGptEntryArray (Disk, &PrimaryHeader, PrimaryEntries);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WriteGptHeader (Disk, &PrimaryHeader);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Flush the Storage
  return Disk->BlockIo->FlushBlocks (Disk->BlockIo);
}

STATIC
EFI_STATUS
CommitModifiedGptDisks (
  IN GPT_DISK *Disks,
  IN UINTN     DiskCount)
{
  EFI_STATUS Status;

  // Go thru every Disk
  for (UINTN i = 0; i < DiskCount; i++) {
    // Skip unmodified Disks
    if (!Disks[i].Modified) {
      continue;
    }

    // Write the updated Partition Tables
    Status = CommitGptDisk (&Disks[i], FALSE);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
VOID
RollbackModifiedGptDisks (
  IN GPT_DISK *Disks,
  IN UINTN     DiskCount)
{
  // Go thru every Disk
  for (UINTN i = 0; i < DiskCount; i++) {
    // Skip unmodified Disks
    if (!Disks[i].Modified) {
      continue;
    }

    // Restore the Original Partition Tables
    if (EFI_ERROR (CommitGptDisk (&Disks[i], TRUE))) {
      DEBUG ((EFI_D_ERROR, "%a: Failed to Roll Back the Partition Tables of Storage %u!\n", __FUNCTION__, i));
    }
  }
}

STATIC
EFI_STATUS
SetUfsBootLu (IN AB_SLOT TargetSlot)
{
  EFI_STATUS                 Status;
  EFI_MEM_CARDINFO_PROTOCOL *CardInfoProtocol = NULL;
  UINT32                     BootLu;

  // Locate Card Info Protocol
  Status = gBS->LocateProtocol (&gEfiMemCardInfoProtocolGuid, NULL, (VOID **)&CardInfoProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Locate Card Info Protocol! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  // Get Boot LUN of the Target Slot
  BootLu = (TargetSlot == AbSlotA) ? UFS_BOOT_LU_SLOT_A : UFS_BOOT_LU_SLOT_B;

  // Set Boot LUN
  Status = CardInfoProtocol->SetBootLU (CardInfoProtocol, BootLu);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Set the UFS Boot LUN %u! Status = %r\n", __FUNCTION__, BootLu, Status));
  }

  return Status;
}

STATIC
VOID
PrepareScreen ()
{
  // Reset Console Input
  gST->ConIn->Reset (gST->ConIn, FALSE);

  // Clear Screen
  gST->ConOut->SetAttribute (gST->ConOut, EFI_LIGHTGRAY | EFI_BACKGROUND_BLACK);
  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  // Print Title
  Print (L"\n  Switch Boot Slot\n\n");
}

STATIC
EFI_INPUT_KEY
WaitForKey ()
{
  EFI_INPUT_KEY Key   = {0};
  UINTN         Index = 0;

  // Wait for a Keypress
  gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);

  // Get the pressed Key
  gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);

  return Key;
}

STATIC
CHAR16 *
GetSlotName (IN AB_SLOT Slot)
{
  return (Slot == AbSlotA) ? L"A" : L"B";
}

EFI_STATUS
EFIAPI
SwitchSlotEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS    Status;
  EFI_INPUT_KEY Key;
  GPT_DISK     *Disks       = NULL;
  UINTN         DiskCount   = 0;
  UINTN         PairCount   = 0;
  AB_SLOT       CurrentSlot = AbSlotA;
  AB_SLOT       TargetSlot  = AbSlotB;

  // Disable Watchdog Timer
  gBS->SetWatchdogTimer (0, 0, 0, (CHAR16 *)NULL);

  // Prepare Screen
  PrepareScreen ();

  // Load the Partition Tables of every Storage
  Status = LoadGptDisks (&Disks, &DiskCount);
  if (EFI_ERROR (Status)) {
    Print (L"  Failed to Load the Partition Tables! Status = %r\n", Status);
    goto exit;
  }

  // Detect the Active Slot
  Status = DetectActiveSlot (Disks, DiskCount, &CurrentSlot);
  if (EFI_ERROR (Status)) {
    Print (L"  Failed to Detect the Active Slot! Status = %r\n", Status);
    goto exit;
  }

  // Get Target Slot
  TargetSlot = (CurrentSlot == AbSlotA) ? AbSlotB : AbSlotA;

  // Switch every A/B Partition Pair in Memory
  Status = SwitchAllGptPairs (Disks, DiskCount, CurrentSlot, TargetSlot, &PairCount);
  if (EFI_ERROR (Status)) {
    Print (L"  Failed to Switch the A/B Partitions! Status = %r\n", Status);
    goto exit;
  }

  // Verify Partition Pair Count
  if (PairCount == 0) {
    Print (L"  No switchable A/B Partitions were found!\n");

    Status = EFI_NOT_FOUND;
    goto exit;
  }

  // Print Slot Info
  Print (L"  Current Slot   : %s\n", GetSlotName (CurrentSlot));
  Print (L"  Target Slot    : %s\n", GetSlotName (TargetSlot));
  Print (L"  A/B Partitions : %u\n\n", PairCount);

  // Ask for Confirmation
  Print (L"  [Volume Up]   Switch to Slot %s and Reboot\n", GetSlotName (TargetSlot));
  Print (L"  [Volume Down] Cancel\n");

  while (TRUE) {
    // Get the pressed Key
    Key = WaitForKey ();

    // Check the pressed Key
    if (Key.ScanCode == SCAN_UP) {
      break;
    } else if (Key.ScanCode == SCAN_DOWN || Key.ScanCode == SCAN_ESC) {
      Print (L"\n  Cancelled.\n");

      Status = EFI_ABORTED;
      goto exit;
    }
  }

  // Print Progress
  Print (L"\n  Updating the Partition Tables...\n");

  // Write the updated Partition Tables
  Status = CommitModifiedGptDisks (Disks, DiskCount);
  if (EFI_ERROR (Status)) {
    Print (L"  Failed to Update the Partition Tables! Status = %r\n", Status);
    Print (L"  Rolling back...\n");

    RollbackModifiedGptDisks (Disks, DiskCount);

    goto exit;
  }

  // Set the UFS Boot LUN of the Target Slot
  Status = SetUfsBootLu (TargetSlot);
  if (EFI_ERROR (Status)) {
    Print (L"  Failed to Set the Boot LUN! Status = %r\n", Status);
    Print (L"  Rolling back...\n");

    RollbackModifiedGptDisks (Disks, DiskCount);

    goto exit;
  }

  // Print Result
  Print (L"\n  Slot %s is now Active. Rebooting...\n", GetSlotName (TargetSlot));

  // Free the Disks
  FreeGptDisks (Disks, DiskCount);

  // Wait 2s
  gBS->Stall (2000000);

  // Reboot Device
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);

  return EFI_SUCCESS;

exit:
  // Free the Disks
  FreeGptDisks (Disks, DiskCount);

  // Wait for a Keypress, if an Error occurred
  if (EFI_ERROR (Status) && Status != EFI_ABORTED) {
    Print (L"\n  Press any Key to Return.\n");

    WaitForKey ();
  }

  return Status;
}
