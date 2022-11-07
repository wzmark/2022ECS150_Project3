#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"
#include "disk.c"

enum{
	UNMOUNTED,
	MOUNTED
};

typedef struct __attribute__((packed)){
	uint64_t Signature;
	int16_t numOfBlocks;
	int16_t indexOfRootDirectory;
	int16_t indexOfStartBlock;
	int16_t numOfDataBlock;
	int8_t numOfFatBlock;
	int8_t unused[4079];
}SuperBlock;

typedef struct __attribute__((packed)){
	char filename[FS_FILENAME_LEN];
	int32_t sizeOfFile;
	uint16_t indexOfFirstBlock;
	int unused[10];
}RootDirectory;

typedef struct __attribute((packed)){
	uint16_t* fat;
}FATBlock;

typedef struct __attribute__((packed)){
	SuperBlock *superBlock;
	FATBlock *fatBlocks;
	RootDirectory *RootDirectory;
	int numOfUnusedRootDirectory;
	int isMounted;
}FileSystem;

FileSystem *fs;



int fs_mount(const char *diskname)
{
	fs = (FileSystem*)malloc(sizeof(FileSystem));

	if(block_disk_open(diskname) == -1){
		return -1;
	}
	int numOfBlocks = block_disk_count();
	if(numOfBlocks == -1){
		return -1;
	}
	fs->superBlock = (SuperBlock*)malloc(sizeof(SuperBlock));
	
	if(block_read(0, fs->superBlock)){
		return -1;
	}
	int numOfFatBlock = fs->superBlock->numOfFatBlock;
	fs->fatBlocks = (FATBlock*)malloc(sizeof(FATBlock) * numOfFatBlock);
	
	for(int i = 0; i < numOfFatBlock; i++){
		fs->fatBlocks[i].fat = (uint16_t*)malloc(sizeof(uint16_t) * numOfFatBlock * BLOCK_SIZE);
		block_read(i + 1, fs->fatBlocks[i].fat);
		printf("%d", *fs->fatBlocks[i].fat);
	}
	fs->RootDirectory = (RootDirectory*)malloc(sizeof(RootDirectory) * FS_FILE_MAX_COUNT);
	
	block_read(fs->superBlock->indexOfRootDirectory, fs->RootDirectory);
	fs->numOfUnusedRootDirectory = FS_FILE_MAX_COUNT;
	if(fs->RootDirectory[0].filename != "0x0"){
		fs->numOfUnusedRootDirectory -= 1;
	}
	fs->isMounted = MOUNTED;
}

int fs_umount(void)
{
	block_write(fs->superBlock->indexOfRootDirectory, fs->RootDirectory);
	for(int i = 0; i < fs->superBlock->numOfFatBlock; i++){
		block_write(i + 1, fs->fatBlocks[i].fat);
	}
	fs->isMounted = UNMOUNTED;
	return 0;
}

int CheckUnusedFat(){
	if(fs->superBlock->numOfFatBlock == 0){
		return -1;
	}
	int numOfFat = fs->superBlock->numOfFatBlock;
	int count = 0;
	for(int i = 0; i < numOfFat; i++){
		if(fs->fatBlocks[i].fat == 0){
			count += 1;
		}
	}
	return fs->superBlock->numOfDataBlock - count;
}

int fs_info(void)
{
	printf("FS Info:\n");
	printf("total_blk_count=%d\n", fs->superBlock->numOfBlocks);
	printf("fat_blk_count=%d\n", fs->superBlock->numOfFatBlock);
	printf("rdir_blk=%d\n", fs->superBlock->indexOfRootDirectory);
	printf("data_blk=%d\n", fs->superBlock->indexOfStartBlock);
	printf("data_blk_count=%d\n", fs->superBlock->numOfDataBlock);
	int numOfUnusedBlock = CheckUnusedFat();
	printf("fat_free_ratio=%d/%d\n", numOfUnusedBlock, fs->superBlock->numOfDataBlock);
	printf("rdir_free_ratio=%d/%d\n", fs->numOfUnusedRootDirectory, FS_FILE_MAX_COUNT);
}

int FileCheck(const char *filename){
	if(fs->isMounted == UNMOUNTED){
		return -1;
	}
	if(filename == NULL){
		return -1;
	}
	if(strlen(filename) > FS_FILENAME_LEN || strlen(filename) == 0){
		return -1;
	}
	return 0;
}

int FindFileLocation(const char *filename){
	int startIndexOfRootDirectory = FS_FILE_MAX_COUNT - fs->numOfUnusedRootDirectory;
	for(int i = 0; i < startIndexOfRootDirectory; i++){
		if(strcmp(filename, fs->RootDirectory[i].filename) == 0){
			return i;
		}
	}
	return -1;
}

int fs_create(const char *filename)
{
	if(FileCheck((char*)filename) == -1){
		return -1;
	}
	int startIndexOfRootDirectory = FS_FILE_MAX_COUNT - fs->numOfUnusedRootDirectory;
	if(FindFileLocation(filename) != -1){
		return -1;
	}
	strcpy(fs->RootDirectory[startIndexOfRootDirectory].filename, filename);
	fs->RootDirectory[startIndexOfRootDirectory].sizeOfFile = 0;
	fs->RootDirectory[startIndexOfRootDirectory].indexOfFirstBlock = 65535;
	fs->numOfUnusedRootDirectory -= 1;
	return 0;

}

int fs_delete(const char *filename)
{
	if(FileCheck(filename) == -1){
		return -1;
	}
	int indexOfRootDirectory = FindFileLocation(filename);
	if(indexOfRootDirectory == -1){
		return -1;
	}
	fs->RootDirectory[indexOfRootDirectory].sizeOfFile = 0;
	strcpy(fs->RootDirectory[indexOfRootDirectory].filename, "\0");
	
	int16_t currentBlock = fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock;
	FATBlock* currentFatBlock = (FATBlock*)malloc(sizeof(FATBlock));
	currentFatBlock->fat = (uint16_t*)malloc(sizeof(uint16_t));
	currentFatBlock = &fs->fatBlocks[currentBlock];
	while(*currentFatBlock->fat != 65535){
		FATBlock* nextFatBlock = (FATBlock*)malloc(sizeof(FATBlock));
		nextFatBlock->fat = (uint16_t*)malloc(sizeof(uint16_t));
		nextFatBlock = &fs->fatBlocks[*currentFatBlock->fat];
		*currentFatBlock->fat = 65535;
		currentFatBlock = nextFatBlock;
	}
	fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock = 65535;
	fs->numOfUnusedRootDirectory += 1;
	return 0;

}

int fs_ls(void)
{
	if(fs->isMounted == UNMOUNTED){
		return -1;
	}
	printf("FS Ls:\n");
	for(int i = 0; i < FS_FILE_MAX_COUNT - fs->numOfUnusedRootDirectory; i++){
		printf("file: %s, ", fs->RootDirectory[i].filename);
		printf("size: %d, ", fs->RootDirectory[i].sizeOfFile);
		printf("data_blk: %d\n",  fs->RootDirectory[i].indexOfFirstBlock);
	}
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

int main(){
	fs_mount("disk.fs");
	fs_ls();
	fs_delete("Makefile");
	fs_create("test");
	fs_ls();
}