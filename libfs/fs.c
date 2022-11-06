#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

typedef struct __attribute__((packed)){
	int64_t Signature;
	int16_t numOfBlocks;
	int16_t indexOfRootDirectory;
	int16_t indexOfStartBlock;
	int16_t numOfDataBlock;
	int8_t numOfFatBlock;
	int8_t unused[4079];
}Superblock;

typedef struct __attribute__((packed)){
	char *filename;
	int32_t sizeOfFile;
	int16_t indexOfFirstBlock;
	int unused[10];
}FatEntry;

int fs_mount(const char *diskname)
{
	
}

int fs_umount(void)
{
	/* TODO: Phase 1 */
}

int fs_info(void)
{
	/* TODO: Phase 1 */
}

int fs_create(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_ls(void)
{
	/* TODO: Phase 2 */
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */
}

int fs_close(int fd)
{
	/* TODO: Phase 3 */
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

