//           vioblk.c - VirtIO serial port (console)
//          

#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "lock.h"

//           COMPILE-TIME PARAMETERS
//          

#define VIOBLK_IRQ_PRIO 1

//           INTERNAL CONSTANT DEFINITIONS
//          

//           VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

//           INTERNAL TYPE DEFINITIONS
//          

//           All VirtIO block device requests consist of a request header, defined below,
//           followed by data, followed by a status byte. The header is device-read-only,
//           the data may be device-read-only or device-written (depending on request
//           type), and the status byte is device-written.

struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

//           Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

//           Status byte values

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

//           Main device structure.
//          
//           FIXME You may modify this structure in any way you want. It is given as a
//           hint to help you, but you may have your own (better!) way of doing things.

struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    struct io_intf io_intf;
    uint16_t instno;
    uint16_t irqno;
    int8_t opened;
    int8_t readonly;

    //           optimal block size
    uint32_t blksz;
    //           current position
    uint64_t pos;
    //           sizeo of device in bytes
    uint64_t size;
    //           size of device in blksz blocks
    uint64_t blkcnt;

    struct {
        //           signaled from ISR
        struct condition used_updated;

        //           We use a simple scheme of one transaction at a time.

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        //           The first descriptor is an indirect descriptor and is the one used in
        //           the avail and used rings. The second descriptor points to the header,
        //           the third points to the data, and the fourth to the status byte.

        struct virtq_desc desc[4];
        struct vioblk_request_header req_header;
        uint8_t req_status;
    } vq;

    //           Block currently in block buffer
    uint64_t bufblkno;
    //           Block buffer
    char * blkbuf;
    struct lock io_lock;
};

//           INTERNAL FUNCTION DECLARATIONS
//          

static int vioblk_open(struct io_intf ** ioptr, void * aux);

static void vioblk_close(struct io_intf * io);

static long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz);

static long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n);

static int vioblk_ioctl (
    struct io_intf * restrict io, int cmd, void * restrict arg);

static void vioblk_isr(int irqno, void * aux);

// define a struct that contains pointers to our driver functions

static const struct io_ops vioblk_io_ops = {
    .close = vioblk_close,
    .read = vioblk_read,
    .write = vioblk_write,
    .ctl = vioblk_ioctl
};

//           IOCTLs

static int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr);
static int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr);
static int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr);
static int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr);

//           EXPORTED FUNCTION DEFINITIONS
//          

//           Attaches a VirtIO block device. Declared and called directly from virtio.c.

// void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno);
//
// initializes virtio block device with the necessary IO operation functions adn sets the required
// feature bits. argument regs is the mmio registers for the given block device. argument irqno
// is the interrupt request no for the given block device. 
// 
// this function should be used to register the block device. it sets feature bits, initializes 
// device fields, and virtque, attaches the virtque to the device, registers the device with the 
// OS and the ISR. 

void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    //           FIXME add additional declarations here if needed
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device * dev;
    uint_fast32_t blksz;
    int result;
    assert (regs->device_id == VIRTIO_ID_BLOCK);
    //           Signal device that we found a driver
    regs->status |= VIRTIO_STAT_DRIVER;
    //           fence o,io
    __sync_synchronize();
    //           Negotiate features. We need:
    //            - VIRTIO_F_RING_RESET and
    //            - VIRTIO_F_INDIRECT_DESC
    //           We want:
    //            - VIRTIO_BLK_F_BLK_SIZE and
    //            - VIRTIO_BLK_F_TOPOLOGY.
    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);
    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }
    //           If the device provides a block size, use it. Otherwise, use 512.
    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;
    debug("%p: virtio block device block size is %lu", regs, (long)blksz);
    //           Allocate initialize device struct
    dev = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(dev, 0, sizeof(struct vioblk_device));

    lock_init(&dev->io_lock, "vioblk_io_lock");
    //           FIXME Finish initialization of vioblk device here
    //-----------------------------------------------------------------------------
    // initialize device fields
    dev->regs = regs;
    dev->irqno = irqno;
    dev->blksz = blksz;
    dev->opened = 0;
    dev->readonly = 0;
    dev->pos = 0;
    dev->bufblkno = (uint64_t)(-1);
    dev->size = regs->config.blk.capacity * 512;
    dev->blkcnt = dev->size / dev->blksz;
    dev->regs->queue_num = 0;

    condition_init(&dev->vq.used_updated, "used_updated");

    dev->blkbuf = kmalloc(blksz * sizeof(char));
    assert(dev->blkbuf != NULL);

    // initialize I/O interface
    dev->io_intf.ops = &vioblk_io_ops;

    // initialize the virtqueue descriptors
    // descriptor 0: indirect descriptor
    dev->vq.desc[0].addr = (uint64_t)&dev->vq.desc[1];
    dev->vq.desc[0].len = sizeof(struct virtq_desc) * 3;
    dev->vq.desc[0].flags = VIRTQ_DESC_F_INDIRECT;
    dev->vq.desc[0].next = 1;

    // descriptor 1: request header
    dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header;
    dev->vq.desc[1].len = sizeof(struct vioblk_request_header);
    dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT;
    dev->vq.desc[1].next = 2;

    // descriptor 2: data buffer
    dev->vq.desc[2].addr = (uint64_t)&dev->blkbuf;
    dev->vq.desc[2].len = blksz;
    dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    dev->vq.desc[2].next = 3;

    // descriptor 3: status byte
    dev->vq.desc[3].addr = (uint64_t)&dev->vq.req_status;
    dev->vq.desc[3].len = sizeof(uint8_t);
    dev->vq.desc[3].flags = VIRTQ_DESC_F_WRITE;
    dev->vq.desc[3].next = 0;

    // attach the virtqueue to the device
    virtio_attach_virtq(regs, 0, 1, (uint64_t)&dev->vq.desc[0], (uint64_t)&dev->vq.used, (uint64_t)&dev->vq.avail);
    
    // register isr
    intr_register_isr(irqno, VIOBLK_IRQ_PRIO, vioblk_isr, dev);

    // register the device with the OS
    uint16_t instno = device_register("blk", vioblk_open, dev);
    assert(instno >= 0);

    dev->instno = instno;
