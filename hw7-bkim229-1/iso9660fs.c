#include "types.h"
#include "param.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fs.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "stat.h"
#include "buf.h"
#include "memlayout.h"
#include "iso9660.h"

static void
read_block_range(char *dst, uint start_block, uint count)
{
  // cprintf("read_block_range() called\n");
  for (int block = start_block; block < start_block + count; block++)
  {
    // cprintf("for loop\n");called 4 times per call,called count times
    // cprintf("block:%d\n",block);
    struct buf *bp = bread(2, block);
    memmove(dst, bp->data, BSIZE);
    brelse(bp);
    dst += BSIZE;
  }
}

void iso9660fs_ipopulate(struct inode *ip)
{
  // begin_op();
  static struct iso9660_pvd_s pvd;
  read_block_range(&pvd, 64, 4);
  if (memcmp(pvd.id, "CD001", 5) != 0)
  {

    cprintf("ID mismatch\n");
    // end_op();
    return;
  }

  uint addr = pvd.root_directory_record.extent * 2048; // 59392
  static char buf[2048];

  struct inode *mount_point = ip->mounted_dev;
  // cprintf("ip->mounted_dev:%d\n", ip->mounted_dev);
  if (ip->inum < addr) // have to change this to mount_point but causes page fault
  {
    read_block_range(buf, addr / 512, 4);
    struct iso9660_dir_s *entry = buf;
    //. folder
    if (entry->file_flags == 0x2)
    { // is a directory
      ip->type = T_DIR;
    }
    else
    { // is a file = 0
      ip->type = T_FILE;
    }
    ip->size = entry->size;
    cprintf("size for inum:%d is %d\n",ip->inum,entry->size);
    ip->addrs[0] = entry->extent * 2048; //* 2048
    ip->type = T_DIR;
    ip->size = 64;//hard coded to 64 for now
  }
  else
  {
    uint byteOffset = ip->inum - addr;
    uint addressOfEntry = ip->inum;
    uint offset1 = byteOffset;
    uint bytesToAdd = 0;
    while (offset1 >= 2048)
    {
      offset1 -= 512;
      bytesToAdd += 512;
    }
    addr += bytesToAdd;
    read_block_range(buf, addr / 512, 4); // read the correct block, only 1 block everytime
    struct iso9660_dir_s *entry = buf;
    char namebuf[100] = {0};
    entry = (struct iso9660_dir_s *)((char *)entry + offset1); 
    // change, it was going past the 2048 limit before, now i factor it in
    if (!entry->length) // entry len is 0
    {
      // end_op();
      return;
    }
    if (entry->file_flags == 0x2)
    { // is a directory
      ip->type = T_DIR;
    }
    else
    { // is a file = 0
      ip->type = T_FILE;

    }
    ip->size = entry->size;
    ip->addrs[0] = entry->extent * 2048; //* 2048
  }
  ip->nlink = 1;

  ip->flags |= I_VALID;
  // end_op();
}

void iso9660fs_iupdate(struct inode *ip)
{
}

static int
iso9660fs_writei(struct inode *ip, char *buf, uint offset, uint count)
{
  return -1;
}

