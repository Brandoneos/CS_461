#include <stdarg.h>

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "vga.h"
static struct {
  struct spinlock lock;
  int locking;
} cons;
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory
int
displayioctl(struct file *f, int param, int value)
{
    if(param == 1) {// switch modes 
        //passed:  ioctl(fd,1,0x13) video mode = 0x13 
        //passed:  ioctl(fd,1,0x3) 0x3 = text mode
        if(value == 0x13) {
            vgaMode13();
        } else if(value == 0x3) {
            vgaMode3();
        } else {
            cprintf("Wrong value, param = %d, value = %d\n",param,value);
            return -1;
        }
        //using vga.h helper functions
    } else if(param == 2) {// the value is a 32-bit struct containing (palette#, R, G, B)
        //passed: ioctl(fd,2,0x0f<<24 | 63 << 16 | fade << 8 | fade )
        //split value to match the 4 arguments?
        int index = (value >> 24) & 0xFF;
        int r = (value >> 16) & 0xFF;
        int g = (value >> 8) & 0xFF;
        int b = (value) & 0xFF;
        vgaSetPalette(index,r,g,b);
        //void vgaSetPalette(int index, int r, int g, int b);
    } else {
        cprintf("Wrong param + value, param = %d, value = %d\n",param,value);
        return -1;
    }
    return 0;
}
static char *vb = (char*)P2V(0xa0000);  // video buffer
int
displaywrite(struct file *f, char *buf, int n) {
    int i;
    acquire(&cons.lock);
    for(i = 0; i < n; i++) {
        vb[f->off] = buf[i];
        f->off++;
    }
    release(&cons.lock);
    return n;
}
void
displayinit(void)
{
  devsw[DISPLAY].write = displaywrite;
  devsw[DISPLAY].ioctl = displayioctl;
}
