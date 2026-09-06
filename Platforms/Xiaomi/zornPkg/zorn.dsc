##
#
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = zorn
  PLATFORM_GUID                  = 1A035EA1-4F66-483E-9697-6571FF0A4190
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/zornPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = zornPkg/zorn.fdf
  #
  # 1 = the stock QcomPkg DisplayDxe drives the panel itself; 0 = SimpleFbDxe just
  # paints into whatever framebuffer ABL left behind.
  #
  # At 1 the screen goes black the moment DisplayDxe loads: "Loading driver
  # DisplayDxe <addr>" is the last thing visible. That is expected either way,
  # because the debug log *is* the framebuffer (SerialPortLib is
  # FrameBufferSerialPortLib), so whoever takes the display takes the log with it.
  # FrameBufferSerialPortLib now mirrors every byte into the "UEFI_Log" region as
  # well, so read work/tmp/read-uefi-log.py after a black boot to see what
  # DisplayDxe actually said. DEBUG builds only -- RELEASE has both the serial port
  # and the debug lib set to Null.
  #
  # Nothing is missing on our side: DXE.inc and APRIORI.inc gate DisplayDxe and
  # CPRDxe on this flag, Binaries/zorn/QcomPkg/Drivers/DisplayDxe is extracted, and
  # RAW.inc already carries both O11 panel XMLs (the one this device uses is
  # Panel_O11_42_02_0a_amoled_dsc_cmd.xml at GUID
  # CAAB3E2D-3009-49ED-B869-1917522632E2) together with their sprconfig files. What
  # is missing is a precedent: every platform here where the stock display driver
  # works is an older generation -- aston is Kailua, gts8p Waipio, enchilada Napali,
  # b4q Palima -- and the only other Lanai device, giulia, also leaves this at 0.
  #
  # zorn's panel is DSC compressed and *command mode*, so it does not scan out by
  # itself and something has to kick the DPU for every frame. That is why the screen
  # feels like it is being flushed, and fixing it for good means owning that kick in
  # Linux (drm/msm DPU + DSI + an O11 panel driver), not in UEFI.
  #
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  #
  # 0 = SM8650-AA
  # 1 = SM8650-Q-AA
  # 2 = SM8650-AB
  # 3 = SM8650-Q-AB
  # 4 = SM8650-AC
  #
  SOC_TYPE                       = 2

!include LanaiPkg/LanaiPkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # Quieter DEBUG log: ERROR | WARN | LOAD (SiliciumPkg.dsc.inc sets 0x8007EE0F).
  # Keeps "Loading driver at ..." and "[Bds]Booting ..." - both are DEBUG_LOAD - while dropping
  # the ConvertRange / AllocatePool / GCD flood. The framebuffer console is the only console we
  # have and rendering it is slow, so this measurably shortens the time to the OS loader.
  #
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000006

  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"XiaoMi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"zorn"
!if $(DEVICE_MODEL) == 0
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"K80"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"zorn"
!elseif $(DEVICE_MODEL) == 1
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"zorn"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"zorn"
!endif
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"zorn"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1440
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|3200
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|zornPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|zornPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf
