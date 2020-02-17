#include <Uefi.h>
#include <Uefi/UefiMultiPhase.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/BlockIo2.h>
#include <Protocol/SdMmcPassThru.h>

UINT8 WriteBuffer[512] = {1, 2, 5, 3, 4, 2, 1, 5, 0, 11};

EFI_SYSTEM_TABLE  *gST;

typedef struct {
  EFI_BLOCK_IO2_PROTOCOL  *BlockIo2;
  EFI_BLOCK_IO2_TOKEN     Token;
  EFI_LBA                 Lba;
  UINT8                   Buffer[512];
} RW_EVENT_DESC;

VOID
ReadEventFinishedCallback (
  IN EFI_EVENT  *Event,
  IN VOID       *Context
  )
{
  RW_EVENT_DESC  *EventDesc;
  UINT32         Index;
  EFI_STATUS     Status;

  EventDesc = (RW_EVENT_DESC*) Context;

  Status = gBS->CloseEvent (
                  Event
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "Failed to close event\n"));
  }

  DEBUG ((DEBUG_INFO, "Dumping read buffer\n"));
  for (Index = 0; Index < 10; Index++) {
    DEBUG ((DEBUG_INFO, " %d", EventDesc->Buffer[Index]));
  }
  DEBUG ((DEBUG_INFO, "\n"));
}

VOID
WriteEventFinishedCallback (
  IN EFI_EVENT  *Event,
  IN VOID       *Context
  )
{
  EFI_STATUS           Status;
  RW_EVENT_DESC        *EventDesc;

  EventDesc = (RW_EVENT_DESC*) Context;

  DEBUG((DEBUG_INFO, "Write Event done\n"));

  Status = gBS->CloseEvent (
                  Event
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "Failed to close event\n"));
  }

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  ReadEventFinishedCallback,
                  EventDesc,
                  &EventDesc->Token.Event
                  );
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_INFO, "Failed to create read event\n"));
    return;
  }

  ZeroMem (&EventDesc->Buffer, 512);
  Status = EventDesc->BlockIo2->ReadBlocksEx (
                                  EventDesc->BlockIo2,
                                  EventDesc->BlockIo2->Media->MediaId,
                                  EventDesc->Lba,
                                  &EventDesc->Token,
                                  512,
                                  &EventDesc->Buffer
                                  );
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_INFO, "Failed to read blocks, %r\n", Status));
  }
}

VOID
FindEmmcBlockIo (
  OUT EFI_BLOCK_IO2_PROTOCOL  **BlockIo2
  )
{
  EFI_HANDLE  *HandleBuffer;
  UINTN       NoHandles;
  EFI_STATUS  Status;
  UINTN       EmmcBlkIoHandle;

  // Assume eMMC is located on block IO number 0.

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIo2ProtocolGuid,
                  NULL,
                  &NoHandles,
                  &HandleBuffer
                  );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "Couldn't find BlockIo2 handle buffer\n"));
    return;
  }

  if (NoHandles > 0) {
    EmmcBlkIoHandle = 0;
  } else {
    return;
  }
  Status = gBS->HandleProtocol (
                  HandleBuffer[EmmcBlkIoHandle],
                  &gEfiBlockIo2ProtocolGuid,
                  BlockIo2
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "Can't open BlockIo2 on handle\n"));
  }
}

EFI_STATUS
TestAppEntry (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_BLOCK_IO2_PROTOCOL  *BlockIo2 = NULL;
  EFI_STATUS              Status;
  RW_EVENT_DESC           *EventDesc;
  UINT32                  Index;

  gST = SystemTable;

  FindEmmcBlockIo (&BlockIo2);
  if (BlockIo2 == NULL) {
    DEBUG ((DEBUG_INFO, "Couldn't find BlockIo2\n"));
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < (UINT32)BlockIo2->Media->LastBlock && Index < 5; Index++) {
    DEBUG ((DEBUG_INFO, "Scheduling write %d\n", Index));
    EventDesc = AllocateZeroPool (sizeof (RW_EVENT_DESC));
    if (EventDesc == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    EventDesc->BlockIo2 = BlockIo2;
    EventDesc->Lba = (EFI_LBA) Index;
    CopyMem (&EventDesc->Buffer, &WriteBuffer, sizeof (WriteBuffer));
    Status = gBS->CreateEvent (
                    EVT_NOTIFY_SIGNAL,
                    TPL_NOTIFY,
                    WriteEventFinishedCallback,
                    EventDesc,
                    &EventDesc->Token.Event
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "Failed to create event\n"));
      continue;
    }

    Status = BlockIo2->WriteBlocksEx (
                         BlockIo2,
                         BlockIo2->Media->MediaId,
                         (EFI_LBA) Index,
                         &EventDesc->Token,
                         512,
                         &EventDesc->Buffer
                         );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "Failed to write data, %r\n", Status));
      continue;
    }
  }

  DEBUG ((DEBUG_INFO, "All writes scheduled\n"));

  while (TRUE) {}

  return EFI_SUCCESS;
}

