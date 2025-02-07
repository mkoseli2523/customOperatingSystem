//           fs.h - File system interface
//          

#ifndef _FS_H_
#define _FS_H_

#include "io.h"

extern char fs_initialized;

extern void fs_init(void);
extern int fs_mount(struct io_intf * blkio);
extern int fs_open(const char * name, struct io_intf ** ioptr);
void fs_close(struct io_intf *io);
long fs_read(struct io_intf *io, void *buf, unsigned long n);
long fs_write(struct io_intf *io, const void *buf, unsigned long n);
int fs_ioctl(struct io_intf *io, int cmd, void *arg);
//int fs_getlen(struct file_struct *fd, void *arg) {
//int fs_getpos(struct file_struct *fd, void *arg) {
//int fs_setpos(struct file_struct *fd, void *arg) {
//int fs_getblksz(struct file_struct *fd, void *arg) {
//extern void fs_close(struct io intf* io);
///extern long fs_write(struct io intf* io, const void* buf, unsigned long n);
//extern long fs_read(struct io intf* io, void* buf, unsigned long n);
//extern int fs_ioctl(struct io_intf *io, int cmd, void *arg){
	

//           _FS_H_
#endif