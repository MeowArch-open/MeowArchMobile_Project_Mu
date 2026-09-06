/**
  DPU register snapshot, taken while the display is still exactly as ABL left it.

  Why this driver exists
  ----------------------
  On zorn the panel shows correct colour under ABL/SimpleFbDxe, but mainline
  Linux's drm/msm renders saturated red and blue wrong (red washes out to grey,
  blue shifts to violet) while green, white and everything unsaturated is fine.
  Eleven hypotheses were tested and ruled out on the Linux side. What was never
  obtainable is the one thing that would settle it: the DPU configuration that
  *works*.

  It cannot be read from Linux. By the time Linux is up, MDSS is powered down --
  reading MDSS_HW_VERSION there returns 0x00000000 instead of 0xa0000000, and
  touching the other blocks faults hard enough to reboot the SoC. The panel keeps
  showing its last frame out of its own GRAM, which is why the screen still looks
  alive and why writing /dev/fb0 changes nothing.

  UEFI is the only place where that configuration is still live: SimpleFbDxe can
  paint precisely because ABL's DPU is still running. So take the snapshot here,
  while it is.

  Getting the data out
  --------------------
  Straight into the DEBUG log, which FrameBufferSerialPortLib mirrors into the
  "Silicium_Log" region at 0xFEEF0000 -- 64 KiB of reserved memory that neither
  XBL nor Linux touches. So the snapshot can be read back from /dev/mem after
  the fact: no variable service, no new carve-out, no efivarfs.

  It is a ring, so the snapshot is taken twice per boot and tagged: "DPU0" at
  load time, "DPUS" at ReadyToBoot. Either one can be the pass that survives, and
  they answer different questions -- see DumpAllRegions. The tag is also what
  greps a pass out of an otherwise noisy log:

    DPUS 0ae56700: 00020000 00000000 00000000 00000000

  Safety
  ------
  DXE has no exception handling; a bad MMIO access is an unbootable phone. Two
  guards, in this order:

    1. The MDSS register window is not in the platform memory map at all (it
       falls in the gap between USB at 0x0A600000 and AOSS at 0x0B000000), so it
       is added to the GCD here. If that fails, nothing is read.
    2. MDSS_HW_VERSION is read first. A powered block reports 0xa0000000 on this
       SoC. Anything else -- 0 or all-ones -- means the block is dark, and then
       not one further register is touched.

  Everything after those two checks is a plain read of a live block. The driver
  never writes a DPU register.
**/

#include <PiDxe.h>
#include <Uefi.h>

#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>

//
// MDSS register window. Not present in the platform memory map, so this driver
// adds it to the GCD itself. 0x0AE00000 .. 0x0AF00000 covers everything that
// matters: mdss top, mdp (+0x1000), the DSPP/LM/CTL/DSC blocks, DSI0 at
// +0x94000 and the DSI PHY at +0x95000.
//
#define MDSS_BASE                 0x0AE00000
#define MDSS_SIZE                 0x00100000

//
// Hard-wired constant on this SoC. A live block reports 0xa0000000; a block
// whose clocks are gated reads back 0x00000000 (observed on the Linux side, see
// the file header). Only the major version nibbles are checked, so a minor
// revision difference does not veto an otherwise live block.
//
#define MDSS_HW_VERSION           (MDSS_BASE + 0x0)
#define MDSS_HW_VERSION_EXPECTED  0xA0000000
#define MDSS_HW_VERSION_MASK      0xFF000000

#define DPU_TAG_EARLY             "DPU0"
#define DPU_TAG_LATE              "DPUS"

typedef struct {
  CONST CHAR8 *Name;
  UINT32       Base;    // absolute MMIO address
  UINT32       Length;  // bytes, must be a multiple of 4
} DPU_REGION;

