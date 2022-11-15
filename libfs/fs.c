#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"
#include "disk.c"
#define FAT_EOC 0xFFFF
#define FS_NUM_FAT_ENTRIES 2048

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
		fs->fatBlocks[i].fat = (uint16_t*)malloc(sizeof(uint16_t) * BLOCK_SIZE);
		block_read(i + 1, fs->fatBlocks[i].fat);
	}
	fs->RootDirectory = (RootDirectory*)malloc(sizeof(RootDirectory) * FS_FILE_MAX_COUNT);
	
	block_read(fs->superBlock->indexOfRootDirectory, fs->RootDirectory);
	printf("%s   %s", fs->RootDirectory[3].filename, fs->RootDirectory[1].filename);
	fs->numOfUnusedRootDirectory = FS_FILE_MAX_COUNT;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		
		if(strcmp(fs->RootDirectory[i].filename, "\0") != 0){
			fs->numOfUnusedRootDirectory -= 1;
		}
	}
	fs->isMounted = MOUNTED;
	return 0;
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
				for(int j = 0; j < FS_NUM_FAT_ENTRIES; j++){
					if(fs->fatBlocks[i].fat[j] != 0){
						count += 1;
					}
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
		return 0;
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
		
		for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
				if(strcmp(filename, fs->RootDirectory[i].filename) == 0){
						return i;
				}
		}
		return -1;
}

int FindUnusedRootLocation(){
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
				if(strlen(fs->RootDirectory[i].filename) == 0){
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
		int startIndexOfRootDirectory = FindUnusedRootLocation();
		if(FindFileLocation(filename) != -1 || startIndexOfRootDirectory == -1){
				return -1;
		}
		strcpy(fs->RootDirectory[startIndexOfRootDirectory].filename, filename);
		fs->RootDirectory[startIndexOfRootDirectory].sizeOfFile = 0;
		fs->RootDirectory[startIndexOfRootDirectory].indexOfFirstBlock = FAT_EOC;
		fs->numOfUnusedRootDirectory -= 1;
		return 0;

}

void FindFatNextLocation(int location, int *indexOfBlock, int *indexInBlock){
	*indexOfBlock = location / FS_NUM_FAT_ENTRIES;
	*indexInBlock = location - FS_NUM_FAT_ENTRIES * *indexOfBlock;
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
		
		int16_t indexOfCurrentFat = fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock;
		int* indexOfBlock = (int*)malloc(sizeof(int));
		int* indexInBlock = (int*)malloc(sizeof(int));
		FindFatNextLocation(indexOfCurrentFat, indexOfBlock, indexInBlock);
		uint16_t* currentFat = (uint16_t*)malloc(sizeof(uint16_t));
		currentFat = fs->fatBlocks[*indexOfBlock].fat[*indexInBlock];
		uint16_t* nextFat = (uint16_t*)malloc(sizeof(uint16_t));
		while(currentFat != FAT_EOC){
				FindFatNextLocation(currentFat, indexOfBlock, indexInBlock);
				nextFat = fs->fatBlocks[*indexOfBlock].fat[*indexInBlock];
				*currentFat = 0;
				currentFat = nextFat;
		}
		currentFat = 0;
		fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock = FAT_EOC;
		fs->numOfUnusedRootDirectory += 1;
		return 0;

}

int fs_ls(void)
{
		if(fs->isMounted == UNMOUNTED){
				return -1;
		}
		printf("FS Ls:\n");
		for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
				if(strlen(fs->RootDirectory[i].filename) != 0){
						printf("file: %s, ", fs->RootDirectory[i].filename);
						printf("size: %d, ", fs->RootDirectory[i].sizeOfFile);
						printf("data_blk: %d\n",  fs->RootDirectory[i].indexOfFirstBlock);
				}
				
		}
		return 0;
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
	fs->fdWithOffset[fd] = 0;
	fs->numOfOpenFiles -= 1;
	return 0;
}

