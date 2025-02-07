// kfs.c
//


#include "io.h"
#include "fs.h"
#include "device.h"
#include "string.h"
#include "halt.h"
#include "error.h"
#include "memory.h"
#include "lock.h"
#include "kfs.h"

// constant definitions
// #define FS_BLKSZ      4096
// #define FS_NAMELEN    32
// #define FS_MAXOPEN    32


// internal type definitions
// Disk layout:
// [ boot block | inodes | data blocks ]


// typedef struct dentry_t{
//     char file_name[FS_NAMELEN];
//     uint32_t inode;
//     uint8_t reserved[28];
// }__attribute((packed)) dentry_t;


// typedef struct boot_block_t{
//     uint32_t num_dentry;
//     uint32_t num_inodes;
//     uint32_t num_data;
//     uint8_t reserved[52];
//     dentry_t dir_entries[63];
// }__attribute((packed)) boot_block_t;


// typedef struct inode_t{
//     uint32_t byte_len;
//     uint32_t data_block_num[1023];
// }__attribute((packed)) inode_t;


// typedef struct data_block_t{
//     uint8_t data[FS_BLKSZ];
// }__attribute((packed)) data_block_t;


// file struct. see 7.2 in cp1 docs


struct file_struct {
    struct io_intf io;
    uint64_t file_position;
    uint64_t file_size;
    uint64_t inode_number;
    uint64_t flags;  
};


// internal function definitions
int fs_mount(struct io_intf* blkio);
int fs_open(const char* name, struct io_intf** ioptr);
void fs_close(struct io_intf* io);
long fs_write(struct io_intf* io, const void* buf, unsigned long n);
long fs_read(struct io_intf* io, void* buf, unsigned long n);
int fs_ioctl(struct io_intf* io, int cmd, void* arg);
int fs_getlen(struct file_struct* fd, void* arg);
int fs_getpos(struct file_struct* fd, void* arg);
int fs_setpos(struct file_struct* fd, void* arg);
int fs_getblksz(struct file_struct* fd, void* arg);


// struct that contains the pointers to our fs functions
static const struct io_ops fs_io_ops = {
    .close = fs_close,
    .read = fs_read,
    .write = fs_write,
    .ctl = fs_ioctl
};


// global variables
struct io_intf * vioblk_io;
char fs_initialized;
struct boot_block_t boot_block;
struct file_struct file_structs[FS_MAXOPEN];
inode_t inode;
data_block_t data_block;
static struct lock fs_lock;

/**
 * fs_mount - Initializes the filesystem for use.
 *
 * @param blkio         Pointer to the block device interface.
 *
 * @return              Returns 0 on success, or a negative error code on failure.
 *                      Errors include already initialized filesystem or I/O issues.
 */
int fs_mount(struct io_intf* blkio) {
    // Initialize lock 
    lock_init(&fs_lock, "Filesystem Lock");
    
    // store the block device interface
    vioblk_io = blkio;


    // check if fs has already been initialized
    if (fs_initialized) {
        console_printf("fs_is already initialized\n");
        return -1;
    }


    // set position to the beginning of io device
    uint64_t offset = 0;
    if (vioblk_io->ops->ctl(vioblk_io, IOCTL_SETPOS, &offset) != 0) {
        console_printf("issue setting block device offset to 0\n");
        return -1;
    }


    // attempt to read bootblock
    if (ioread(blkio, (void *)&boot_block, FS_BLKSZ) < 0) {
        console_printf("error: failed to read bootblock\n");
        return -1;
    }


    console_printf("boot block read successfully, inodes: %u, data blocks: %u\n", boot_block.num_inodes, boot_block.num_data);


    // mark fs as initialized
    fs_initialized = 1;
   
    // init file structs array
    memset(file_structs, 0, sizeof(file_structs));
    return 0;
}






/**
 * fs_open - Opens a file in the filesystem.
 *
 * @param name          Name of the file to be opened.
 * @param ioptr         Pointer to a location where the file's io_intf will be stored.
 *
 * @return              Returns 0 on success, or a negative error code on failure.
 *                      Errors include uninitialized filesystem, no available file slots,
 *                      or file not found in directory entries.
 */
