#ifndef KFS_H
#define KFS_H

// constant definitions

#define FS_BLKSZ      4096
#define FS_NAMELEN    32
#define FS_MAXOPEN    32

// struct definitions

typedef struct dentry_t{
    char file_name[FS_NAMELEN];
    uint32_t inode;
    uint8_t reserved[28];
}__attribute((packed)) dentry_t;


typedef struct boot_block_t{
    uint32_t num_dentry;
    uint32_t num_inodes;
    uint32_t num_data;
    uint8_t reserved[52];
    dentry_t dir_entries[63];
}__attribute((packed)) boot_block_t;


typedef struct inode_t{
    uint32_t byte_len;
    uint32_t data_block_num[1023];
}__attribute((packed)) inode_t;


typedef struct data_block_t{
    uint8_t data[FS_BLKSZ];
}__attribute((packed)) data_block_t;

// boot block; need some data from the boot block for some shell commands

extern struct boot_block_t boot_block;

#endif
