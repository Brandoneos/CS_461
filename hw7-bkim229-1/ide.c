// Simple PIO-based (non-DMA) IDE driver code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

#define SECTOR_SIZE   512
#define IDE_BSY       0x80
#define IDE_DRDY      0x40
#define IDE_DF        0x20
#define IDE_ERR       0x01

#define IDE_CMD_READ  0x20
#define IDE_CMD_WRITE 0x30
#define IDE_CMD_RDMUL 0xc4
#define IDE_CMD_WRMUL 0xc5

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating queue.

static struct spinlock idelock;

static struct buf *idequeue;

static int havedisk1;
static void idestart(struct buf*);

// Wait for IDE disk to become ready.
static int
idewait(int IOPS, int checkerr)
{
  int r;

  while(((r = inb(IOPS+7)) & (IDE_BSY|IDE_DRDY)) != IDE_DRDY)
    ;
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0) {
    cprintf("Some IDE error: %x %x\n",r,inb(IOPS+1));
    return -1;
  }
  return 0;
}

void
ideinit(void)
{
  initlock(&idelock, "ide");
  ioapicenable(IRQ_IDE, ncpu - 1);
  idewait(0x1f0,0x0);

  // Check if disk 1 is present
  outb(0x1f6, 0xe0 | (1<<4));
  for(int i=0; i<1000; i++){
    if(inb(0x1f7) != 0){
      havedisk1 = 1;
      break;
    }
  }

  // Switch back to disk 0.
  outb(0x1f6, 0xe0 | (0<<4));
  outb(0x176, 0xe0 | (0<<4));

  ioapicenable(IRQ_IDE+1, ncpu - 1);
  idewait(0x170,1);

  for(int i=0; i<1000; i++){
    if(inb(0x177) != 0){
      cprintf("Found cdrom!\n");
      break;
    }
  }

}

// Start the request for b.  Caller must hold idelock.
static void
idestart(struct buf *b)
{
  if(b == 0)
    panic("idestart");
  // if(b->blockno >= FSSIZE)
  //   panic("incorrect blockno");
  int sector_per_block =  BSIZE/SECTOR_SIZE;
  int sector = b->blockno * sector_per_block;
  int read_cmd = (sector_per_block == 1) ? IDE_CMD_READ :  IDE_CMD_RDMUL;
  int write_cmd = (sector_per_block == 1) ? IDE_CMD_WRITE : IDE_CMD_WRMUL;

  if (sector_per_block > 7) panic("idestart");

  int DCR;
  int IOPS;
  int dev;

  if(b->dev < 2) {
    DCR = 0x3f6;
    IOPS = 0x1f0;
    dev = b->dev&1;
  }
  else {
    DCR = 0x376;
    IOPS = 0x170;
    dev = (b->dev % 2);
  }
  
  idewait(IOPS, 0);

  outb(DCR, 0); // generate interrupt
  outb(IOPS+2, sector_per_block);  // number of sectors
  outb(IOPS+3, sector & 0xff);
  outb(IOPS+4, (sector >> 8) & 0xff);
  outb(IOPS+5, (sector >> 16) & 0xff);

  outb(IOPS+6, 0xe0 | (dev<<4) | ((sector>>24)&0x0f));
  if(b->flags & B_DIRTY){
    outb(IOPS+7, write_cmd);
    outsl(IOPS+0, b->data, BSIZE/4);
  } else {
    outb(IOPS+7, read_cmd);
  }
}

// Interrupt handler.
void
ideintr(int IOPS)
{
  struct buf *b;

  // First queued buffer is the active request.
  acquire(&idelock);
  if((b = idequeue) == 0){
    release(&idelock);
    cprintf("spurious IDE interrupt %x\n",IOPS);
    return;
  }
  idequeue = b->qnext;

  // Read data if needed.
  if(!(b->flags & B_DIRTY) && idewait(IOPS,1) >= 0)
    insl(IOPS+0, b->data, BSIZE/4);

  // Wake process waiting for this buf.
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  wakeup(b);

  // Start disk on next buf in queue.
  if(idequeue != 0)
    idestart(idequeue);

  release(&idelock);
}

//PAGEBREAK!
// Sync buf with disk.
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void
iderw(struct buf *b)
{
  struct buf **pp;

  if(!holdingsleep(&b->lock))
    panic("iderw: buf not locked");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("iderw: nothing to do");
  if(b->dev != 0 && !havedisk1)
    panic("iderw: ide disk 1 not present");

  acquire(&idelock);  //DOC:acquire-lock

  // Append b to idequeue.
  b->qnext = 0;
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  //DOC:insert-queue
    ;
  *pp = b;

  // Start disk if necessary.
  if(idequeue == b)
    idestart(b);

  // Wait for request to finish.
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){
    sleep(b, &idelock);
  }

  release(&idelock);
}