int fs_open(const char* name, struct io_intf** ioptr) {
    // Acquire the lock
    lock_acquire(&fs_lock);

    // check if file system is initialized before calling open
    if (!fs_initialized) {
        console_printf("filesystem not initialized\n");
        lock_release(&fs_lock); // Release the lock before returning 
        return -1;
    }


    // new file
    struct file_struct * file = NULL;


    for (int i = 0; i < FS_MAXOPEN; i++) {
        if (file_structs[i].flags == 0) {
            file = &file_structs[i];
            console_printf("found available slot at index %d\n", i);
            break;
        }
    }


    // check if we found a valid file slot
    if (file == NULL) {
        console_printf("no available file slots\n");
        lock_release(&fs_lock);
        return -1;
    }


    // search for file in directory entries
    struct dentry_t * dentry = NULL;
   
    for (int i = 0; i < boot_block.num_dentry; i++) {
        // ensure file name is null terminated
        char fname[FS_NAMELEN + 1];
        strncpy(fname, boot_block.dir_entries[i].file_name, FS_NAMELEN);
        fname[FS_NAMELEN] = '\0';


        // compare the provided name with the directory entry
        if (strncmp(name, fname, FS_NAMELEN) == 0) {
            dentry = &boot_block.dir_entries[i];
            break;
        }
    }


    if (!dentry) {
        console_printf("file not found in directory entries\n");
        lock_release(&fs_lock);
        return -1;
    }


    // set file position
    file->file_position = 0;
    file->inode_number = dentry->inode;


    // set position to inode start
    uint64_t inode_pos = FS_BLKSZ + file->inode_number * FS_BLKSZ;
    if (vioblk_io->ops->ctl(vioblk_io, IOCTL_SETPOS, &inode_pos) != 0) {
        console_printf("can't set file position\n");
        lock_release(&fs_lock); 
        return -1;
    }


    // read inode data
    uint64_t bytes_read = vioblk_io->ops->read(vioblk_io, &inode, sizeof(struct inode_t));
    if (bytes_read != sizeof(struct inode_t)) {
        console_printf("can't read inode\n");
        lock_release(&fs_lock);
        return -1;
    }


    // initialize file structure with inode data
    file->file_size = inode.byte_len;
    file->flags = 1;                        // mark file as in use
    file->io.ops = &fs_io_ops;
    *ioptr = &file->io;

    (*ioptr)->refcnt = 1;
   
    // succesfully opened file return 0
    console_printf("file opened successfully. file position: %d file size: %d inode number: %d\n",
                                    file->file_position, file->file_size, file->inode_number);

    lock_release(&fs_lock);
    return 0;
}






/**
 * fs_close - Closes an open file.
 *
 * @param io            Pointer to the io_intf of the file to be closed.
 *
 * @return              None. Marks the associated file struct as unused.
 */
void fs_close(struct io_intf* io) {
    for (int i = 0; i < FS_MAXOPEN; i++) {
        if (&file_structs[i].io == io) {
            file_structs[i].flags = 0;
            break;
        }
    }


    return;
}






/**
 * fs_write - Writes data to an open file.
 *
 * @param io            Pointer to the file's io_intf.
 * @param buf           Pointer to the buffer containing data to write.
 * @param n             Number of bytes to write.
 *
 * @return              Returns the number of bytes written on success,
 *                      or a negative error code on failure. This function
 *                      does not extend the file size or create new files.
 */