//
// What to capture, and why each one is here. All addresses are absolute and
// were verified by reading the same registers from a running Android on this
// device, so they can be diffed field by field against a mainline capture.
//
// The DSPP colour blocks are the reason this list is not just the DSC block.
// Four of them can change hue and saturation without touching luminance, which
// is the exact shape of the symptom, and mainline drm/msm programs none of them:
//
//   PA HSIC    +0x800   hue / saturation / intensity / contrast
//   SIXZONE    +0x900   per-hue-zone saturation, the closest fit of all
//   GAMUT      +0x1000  3D LUT
//   PCC        +0x1700  3x3 colour matrix
//
// Offsets confirmed against sm8650_dspp_sblk in dpu_hw_catalog.c (pcc 0x1700,
// gc 0x17c0, spr 0x15400) and dspp_0/dspp_1 at 0x54000/0x56000 relative to
// mdp = mdss + 0x1000. So anything ABL set here is invisible from the Linux
// side and shows up in a diff against a mainline capture.
//
STATIC CONST DPU_REGION mDpuRegions[] = {
  { "MDSS",          0x0AE00000, 0x010 },  // HW_VERSION and friends
  { "MDP_TOP",       0x0AE01000, 0x040 },
  { "DSPP_TOP",      0x0AE01300, 0x080 },  // +0x50 carries the real panel width
  { "CTL_0",         0x0AE16000, 0x060 },  // which blocks are in the data path
  { "CTL_1",         0x0AE17000, 0x060 },
  { "LM_0",          0x0AE45000, 0x040 },
  { "LM_1",          0x0AE46000, 0x040 },
  // { "DSPP_0",        0x0AE55000, 0x080 },  // DSPP triggers hard reset when accessed
  // { "DSPP_0_PA",     0x0AE55800, 0x200 },
  // { "DSPP_0_GAMUT",  0x0AE56000, 0x040 },
  // { "DSPP_0_PCC",    0x0AE56700, 0x150 },
  // { "DSPP_1",        0x0AE57000, 0x080 },
  // { "DSPP_1_PA",     0x0AE57800, 0x200 },
  // { "DSPP_1_GAMUT",  0x0AE58000, 0x040 },
  // { "DSPP_1_PCC",    0x0AE58700, 0x150 },
  //
  // SSPP, the source pipes. Never captured before, and the reason this list grew:
  // the DSC block is now known to be configured identically under ABL and under
  // mainline (MAIN_CONF 0x205402ca, DSC_CFG 0x3501, DATA_IN_SWAP 0x0002c688 on
  // both) while ABL renders blue text as blue and mainline renders it white. A
  // difference that survives an identical DSC configuration has to be upstream of
  // DSC, and SSPP is where the pixels enter: its format register decides the
  // fetch layout and the component order handed to the layer mixer.
  //
  // Only the head of each pipe is read. The full block is 0x344 bytes, but the
  // interesting registers -- SSPP_SRC_FORMAT, SRC_UNPACK_PATTERN, SRC_OP_MODE,
  // SRC_SIZE/XY, OUT_SIZE/XY -- live in the first 0x60, and a shorter read keeps
  // the whole snapshot inside the 64 KiB log ring.
  //
  // Which pipe is live is not known in advance, so all six DMA pipes and the four
  // VIG pipes are read; a pipe that is not fetching reads back zero or its reset
  // value, which is itself the answer to "which one is ABL using".
  //
  // Unlike DSPP, these cannot be dark while the panel is being scanned out: they
  // are the data path. DSPP is a post-processing block that ABL leaves powered
  // down, which is why reading it hard-resets the SoC -- see the note above.
  //
  // The catalog bases these off MDP, which is mdss + 0x1000, so sspp_8's
  // .base = 0x24000 is absolute 0x0AE25000 and not 0x0AE24000. Reading the
  // latter black-screened the phone: it lands in the gap before the block, and
  // an access there is unclocked. Android's own debugfs agrees -- it rejects
  // offset 0x24000 and accepts 0x25000, returning live data. The same +0x1000
  // holds for every block here: DSC's catalog base 0x80000 plus sblk 0x100 is
  // the 0x0AE81100 that has been read successfully all along.
  { "SSPP_VIG0",     0x0AE05000, 0x060 },
  { "SSPP_VIG1",     0x0AE07000, 0x060 },
  { "SSPP_VIG2",     0x0AE09000, 0x060 },
  { "SSPP_VIG3",     0x0AE0B000, 0x060 },
  { "SSPP_DMA0",     0x0AE25000, 0x060 },
  { "SSPP_DMA1",     0x0AE27000, 0x060 },
  { "SSPP_DMA2",     0x0AE29000, 0x060 },
  { "SSPP_DMA3",     0x0AE2B000, 0x060 },
  { "SSPP_DMA4",     0x0AE2D000, 0x060 },
  { "SSPP_DMA5",     0x0AE2F000, 0x060 },

  //
  // CTL_0 in full. The earlier 0x60-byte read already showed ABL and Android
  // disagreeing about how layers reach the mixer (CTL_LAYER(LM_0) is 0x0100002d
  // under ABL and 0x09e00000 under Android), and the blend-stage extension
  // registers that resolve that live at +0x40 and beyond.
  //
  { "CTL_0_FULL",    0x0AE16000, 0x0A0 },

  //
  // INTF_1, never captured before. This is the block between DSC and the DSI
  // link, and INTF_CONFIG2 at +0x60 holds the two bits that decide how the
  // compressed stream is framed:
  //
  //   BIT(0)  DATABUS_WIDEN        48 bits per pclk instead of 24
  //   BIT(4)  DATA_HCTL_EN
  //   BIT(12) DCE_DATA_COMPRESS    the interface is carrying DSC output
  //
  // mainline sets DATABUS_WIDEN whenever there is a DSC and the DSI is 6G
  // >= v2.5.0, which is unconditionally true here, so it has never been possible
  // to tell from the Linux side whether ABL agrees. Catalog base for intf_1 is
  // 0x35000 MDP-relative, hence 0x0AE36000 absolute (mdss + 0x1000 + 0x35000).
  //
  { "INTF_1",        0x0AE36000, 0x080 },

  //
  // PINGPONG_0/1. The DSC blocks bind to a pingpong (DSC_CTL's mux field), and
  // the frame-done timeout seen when CDM was in the path was a pingpong timeout,
  // so its configuration is worth having alongside. Catalog pingpong_0 base is
  // 0x69000 MDP-relative -> 0x0AE6A000; the SPR sub-block at +0x400 is the
  // SPR_0 already in this list, which cross-checks the offset.
  //
  { "PINGPONG_0",    0x0AE6A000, 0x040 },
  { "PINGPONG_1",    0x0AE6B000, 0x040 },

  { "SPR_0",         0x0AE6A400, 0x100 },  // pentile sub-pixel rendering
  { "SPR_1",         0x0AE6B400, 0x100 },
  { "DSC_0_ENC0",    0x0AE81100, 0x0A0 },
  { "DSC_0_ENC1",    0x0AE81200, 0x0A0 },
  { "DSC_0_CTL",     0x0AE81F00, 0x040 },
  //
  // DSI0 out to 0x300, not 0x100. The compression-mode registers are at +0x2a4
  // (COMMAND_COMPRESSION_MODE_CTRL) and +0x2a8 (its slice widths) in mainline's
  // numbering, and they say how the DSC stream is framed into DSI packets --
  // outside the 0x100 the earlier snapshot read.
  //
  { "DSI0_CTRL",     0x0AE94000, 0x300 },
};