int iso9660fs_readi(struct inode *ip, char *dst, uint offset, uint size)
{
  // begin_op();
  if (ip->type == T_DIR)
  {
    struct inode *mount_point = ip->mounted_dev;
    if (mount_point)
    {
      static struct iso9660_pvd_s pvd;

      read_block_range(&pvd, 64, 4);
      if (memcmp(pvd.id, "CD001", 5) != 0)
      {
        cprintf("ID mismatch\n");
        // end_op();
        return;
      }
      // cprintf("in readi:inode:%d\n",ip->inum);//21 in root
      // ls mnt = 21,ls mnt/BOOT/ISOLINUX = 59630(boot)
      // cprintf("pvdextent:%d\n",pvd.root_directory_record.extent);//29
      uint addr = pvd.root_directory_record.extent * 2048; // location of directory file in bytes
      // cprintf("addr:%d\n", addr);                          // 59392, + byte offset shoudl be inum
      static char buf[2048];
      read_block_range(buf, addr / 512, 4);
      struct iso9660_dir_s *entry = buf;

      if (offset == 0)
      {
        struct dirent *de = dst;
        memmove(de->name, ".", 2); // current directory
        de->inum = ip->inum;
        // end_op();
        return (sizeof(struct dirent));
      }
      else if (offset == 32)
      {
        struct dirent *de = dst;
        int index = offset / (sizeof(struct dirent));
        int entryTotal = 0;
        for (int i = 0; i < index; i++)
        {
          entryTotal += entry->length;
          entry = (struct iso9660_dir_s *)((char *)entry + entry->length);
        }
        if (!entry->length)
        {
          // end_op();
          return 0;
        }
        memmove(de->name, "..", 3); // parent directory
        int inum1 = entryTotal + addr;
        de->inum = inum1;
        // end_op();
        return (sizeof(struct dirent));
      }
      else
      { // will be 64 or higher, index is 2 or higher
        struct dirent *de = dst;
        int count = 0;
        int index = offset / (sizeof(struct dirent));
        int entryTotal = 0;
        for (int i = 0; i < index; i++)
        {
          entryTotal += entry->length;
          entry = (struct iso9660_dir_s *)((char *)entry + entry->length);
        }
        if (!entry->length)
        {
          // end_op();
          return 0;
        }
        char namebuf[100] = {0};
        memcpy(namebuf, &entry->filename.str[1], entry->filename.len);
        int foundsemi = 0;
        int realEnd = 0;
        for (int i = 0; i < entry->filename.len; i++)
        {
          if (namebuf[i] == ';')
          {
            if (i + 1 < entry->filename.len)
            {
              if (namebuf[i + 1] == '1')
              {
                namebuf[i] = '\0';
                realEnd = i;
              }
            }
          }
        }
        if (realEnd != 0)
        {
          memmove(de->name, namebuf, realEnd + 1);
        }
        else
        {
          memmove(de->name, namebuf, entry->filename.len + 1);
        }
        int inum1 = entryTotal + addr;
        de->inum = inum1;
        // end_op();
        return (sizeof(struct dirent));
      }
    }
    else
    { // not root directory
      // uint start_block1 = ip->addrs[0]; 
      uint addr = ip->addrs[0];  // location of directory file in bytes
      static char buf[2048];
      read_block_range(buf, addr / 512, 4); // made this one
      struct iso9660_dir_s *entry = buf;
      if (offset == 0)
      {
        struct dirent *de = dst;
        memmove(de->name, ".", 2); // current directory
        de->inum = ip->inum;
        // end_op();
        return (sizeof(struct dirent));
      }
      else if (offset == 32)
      {
        struct dirent *de = dst;
        int index = offset / (sizeof(struct dirent));
        int entryTotal = 0;
        for (int i = 0; i < index; i++)
        {
          entryTotal += entry->length;
          entry = (struct iso9660_dir_s *)((char *)entry + entry->length);
        }
        if (!entry->length)
        {
          // end_op();
          return 0;
        }
        memmove(de->name, "..", 3); // parent directory
        int inum1 = entryTotal + addr;
        de->inum = inum1;
        // end_op();
        return (sizeof(struct dirent));
      }
      else
      { // will be 64 or higher, index is 2 or higher
        struct dirent *de = dst;
        // // use the same logic as init() below
        int count = 0;
        int index = offset / (sizeof(struct dirent));
        int entryTotal = 0;
        for (int i = 0; i < index; i++)
        {
          entryTotal += entry->length;
          entry = (struct iso9660_dir_s *)((char *)entry + entry->length);
          // cprintf("entrylen:%d\n",entry->length);//use entry length instead?
        }
        if (!entry->length)
        {
          // end_op();
          return 0;
        }
        char namebuf[100] = {0};
        memcpy(namebuf, &entry->filename.str[1], entry->filename.len);
        int foundsemi = 0;
        int realEnd = 0;
        for (int i = 0; i < entry->filename.len; i++)
        {
          if (namebuf[i] == ';')
          {
            if (i + 1 < entry->filename.len)
            {
              if (namebuf[i + 1] == '1')
              {
                namebuf[i] = '\0';
                realEnd = i;
              }
            }
          }
        }
        if (realEnd != 0)
        {
          memmove(de->name, namebuf, realEnd + 1);
        }
        else
        {
          memmove(de->name, namebuf, entry->filename.len + 1);
        }
        int inum1 = entryTotal + addr;
        de->inum = inum1;
        // end_op();
        return (sizeof(struct dirent));
      }
    }
  }
  else if (ip->type == T_FILE)
  {
    struct dirent *de = dst;
    int len = size < 16 ? size : 16;
    if (offset < ip->size)
    {
      // read_block_range(char *dst, uint start_block, uint count)
      // uint start_block1 = ip->addrs[0];
      uint addr = ip->addrs[0];
      static char buf[512];
      uint bytesToAdd = 0;
      uint offset1 = offset;
      while (offset1 >= 512)
      {
        offset1 -= 512;
        bytesToAdd += 512;
      }
      addr += bytesToAdd;
      read_block_range(buf, addr / 512, 1); // read the correct block, only 1 block everytime
      char *buf2 = buf + offset1;
      if (ip->size - offset < len)
      {
        len = ip->size - offset;
      }
      memmove(dst, buf2, len);
      // end_op();
      return len;
    }
    else
    {
      // end_op();
      return 0;
    }
  }
  // end_op();
  return -1; // return -1 on error
}

