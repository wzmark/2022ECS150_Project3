#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"
#include "disk.c"
#define FAT_EOC 0xFFFF

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
		int8_t unused[10];
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
		char* fdWithFileName[FS_OPEN_MAX_COUNT];
		int fdWithOffset[FS_OPEN_MAX_COUNT];
		int numOfOpenFiles;
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
	if(numOfBlocks != fs->superBlock->numOfFatBlock + fs->superBlock->numOfDataBlock + 2){
		return -1;
	}
	fs->fatBlocks = (FATBlock*)malloc(sizeof(FATBlock) * numOfFatBlock);
	for(int i = 0; i < numOfFatBlock; i++){
		fs->fatBlocks[i].fat = (uint16_t*)malloc(sizeof(uint16_t) * numOfFatBlock * BLOCK_SIZE);
		block_read(i + 1, fs->fatBlocks[i].fat);
	}
	fs->RootDirectory = (RootDirectory*)malloc(sizeof(RootDirectory) * FS_FILE_MAX_COUNT);
	int temp = sizeof(RootDirectory);
	block_read(fs->superBlock->indexOfRootDirectory, fs->RootDirectory);
	printf("%s   %s", fs->RootDirectory[0].filename, fs->RootDirectory[1].filename);
	fs->numOfUnusedRootDirectory = FS_FILE_MAX_COUNT;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		
		if(strcmp(fs->RootDirectory[i].filename, "\0") != 0){
			fs->numOfUnusedRootDirectory -= 1;
		}
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
				
				if(*fs->fatBlocks[i].fat == 0){
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
		fs->RootDirectory[startIndexOfRootDirectory].indexOfFirstBlock = FAT_EOC;
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
		currentFatBlock = &fs->fatBlocks[currentBlock];
		FATBlock* nextFatBlock = (FATBlock*)malloc(sizeof(FATBlock));
		nextFatBlock->fat = (uint16_t*)malloc(sizeof(uint16_t));
		while(*currentFatBlock->fat != FAT_EOC){
				nextFatBlock = &fs->fatBlocks[*currentFatBlock->fat];
				*currentFatBlock->fat = 0;
				currentFatBlock = nextFatBlock;
		}
		
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
	if(FileCheck(filename) == -1){
		return -1;
	}
	int indexOfFile = FindFileLocation(filename);
	if(indexOfFile == -1){
		return -1;
	}
	if(fs->numOfOpenFiles == FS_OPEN_MAX_COUNT){
		return -1;
	}
	
	for(int i = 0; i < fs->numOfOpenFiles; i++){
		if(strcmp(filename, fs->fdWithFileName[i]) != -1){
			return i;
		}
	}
	fs->numOfOpenFiles += 1;
	for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
		if(strcmp(fs->fdWithFileName[i], "") != -1){
			strcpy(fs->fdWithFileName[i], filename);
			return i;
		}
	}
	return -1;
}

int FdCheck(int fd){
	if(fs->isMounted == UNMOUNTED){
		return -1;
	}
	if(fd > FS_OPEN_MAX_COUNT){
		return -1;
	}
	if(strcmp(fs->fdWithFileName[fd], "") != -1){
		return -1;
	}
	return 0;
}

int fs_close(int fd)
{
	if(FdCheck(fd) == -1){
		return -1;
	}
	strcpy(fs->fdWithFileName[fd], "");
	fs->numOfOpenFiles -= 1;
	return 0;
}

int fs_stat(int fd)
{
	if(FdCheck(fd) == -1){
		return -1;
	}
	const char* filename;
	strcpy(filename, fs->fdWithFileName[fd]);
	int indexOfFile = FindFileLocation(filename);
	if(indexOfFile == -1){
		return -1;
	}
	return fs->RootDirectory[indexOfFile].sizeOfFile;
}

int fs_lseek(int fd, size_t offset)
{
	if(FdCheck(fd) == -1){
		return -1;
	}
	if(fs_stat(fd) < offset){
		return -1;
	}
	fs->fdWithOffset[fd] = offset;
	return 0;
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
	fs_info();
	/*
	fs_ls();
	fs_delete("Makefile");
	fs_create("test");
	fs_ls();
	*/
}