/**
  Put the MDSS register window in the page tables.

  The platform memory map has no entry for it, so a read would fault without
  this. Adding it to the GCD is the whole point of the driver's first guard: if
  this does not succeed, no register is touched.
**/
STATIC
EFI_STATUS
MapMdssWindow (
  VOID
  )
{
  EFI_STATUS                      Status;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Descriptor;

  Status = gDS->GetMemorySpaceDescriptor (MDSS_BASE, &Descriptor);
  if (!EFI_ERROR (Status) &&
      Descriptor.GcdMemoryType == EfiGcdMemoryTypeMemoryMappedIo)
  {
    //
    // Already owned by somebody -- a stock display driver, or a memory map
    // entry added later. Use it as it stands and leave it alone on the way out.
    //
    return EFI_SUCCESS;
  }

  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeMemoryMappedIo,
                  MDSS_BASE,
                  MDSS_SIZE,
                  EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DpuSnapshot: AddMemorySpace: %r\n", Status));
    return Status;
  }

  //
  // AddMemorySpace only records the range. This is the call that maps it; skip
  // it and every read below faults just as if the range had never been added.
  //
  Status = gDS->SetMemorySpaceAttributes (MDSS_BASE, MDSS_SIZE, EFI_MEMORY_UC);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DpuSnapshot: SetMemorySpaceAttributes: %r\n", Status));
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  Read one region and print it, 16 bytes per line.

  @return  The sum of every word read, so the caller can print a total that
           makes a truncated log obvious.
