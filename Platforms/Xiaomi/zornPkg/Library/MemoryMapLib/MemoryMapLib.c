#include <Library/MemoryMapLib.h>

STATIC
EFI_MEMORY_REGION_DESCRIPTOR
gMemoryDescriptor[] = {
  // Name, Address, Length, HobOption, ResourceType, ResourceAttribute, MemoryType, ArmAttribute

  // DDR Regions
{"RSRV0",             0x816E0000, 0x00320000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
{"XBL_DT",            0x81A00000, 0x00040000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
{"Minidump_Regions",  0x81BFF000, 0x00001000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
{"AOP",               0x81C00000, 0x000A0000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
{"UEFI_Log",          0x81CE4000, 0x00010000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
{"SMEM",              0x81D00000, 0x00200000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
{"PvmFw",             0x824A0000, 0x00100000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
{"XBL_Ramdump",       0x84800000, 0x00200000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
{"PIL_Reserved",      0x8BC00000, 0x16F80000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
{"Display_Demura",    0xA3600000, 0x02B00000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
{"DBI_Dump",          0xA6100000, 0x00F00000, NoHob, MMAP_IO, INITIALIZED, Conv, UNCACHED_UNBUFFERED_XN},
{"FD_Reserved",       0xA7000000, 0x00100000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
{"UEFI_FD",           0xA7100000, 0x00300000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
{"UEFI_FD_Reserved",  0xA7400000, 0x00200000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
{"CPU_Vectors",       0xA7600000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
{"Info_Blk",          0xA7601000, 0x00001000, AddMem, SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
{"MMU_PageTables",    0xA7602000, 0x00003000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
{"Log_Buffer",        0xA7605000, 0x00008000, AddMem, SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
{"UEFI_Stack",        0xA760D000, 0x00040000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
{"SEC_Heap",          0xA764D000, 0x0008C000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
{"Sched_Heap",        0xA76D9000, 0x00400000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
{"FV_Region",         0xA7AD9000, 0x00400000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
{"UEFI_RESV",         0xA7ED9000, 0x00127000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
{"Kernel",            0xA8000000, 0x10000000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
{"DXE_Heap",          0xB8000000, 0x1BA00000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK_XN},
{"TZApps_Reserved",   0xD8800000, 0x1B000000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
//
// The last 64 KiB of the display carve-out is handed to FrameBufferSerialPortLib
// as a debug log that outlives a reset. "UEFI_Log" at 0x81CE4000 cannot be used
// for that: it belongs to XBL, which rewrites it ("Format: ...") on the very next
// boot, so a UEFI that resets itself loses its own last words. Nothing writes up
// here -- the frame buffer needs 1440*3200*4 = 0x1194000 of the 0x2AF0000 that is
// left, and demura has its own region.
//
{"Display_Reserved",  0xFC400000, 0x02AF0000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
{"Silicium_Log",      0xFEEF0000, 0x00010000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},


// Register Regions
{"IMEM_Base",         0x14680000, 0x0002A000, NoHob, MMAP_IO, INITIALIZED, Conv, NS_DEVICE},
{"IMEM_Cookie_Base",  0x146AA000, 0x00016000, AddDev, MMAP_IO, INITIALIZED, Conv, NS_DEVICE},

{"IPC_ROUTER_TOP",    0x00400000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"SECURITY_CONTROL",  0x00780000, 0x00007000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"QUP",               0x00800000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"PRNG_CFG_PRNG",     0x010C0000, 0x0000C000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"CRYPTO0_CRYPTO",    0x01DC0000, 0x00040000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"CLK_TCSR_TCSR_REGS",0x01F00000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"PERIPH_SS",         0x08800000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"USB",               0x0A600000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
//
// The DPU/DSI register window, which was simply missing: nothing in this build
// touched it, because SimpleFbDxe only writes the frame buffer and the DPU that
// scans it out is still the one ABL configured. DpuSnapshotDxe does read it, and
// falls back to adding this range to the GCD itself if it is absent -- but that
// is one more thing to go wrong on a phone with no exception handling, so
// declare it here like every other register block. 1 MiB reaches DSI0 at
// +0x94000 and the DSI PHY at +0x95000.
//
{"MDSS",              0x0AE00000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"AOSS",              0x0B000000, 0x04000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"TLMM",              0x0F000000, 0x01000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"SMMU",              0x15000000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"APSS_HM",           0x17000000, 0x02000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  // Pass Data
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