struct inode_functions iso9660fs_functions = {
    iso9660fs_ipopulate,
    iso9660fs_iupdate,
    iso9660fs_readi,
    iso9660fs_writei,
};

void iso9660fsinit(char *const path, char *device)
{
  /*

      uint dev = ip->dev;
    uint addr = ip->inum;
    // because we're not sure about the alignment of the record, always read two blocks
    static char buf[1024];
    read_block_range(buf,addr/512,2);
    struct iso9660_dir_s *entry = buf+(addr & 0x1ff); // offset into the block

    ip->type = ((entry->file_flags == 0x2)?T_DIR:T_FILE);
    ip->nlink = 1;
    ip->size = entry->size;
    ip->addrs[0] = entry->extent*2048;

*/
  cprintf("Mounting cdrom\n");
  begin_op();
  struct inode *mount_point = namei(path);
  if (mount_point)
  {

    static struct iso9660_pvd_s pvd;
    read_block_range(&pvd, 64, 4);
    if (memcmp(pvd.id, "CD001", 5) == 0)
    {
      cprintf("ID matched ISO9660 file system\n");
    }
    else
    {
      cprintf("ID mismatch\n");
      return;
    }
    /*
    struct inode {//*change = metadata of a file
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock;
  int flags;          // I_VALID

  uint mounted_dev;   // if this inode is a mount point, mounted_dev is the dev of contained files
  struct inode* mount_parent;
  struct inode_functions *i_func;

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};
    */

    cprintf("Root directory record version %d len %d size %d at %d, volume size %d block size %d set size %d.\n", pvd.version,
            pvd.root_directory_record.length,
            pvd.root_directory_record.size,
            pvd.root_directory_record.extent, pvd.volume_space_size * 2048, pvd.logical_block_size, pvd.volume_set_size);

    uint addr = pvd.root_directory_record.extent * 2048; // location of directory file in bytes
    static char buf[2048];
    read_block_range(buf, addr / 512, 4);
    struct iso9660_dir_s *entry = buf;
    //
    // uint total = 0;
    while (entry->length)
    {
      // total+= entry->length;
      char namebuf[100] = {0};
      memcpy(namebuf, &entry->filename.str[1], entry->filename.len);
      // void *memcpy(void *to, const void *from, size_t numBytes);
      cprintf("Entry: %s\n", namebuf);
      entry = (struct iso9660_dir_s *)((char *)entry + entry->length);
    }
    // cprintf("total:%d\n",total);
    ilock(mount_point);
    mount_point->i_func = &iso9660fs_functions;
    mount_point->mounted_dev = 2;
    mount_point->addrs[0] = pvd.root_directory_record.extent * 2048;
    iunlock(mount_point);
  }
  end_op();
}