//-----------------------------------------------------------------------------
 
    regs->status |= VIRTIO_STAT_DRIVER_OK;    
    //           fence o,oi
    __sync_synchronize();
}

// int vioblk_open(struct io_intf ** ioptr, void * aux);
//
// sets the virtq_avail and virtq_used such that they are available for use
// argument ioptr returns the io operations, argument aux is the pointer to the
// device. returns 0 in success
// 
// should be used to open a device. it enables the interrupt line for the virtio
// device and sets necessary flags in vioblk_device

int vioblk_open(struct io_intf ** ioptr, void * aux) {
    struct vioblk_device * dev = (struct vioblk_device *)aux;

    lock_acquire(&dev->io_lock); // acquire the lock

    // check if the device is already opened
    if (dev->opened) {
        lock_release(&dev->io_lock);
        return -EBUSY;
    }

    // initialize the avail ring
    dev->vq.avail.flags = 0;
    dev->vq.avail.idx = 0;
    dev->vq.avail.ring[0] = 0;

    // initialize the used ring
    dev->vq.used.flags = 0;
    dev->vq.used.idx = 0;
    dev->vq.used.ring[0].id = 0;
    dev->vq.used.ring[0].len = 0;

    virtio_enable_virtq(dev->regs, dev->regs->queue_num);
    virtio_notify_avail(dev->regs, dev->regs->queue_num);

    // enable interrupt line
    intr_enable_irq(dev->irqno);

    // return the io interface through ioptr
    *ioptr = &dev->io_intf;

    (*ioptr)->refcnt = 1;

    // mark device as opened
    dev->opened = 1;

    // console_printf("device opened\n");
    lock_release(&dev->io_lock);
    return 0;
}

//           Must be called with interrupts enabled to ensure there are no pending
//           interrupts (ISR will not execute after closing).

// void vioblk_close(struct io_intf * io);
//
// resets the virtq_avail and virtq_used queeus and sets necessary flags in vioblk_device
// arg io is the pointer to the io_intf of the block device
//
// should be used to close the device

void vioblk_close(struct io_intf * io) {
    struct vioblk_device *dev = (void *)io - offsetof(struct vioblk_device, io_intf);

    lock_acquire(&dev->io_lock);

    // reset the avail ring 
    dev->vq.avail.idx = 0;
    dev->vq.avail.flags = VIRTQ_AVAIL_F_NO_INTERRUPT;
    
    // disable interrupts from device
    intr_disable_irq(dev->irqno);

    // reset the device position to the beginning
    virtio_reset_virtq(dev->regs, dev->regs->queue_num);

    lock_release(&dev->io_lock);

    dev->opened = 0;
}

// long vioblk_read(struct io_intf * restrict io,
//                  void * restrict buf,
//                  unsigned long bufsz);
//
// Reads bufsz number of bytes from the disk and writes them to buf. Achieves this by repeatedly
// setting the appropriate registers to request a block from the disk, waiting until the data has been
// populated in block buffer cache, and then writes that data out to buf. argument io points to the
// io of the device given, buf is the buffer to read and bufsz is how many bytes of data we want to read
//
// Thread sleeps while waiting for the disk to service the request. Returns the number of bytes
// successfully read from the disk.