int fs_stat(int fd)
{
	if(FdCheck(fd) == -1){
		return -1;
	}
	char filename[FS_FILENAME_LEN];
	strcpy(filename, fs->fdWithFileName[fd]);
	const char* constFilename = filename;
	int indexOfFile = FindFileLocation(constFilename);
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

int FindUnusedFatLocation(){
	int* indexOfBlock = (int*)malloc(sizeof(int));
	int* indexInBlock = (int*)malloc(sizeof(int));
	for(int i = 1; i < FS_NUM_FAT_ENTRIES; i++){
		FindFatNextLocation(i, indexOfBlock, indexInBlock);
		uint16_t currentFat = fs->fatBlocks[*indexOfBlock].fat[*indexInBlock];
		if(currentFat == 0){
			free(indexInBlock);
			free(indexOfBlock);
			return i;
		}
	}
	return -1;
	
}

int fs_write(int fd, void *buf, size_t count)
{
	if(FdCheck(fd) == -1){
		return -1;
	}
	if(buf == NULL){
		return -1;
	}
	char filename[FS_FILENAME_LEN];
	strcpy(filename, fs->fdWithFileName[fd]);
	int indexOfUnusedFatBlock = FindUnusedFatLocation();
	if(indexOfUnusedFatBlock == -1){
		//no available fat
		return -1;
	}
	const char *constFilename = filename;
	int indexOfRootDirectory = FindFileLocation(constFilename);
	int startOfFat = fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock;
	if(startOfFat == FAT_EOC){
		startOfFat = indexOfUnusedFatBlock;
		fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock = indexOfUnusedFatBlock;
	}
	void* partOfBuffer = malloc(BLOCK_SIZE);
	int numOfWriteTimes = count / BLOCK_SIZE;
	if(count > BLOCK_SIZE * numOfWriteTimes){
		//add one more times 
		numOfWriteTimes += 1;
	}
	int remainSize = count;
	int actualSize = 0;
	for(int i = 0; i < numOfWriteTimes; i++){
		if(remainSize > BLOCK_SIZE){
			memcpy(partOfBuffer, buf + BLOCK_SIZE * i, BLOCK_SIZE);
			actualSize += BLOCK_SIZE;
		}else{
			memcpy(partOfBuffer, buf + BLOCK_SIZE * i, remainSize);
			actualSize += remainSize;
			
		}
		
		remainSize -= BLOCK_SIZE;
		block_write(fs->superBlock->indexOfStartBlock + indexOfUnusedFatBlock , partOfBuffer);
		indexOfUnusedFatBlock = FindUnusedFatLocation();
		if(indexOfUnusedFatBlock == -1){
			break;
		}

		
	}
	fs->RootDirectory[indexOfRootDirectory].sizeOfFile = actualSize;
	return actualSize;
	
}

int fs_read(int fd, void *buf, size_t count)
{
	if(FdCheck(fd) == -1){
		return -1;
	}
	char filename[FS_FILENAME_LEN];
	strcpy(filename, fs->fdWithFileName[fd]);
	
	const char *constFilename = filename;
	int indexOfRootDirectory = FindFileLocation(constFilename);
	if(fs->fdWithOffset[fd] > fs->RootDirectory[indexOfRootDirectory].sizeOfFile){
		return -1;
	}
	int numOfReadTimes = count / BLOCK_SIZE;
	if(count > numOfReadTimes * BLOCK_SIZE ){
		numOfReadTimes += 1;
	}
	int* indexOfBlock = (int*)malloc(sizeof(int));
	int* indexInBlock = (int*)malloc(sizeof(int));
	int remainSize = count;
	int actualSize = 0;
	void* partOfBuffer = malloc(BLOCK_SIZE);
	int indexOfFat = fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock;
	for(int i = 0; i < numOfReadTimes; i++){
		
		
		
		block_read(fs->superBlock->indexOfStartBlock + indexOfFat , partOfBuffer);
		FindFatNextLocation(indexOfFat, indexOfBlock, indexInBlock);
		indexOfFat = fs->fatBlocks[*indexOfBlock].fat[*indexInBlock];
		if(remainSize > BLOCK_SIZE){
			memcpy(buf + BLOCK_SIZE * i, partOfBuffer, BLOCK_SIZE);
			actualSize += BLOCK_SIZE;
		}else{
			memcpy(buf + BLOCK_SIZE * i, partOfBuffer, remainSize);
			actualSize += remainSize;
		}
		remainSize -= BLOCK_SIZE;
		
	}
	return actualSize;
}

int main(){
	fs_mount("disk.fs");
	fs_info();
	fs_ls();
	fs_delete("Makefile");
	fs_ls();
}