long fs_write(struct io_intf* io, const void* buf, unsigned long n) {
    // Acquire lock
    lock_acquire(&fs_lock);

    // make sure the parameters are valid
    if (!io || !buf) {
        lock_release(&fs_lock); // Release lock before returning
        return -1;
    }


    // retrieve file struct from io_intf
    struct file_struct* file = (struct file_struct*)((char*)io - offsetof(struct file_struct, io));


    // ensure the file struct is valid and in use
    if (file->flags == 0) {
        lock_release(&fs_lock); // Release lock before returning
        return -2;
    }


    // make sure the file system is initialized
    if (!fs_initialized) {
        lock_release(&fs_lock); // Release lock before returning
        return -3;
    }


    // check if we are at the end of a file
    if (file->file_position >= file->file_size) {
        lock_release(&fs_lock); // Release lock before returning
        return 0;
    }


    // make sure n does not exceed the number of bytes in the file
    if (file->file_position + n > file->file_size) {
        n = file->file_size - file->file_position;
    }


    // read the inode associated with the file
    uint32_t inode_number = file->inode_number;


    // calculate the inode's offset in the filesystem
    uint64_t inode_offset = FS_BLKSZ + (inode_number * FS_BLKSZ);


    // read the inode
    if (vioblk_io->ops->ctl(vioblk_io, IOCTL_SETPOS, &inode_offset) != 0) {
    lock_release(&fs_lock); // Release lock before returning
        return -1; // Error setting position
    }


    long bytes_read = vioblk_io->ops->read(vioblk_io, &inode, sizeof(inode_t));
    if (bytes_read != sizeof(inode_t)) {
    lock_release(&fs_lock);
        return -5;
    }


    // initialize variables for reading the data
    unsigned long total_bytes_written = 0;
    unsigned long bytes_to_write = n;
    uint64_t file_pos = file->file_position;
    long bytes_written;


    while (bytes_to_write > 0) {
        // calculate the current data block index and the index within the block
        uint32_t block_index = file_pos / FS_BLKSZ;
        uint32_t block_offset = file_pos % FS_BLKSZ;


        // check if block index exceeds the max number of blocks allowed
        if (block_index >= sizeof(struct inode_t)) {
            break;
        }


        // get the data block number
        uint32_t data_block_num = inode.data_block_num[block_index];


        // calculate the offset of the data block in the filesystem
        uint64_t data_block_offset = FS_BLKSZ                             // boot block size
                                   + (boot_block.num_inodes * FS_BLKSZ)   // total inode size
                                   + (data_block_num * FS_BLKSZ)          // data block offset
                                   + block_offset;                        // mem offset


        // write to the data block
        if (vioblk_io->ops->ctl(vioblk_io, IOCTL_SETPOS, &data_block_offset) != 0) {
            lock_release(&fs_lock);
            return -6;
        }


        // calculate how many bytes we can write to this block
        unsigned long bytes_available = FS_BLKSZ - block_offset;
        unsigned long bytes_this_write = (bytes_to_write < bytes_available) ? bytes_to_write : bytes_available;


        // copy the data to the buffer
        memcpy(data_block.data, (char*)buf + total_bytes_written, bytes_this_write);


        bytes_written = vioblk_io->ops->write(vioblk_io, &data_block, bytes_this_write);
        if (bytes_written != bytes_this_write) {
            lock_release(&fs_lock);
            return -7;
        }


        // update counters
        total_bytes_written += bytes_this_write;
        bytes_to_write -= bytes_this_write;
        file_pos += bytes_this_write;
    }


    // update file position
    file->file_position = file_pos;

    lock_release(&fs_lock);

    // return the number of bytes read
    return total_bytes_written;
}






/**
 * fs_read - Reads data from an open file.
 *
 * @param io            Pointer to the file's io_intf.
 * @param buf           Pointer to the buffer to store the read data.
 * @param n             Number of bytes to read.
 *
 * @return              Returns the number of bytes read on success,
 *                      or a negative error code on failure. Updates
 *                      the file's read position accordingly.
 */
long fs_read(struct io_intf* io, void* buf, unsigned long n)
{
    // Acquire lock
    lock_acquire(&fs_lock);

    // make sure the parameters are valid
    if (!io || !buf) {
        lock_release(&fs_lock);
        return -1;
    }


    // retrieve file struct from io_intf
    struct file_struct* file = (struct file_struct*)((char*)io - offsetof(struct file_struct, io));


    // ensure the file struct is valid and in use
    if (file->flags == 0) {
        lock_release(&fs_lock); // Release lock before returning
        return -1;
    }


    // make sure the file system is initialized
    if (!fs_initialized) {
        lock_release(&fs_lock); // Release lock before returning
        return -1;
    }


    // check if we are at the end of a file
    if (file->file_position >= file->file_size) {
        lock_release(&fs_lock); // Release lock before returning
        return 0;
    }


    // make sure n does not exceed the number of bytes in the file
    if (file->file_position + n > file->file_size) {
        n = file->file_size - file->file_position;
    }


    // read the inode associated with the file
    uint32_t inode_number = file->inode_number;


    // calculate the inode's offset in the filesystem
    uint64_t inode_offset = FS_BLKSZ + (inode_number * FS_BLKSZ);


    // read the inode
    if (vioblk_io->ops->ctl(vioblk_io, IOCTL_SETPOS, &inode_offset) != 0) {
        lock_release(&fs_lock);
        return -1; // Error setting position
    }


    long bytes_read = vioblk_io->ops->read(vioblk_io, &inode, sizeof(inode_t));
    if (bytes_read != sizeof(inode_t)) {
        lock_release(&fs_lock);
        return -1;
    }


    // initialize variables for reading the data
    unsigned long total_bytes_read = 0;
    unsigned long bytes_to_read = n;
    uint64_t file_pos = file->file_position;


    while (bytes_to_read > 0) {
        // calculate the current data block index and the index within the block
        uint32_t block_index = file_pos / FS_BLKSZ;
        uint32_t block_offset = file_pos % FS_BLKSZ;


        // check if block index exceeds the max number of blocks allowed
        if (block_index >= sizeof(struct inode_t)) {
            break;
        }


        // get the data block number
        uint32_t data_block_num = inode.data_block_num[block_index];


        // calculate the offset of the data block in the filesystem
        uint64_t data_block_offset = FS_BLKSZ                             // boot block size
                                   + (boot_block.num_inodes * FS_BLKSZ)   // total inode size
                                   + (data_block_num * FS_BLKSZ);         // data block offset


        // read the data block
        if (vioblk_io->ops->ctl(vioblk_io, IOCTL_SETPOS, &data_block_offset) != 0) {
            lock_release(&fs_lock);
            return -1;
        }


        // calculate how many bytes we can read from this block
        unsigned long bytes_available = FS_BLKSZ - block_offset;
        unsigned long bytes_this_read = (bytes_to_read < bytes_available) ? bytes_to_read : bytes_available;


        bytes_read = vioblk_io->ops->read(vioblk_io, &data_block, sizeof(data_block_t));
        if (bytes_read != sizeof(data_block_t)) {
            lock_release(&fs_lock);
            return -1;
        }


        // copy the data to the buffer
        memcpy(buf + total_bytes_read, data_block.data + block_offset, bytes_this_read);


        // update counters
        total_bytes_read += bytes_this_read;
        bytes_to_read -= bytes_this_read;
        file_pos += bytes_this_read;
    }


    // update file position
    file->file_position = file_pos;

    lock_release(&fs_lock); // Release lock before returning

    // return the number of bytes read
    return total_bytes_read;
}