long vioblk_read (
    struct io_intf * restrict io,
    void * buf,
    unsigned long bufsz)
{
    struct vioblk_device * dev = (void *)io - offsetof(struct vioblk_device, io_intf);
    long total_read = 0;

    lock_acquire(&dev->io_lock);

    while (total_read < bufsz) {
        if (dev->pos >= dev->size) break;

        // calculate how many bytes we can read
        // take into account partial reads
        uint32_t sector_index = dev->pos / dev->blksz;
        uint32_t sector_offset = dev->pos % dev->blksz;

        unsigned long bytes_available_in_block = dev->blksz - sector_offset;

        unsigned long bytes_remaining = bufsz - total_read;
        unsigned long bytes_this_read = (bytes_available_in_block < bytes_remaining) ? bytes_available_in_block :
                                                                                       bytes_remaining;

        // set up descriptors
        // request header
        dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header;
        dev->vq.desc[1].len = sizeof(struct vioblk_request_header);
        dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT;
        dev->vq.desc[1].next = 1;

        // data header
        dev->vq.desc[2].addr = (uint64_t)dev->blkbuf;
        dev->vq.desc[2].len = dev->blksz;
        dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
        dev->vq.desc[2].next = 2;

        // set up request header
        dev->vq.req_header.sector = sector_index;
        dev->vq.req_header.type = VIRTIO_BLK_T_IN;

        // set up avail ring
        dev->vq.avail.ring[dev->vq.avail.idx % 1] = 0;
        __sync_synchronize(); // mem barrier
        dev->vq.avail.idx += 1;
        __sync_synchronize(); // mem barrier

        // notify the avail ring
        virtio_notify_avail(dev->regs, 0);

        uint64_t intr_state = intr_disable();
        condition_wait(&dev->vq.used_updated);
        intr_restore(intr_state);

        // data cooked; copy it back
        memcpy(buf + total_read, dev->blkbuf + sector_offset, bytes_this_read);

        dev->pos += bytes_this_read;
        total_read += bytes_this_read;
    }

    lock_release(&dev->io_lock);
    return total_read;
}

// long vioblk_write (
//    struct io_intf * restrict io,
//    const void * restrict buf,
//    unsigned long n);
//
// Writes n number of bytes from the parameter buf to the disk. The size of the virtio device should
// not change. You should only overwrite existing data. Write should also not create any new files.
// Achieves this by filling up the block buffer cache and then setting the appropriate registers to request
// the disk write the contents of the cache to the specified block location. arg io points to the device
// arg buf contains the data to be written, and arg n is the # of bytes to write
//
// Thread sleeps while waiting for the disk to service the request. Returns the number of bytes 
// successfully written to the disk.

long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n)
{
    struct vioblk_device *dev = (void *)io - offsetof(struct vioblk_device, io_intf);
    long total_written = 0;
    
    if (dev->readonly) {
        return -EINVAL;
    }

    lock_acquire(&dev->io_lock); // acquire lock

    // very similar to read
    while (total_written < n) {
        if (dev->pos >= dev->size) return 0;

        // check how many bytes we can write to this file
        // take into consideration partial writes
        uint32_t sector_index = dev->pos / dev->blksz;
        uint32_t sector_offset = dev->pos % dev->blksz;

        unsigned long bytes_available_in_block = dev->blksz - sector_offset;

        unsigned long bytes_remaining = n - total_written;
        unsigned long bytes_this_write = (bytes_available_in_block < bytes_remaining) ? bytes_available_in_block :
                                                                                        bytes_remaining;

        // check if its a partial block write
        // if so we need to read the block in first
        if (bytes_this_write != dev->blksz) {
            // set up descriptors
            // request header
            dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header;
            dev->vq.desc[1].len = sizeof(struct vioblk_request_header);
            dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT;
            dev->vq.desc[1].next = 1;

            // data header
            dev->vq.desc[2].addr = (uint64_t)dev->blkbuf;
            dev->vq.desc[2].len = dev->blksz;
            dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
            dev->vq.desc[2].next = 2;

            // set up request header
            dev->vq.req_header.sector = sector_index;
            dev->vq.req_header.type = VIRTIO_BLK_T_IN;

            // set up avail ring
            dev->vq.avail.ring[dev->vq.avail.idx % 1] = 0;
            __sync_synchronize(); // mem barrier
            dev->vq.avail.idx += 1;
            __sync_synchronize(); // mem barrier

            // notify the avail ring
            virtio_notify_avail(dev->regs, 0);

            uint64_t intr_state = intr_disable();
            condition_wait(&dev->vq.used_updated);
            intr_restore(intr_state);
        }

        memcpy(dev->blkbuf + sector_offset, buf + total_written, bytes_this_write);

        // set up descriptors
        // request header
        dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header;
        dev->vq.desc[1].len = sizeof(struct vioblk_request_header);
        dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT;
        dev->vq.desc[1].next = 1;

        // data header
        dev->vq.desc[2].addr = (uint64_t)dev->blkbuf;
        dev->vq.desc[2].len = dev->blksz;
        dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT;
        dev->vq.desc[2].next = 2;

        // set up request header
        dev->vq.req_header.sector = sector_index;
        dev->vq.req_header.type = VIRTIO_BLK_T_OUT;

        // set up avail ring
        dev->vq.avail.ring[dev->vq.avail.idx % 1] = 0;
        __sync_synchronize(); // mem barrier
        dev->vq.avail.idx += 1;
        __sync_synchronize(); // mem barrier

        // notify the avail ring
        virtio_notify_avail(dev->regs, 0);

        uint64_t intr_state = intr_disable();
        condition_wait(&dev->vq.used_updated);
        intr_restore(intr_state);

        dev->pos += bytes_this_write; 
        total_written += bytes_this_write;
    }

    lock_release(&dev->io_lock);
    return total_written;
}