**/
STATIC
UINT32
DumpRegion (
  IN CONST DPU_REGION *Region,
  IN CONST CHAR8      *Tag
  )
{
  UINT32 Offset;
  UINT32 Sum;

  //
  // Every entry in the table is a multiple of 16 so a line is always full.
  // Refuse anything else rather than reading past the end of a region.
  //
  if ((Region->Length == 0) || ((Region->Length % 16) != 0)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a skip %a: length 0x%x is not a multiple of 16\n",
      Tag,
      Region->Name,
      Region->Length
      ));
    return 0;
  }

  DEBUG ((
    DEBUG_ERROR,
    "%a === %a @ 0x%08x len 0x%x\n",
    Tag,
    Region->Name,
    Region->Base,
    Region->Length
    ));

  Sum = 0;

  for (Offset = 0; Offset < Region->Length; Offset += 16) {
    UINT32 Word0 = MmioRead32 (Region->Base + Offset + 0x0);
    UINT32 Word1 = MmioRead32 (Region->Base + Offset + 0x4);
    UINT32 Word2 = MmioRead32 (Region->Base + Offset + 0x8);
    UINT32 Word3 = MmioRead32 (Region->Base + Offset + 0xC);

    Sum += Word0 + Word1 + Word2 + Word3;

    DEBUG ((
      DEBUG_ERROR,
      "%a %08x: %08x %08x %08x %08x\n",
      Tag,
      Region->Base + Offset,
      Word0,
      Word1,
      Word2,
      Word3
      ));
  }

  return Sum;
}

/**
  Read every region and print it, tagged so one pass can be told from the other.

  The snapshot is taken twice per boot, for two different failure modes:

    DPU0, at load time -- the display is certainly still ABL's here, because the
    boot log being painted on the panel is proof the DPU is scanning out. But the
    log is a 64 KiB ring, so a long boot can overwrite this pass.

    DPUS, at ReadyToBoot -- lands near the tail of the ring where it survives,
    and is the state the kernel actually inherits. Whatever powers MDSS down
    before Linux has never been pinned down, so this pass may find it already
    dark; that is what the guard below reports.

  Neither pass is redundant: the pair also answers whether anything inside UEFI
  touched the DPU, which nothing is supposed to with DisplayDxe out of the build.
**/
STATIC
VOID
DumpAllRegions (
  IN CONST CHAR8 *Tag
  )
{
  UINT32 HwVersion;
  UINT32 Sum;
  UINTN  Index;

  //
  // Guard 2: the first read is the one that tells us whether reading is safe.
  // MDSS_HW_VERSION is hard-wired, so a live block cannot report anything else.
  // If this comes back 0 or all-ones the block is dark and every other register
  // in it would fault, so stop here.
  //
  HwVersion = MmioRead32 (MDSS_HW_VERSION);
  if ((HwVersion & MDSS_HW_VERSION_MASK) !=
      (MDSS_HW_VERSION_EXPECTED & MDSS_HW_VERSION_MASK))
  {
    DEBUG ((
      DEBUG_ERROR,
      "%a ABORT: MDSS is dark, HW_VERSION = 0x%08x (expected 0x%08x)\n",
      Tag,
      HwVersion,
      MDSS_HW_VERSION_EXPECTED
      ));
    return;
  }

  DEBUG ((
    DEBUG_ERROR,
    "%a BEGIN v1 HW_VERSION=0x%08x regions=%d\n",
    Tag,
    HwVersion,
    (UINTN)ARRAY_SIZE (mDpuRegions)
    ));

  Sum = 0;
  for (Index = 0; Index < ARRAY_SIZE (mDpuRegions); Index++) {
    Sum += DumpRegion (&mDpuRegions[Index], Tag);
  }

  //
  // The sum is here so a log that got cut off short can be told apart from a
  // complete one without counting lines by hand.
  //
  DEBUG ((DEBUG_ERROR, "%a END sum=0x%08x\n", Tag, Sum));
}

/**
  ReadyToBoot handler. Fires once; closing the event first keeps a second boot
  attempt from printing the whole thing again.
**/
STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  gBS->CloseEvent (Event);

  DumpAllRegions (DPU_TAG_LATE);
}

/**
  Map the register window, snapshot it, and arm the second snapshot.

  Always returns EFI_SUCCESS: this is a diagnostic, and there is no failure here
  worth reporting up a boot path that would otherwise be fine.
**/
EFI_STATUS
EFIAPI
DpuSnapshotEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_EVENT  Event;

  //
  // Guard 1: the window has to be mapped before it can be read at all.
  //
  Status = MapMdssWindow ();
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a ABORT: MDSS window not mapped (%r), no registers read\n",
      DPU_TAG_EARLY,
      Status
      ));
    return EFI_SUCCESS;
  }

  DumpAllRegions (DPU_TAG_EARLY);

  Status = EfiCreateEventReadyToBootEx (
             TPL_CALLBACK,
             OnReadyToBoot,
             NULL,
             &Event
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a ReadyToBoot event: %r, so the early pass is all there will be\n",
      DPU_TAG_EARLY,
      Status
      ));
  }

  return EFI_SUCCESS;
}