/**
 * fs_ioctl - Executes control commands on a file.
 *
 * @param io            Pointer to the file's io_intf.
 * @param cmd           Command to execute (e.g., get length, set position).
 * @param arg           Pointer to additional arguments or output data.
 *
 * @return              Returns the result of the command on success,
 *                      or a negative error code for unsupported commands.
 */
int fs_ioctl(struct io_intf* io, int cmd, void* arg) {
    lock_acquire(&fs_lock);

    // retrieve file struct from io_intf
    struct file_struct* file = (struct file_struct*)((char*)io - offsetof(struct file_struct, io));


    // check if the file is valid and open
    if (!file || !file->flags) {
        lock_release(&fs_lock);
        return -1;
    }

    int result;
    // route the command to the appropriate helper function
    switch(cmd) {
        case IOCTL_GETLEN:
            result = fs_getlen(file, arg);
            break;
        case IOCTL_GETPOS:
            result = fs_getpos(file, arg);
            break;

        case IOCTL_SETPOS:
            result = fs_setpos(file, arg);
            break;

        case IOCTL_GETBLKSZ:
            result = fs_getblksz(file, arg);
            break;
        
        default:
            return -ENOTSUP;
    }
    
    lock_release(&fs_lock);
    return result;
}






/**
 * fs_getlen - Retrieves the size of a file.
 *
 * @param fd            Pointer to the file's file_struct.
 * @param arg           Pointer to store the file size.
 *
 * @return              Returns 0 on success, or a negative error code on failure.
 */
int fs_getlen(struct file_struct* fd, void* arg) {
    // check if fd and arg are valid
    if (!fd || !arg) {
        return -1;
    }


    // store the file size in the mem location pointed by arg
    *(uint64_t*)arg = fd->file_size;
    return 0;
}






/**
 * fs_getpos - Retrieves the current position within a file.
 *
 * @param fd            Pointer to the file's file_struct.
 * @param arg           Pointer to store the current position.
 *
 * @return              Returns 0 on success, or a negative error code on failure.
 */
int fs_getpos(struct file_struct* fd, void* arg) {
    // check if fd and arg are valid
    if (!fd || !arg) {
        return -1;
    }


    // store the current file position in the memory location pointed by arg
    *(uint64_t*)arg = fd->file_position;
    return 0;
}






/**
 * fs_setpos - Sets the current position within a file.
 *
 * @param fd            Pointer to the file's file_struct.
 * @param arg           Pointer containing the new position.
 *
 * @return              Returns 0 on success, or a negative error code if
 *                      the position is invalid (e.g., beyond file size).
 */
int fs_setpos(struct file_struct* fd, void* arg) {
    // check if fd and arg are valid
    if(!fd || !arg) {
        return -1;
    }
   
    // retrieve new position
    uint64_t new_pos = *(uint64_t*)arg;


    // ensure the new position is smaller than the total file size
    if (new_pos > fd->file_size) {
        return -1;
    }


    // set the file position to the new value
    fd->file_position = new_pos;
    return 0;  
}






/**
 * fs_getblksz - Retrieves the filesystem's block size.
 *
 * @param fd            Pointer to the file's file_struct.
 * @param arg           Pointer to store the block size.
 *
 * @return              Returns 0 on success, or a negative error code on failure.
 */
int fs_getblksz(struct file_struct* fd, void* arg) {
    // check if fd and arg are valid pointers
    if (!fd || !arg) {
        return -1;
    }


    // store block size in memory location pointed by arg
    *(uint64_t*)arg = FS_BLKSZ;
    return 0;
}