int vioblk_ioctl(struct io_intf * restrict io, int cmd, void * restrict arg) {
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);
    
    trace("%s(cmd=%d,arg=%p)", __func__, cmd, arg);

    int result;

    lock_acquire(&dev->io_lock);
    
    switch (cmd) {
    case IOCTL_GETLEN:
        result = vioblk_getlen(dev, arg);
        break;
    case IOCTL_GETPOS:
        result = vioblk_getpos(dev, arg);
        break;
    case IOCTL_SETPOS:
        result = vioblk_setpos(dev, arg);
        break;
    case IOCTL_GETBLKSZ:
        result = vioblk_getblksz(dev, arg);
        break;
    default:
        return -ENOTSUP;
    }

    lock_release(&dev->io_lock);
    return result;
}

// void vioblk_isr(int irqno, void * aux);
//
// Sets the appropriate device registers and wakes the thread up after waiting 
// for the disk to finish servicing a request. aux points to the device
// and irqno is the interrupt request no. 

void vioblk_isr(int irqno, void * aux) {
    struct vioblk_device * dev = (struct vioblk_device *)aux;

    // read the interrupt status register to determine the cause of the interrupt
    uint32_t interrupt_status = dev->regs->interrupt_status;

    // handle virtqueue interrupts
    if (interrupt_status & 0x1) {
        condition_broadcast(&dev->vq.used_updated);   
        // write to acknowledge register
        dev->regs->interrupt_ack = interrupt_status;
        __sync_synchronize();
    }
}

// int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr);
//
// Ioctl helper function which provides the device size in bytes. arg dev points
// to the device. arg lenptr points to the len. returns 0 on success

int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr) {
    // check if arguments are valid
    if (!dev || !lenptr) {
        return -EINVAL;
    }

    *lenptr = dev->size;

    return 0;
}

// int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr);
//
// Ioctl helper function which gets the current position in the disk which is currently 
// being written to or read from. arg dev points to the device, arg posptr points
// to the position. returns 0 on success

int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr) {
    // check if arguments are valid
    if (!dev || !posptr) {
        return -EINVAL;
    }

    // retrieve current pos
    *posptr = dev->pos;

    return 0;
}

// int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr);
//
// Ioctl helper function which sets the current position in the disk which is currently 
// being written to or read from. arg dev points to the device, and arg posptr is the
// pointer to the position. returns 0 on success

int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr) {
    // check if arguments are valid
    if (!dev || !posptr) {
        return -EINVAL;
    }

    uint64_t new_pos = *posptr;

    // check if the new position is within the device size
    if (new_pos > dev->size) {
        return -EINVAL;
    }

    // update the device's current position
    dev->pos = new_pos;

    return 0;
}

// int vioblk_getblksz(const struct vioblk_device * dev, uint32_t * blkszptr);
// 
// helper function which provides the device block size argument dev points to the 
// block device, argument blkszptr points to the block size. returns 0 on success

int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr)
{
    // check if arguments are valid
    if (!dev || !blkszptr) {
        return -EINVAL;
    }

    // write the block size to the provided pointer
    *blkszptr = dev->blksz;

    // success return 0
    return 0;
}