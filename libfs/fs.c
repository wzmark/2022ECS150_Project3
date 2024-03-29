#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "disk.c"
#include "fs.h"
#define FAT_EOC 0xFFFF
#define FS_NUM_FAT_ENTRIES 2048
#define SIGNATURE 6000536558536704837



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
		uint64_t fdWithOffset[FS_OPEN_MAX_COUNT];
		int numOfOpenFiles;
}FileSystem;

FileSystem *fs;



int fs_mount(const char *diskname)
{
	fs = (FileSystem*)malloc(sizeof(FileSystem));
	// check if the disk can be open or not
	if(block_disk_open(diskname) == -1){
			return -1;
	}
	// get the number of block and check if the number is correct
	int numOfBlocks = block_disk_count();
	if(numOfBlocks == -1){
			return -1;
	}
	fs->superBlock = (SuperBlock*)malloc(sizeof(SuperBlock));
	// read the superblock from disk
	if(block_read(0, fs->superBlock)){
			return -1;
	}
	// check the signature of the file system correspond 
	// to the one defined by the specifications
	if (fs->superBlock->Signature != SIGNATURE){
			return -1;
	}
	int numOfFatBlock = fs->superBlock->numOfFatBlock;
	// check if number of block is correct
	// total # of block = # of fat + # of data + superblock + rootdirectory
	if(numOfBlocks != fs->superBlock->numOfFatBlock + fs->superBlock->numOfDataBlock + 2){
			return -1;
	}
	fs->fatBlocks = (FATBlock*)malloc(sizeof(FATBlock) * numOfFatBlock);
	// read all the fat blocks
	for(int i = 0; i < numOfFatBlock; i++){
			fs->fatBlocks[i].fat = (uint16_t*)malloc(sizeof(uint16_t) * BLOCK_SIZE);
			block_read(i + 1, fs->fatBlocks[i].fat);
	}
	fs->RootDirectory = (RootDirectory*)malloc(sizeof(RootDirectory) * FS_FILE_MAX_COUNT);
	// read the root directory
	block_read(fs->superBlock->indexOfRootDirectory, fs->RootDirectory);
	// printf("%s   %s", fs->RootDirectory[3].filename, fs->RootDirectory[1].filename);
	// check the number of unused root directory (# of unused file)
	fs->numOfUnusedRootDirectory = FS_FILE_MAX_COUNT;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
			// if file name is not empty, discount 1 from 32
			if(strcmp(fs->RootDirectory[i].filename, "\0") != 0){
					fs->numOfUnusedRootDirectory -= 1;
			}
	}
	fs->isMounted = MOUNTED;
	// allocate memory for saving the file name
	for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
			fs->fdWithFileName[i] = (char*)malloc(sizeof(char) * FS_FILENAME_LEN);
	}
	return 0;
}

int fs_umount(void)
{
		// write root directory into disk
		block_write(fs->superBlock->indexOfRootDirectory, fs->RootDirectory);
		// write fat block into disk
		for(int i = 0; i < fs->superBlock->numOfFatBlock; i++){
				block_write(i + 1, fs->fatBlocks[i].fat);
		}
		fs->isMounted = UNMOUNTED;
		// free data structure: filesystem, fatblock, RootDirectory
		// char* fdWithFileName[FS_OPEN_MAX_COUNT], superBlock
		for(int i = 0; i < fs->superBlock->numOfFatBlock; i++){
				free(fs->fatBlocks[i].fat);
		}
		free(fs->superBlock);
		free(fs->fatBlocks);
		free(fs->RootDirectory);
		for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
				free(fs->fdWithFileName[i]);
		}
		free(fs);
		return 0;
}

int CheckUnusedFat(){
		if(fs->superBlock->numOfFatBlock == 0){
				return -1;
		}
		// get the number of fat block and check number of used fat block
		int numOfFat = fs->superBlock->numOfFatBlock;
		int count = 0;
		for(int i = 0; i < numOfFat; i++){
				for(int j = 0; j < FS_NUM_FAT_ENTRIES; j++){
						if(fs->fatBlocks[i].fat[j] != 0){
								count += 1;
						}
				}
		}
		// # of datablock - # of used fat = # of unused fat
		return fs->superBlock->numOfDataBlock - count;
}

int fs_info(void)
{
		// print the file system information based on reference
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
	// check if the file is mounte or not
	// check if filename is correct(NULL, more than 16 chars, no file name)
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
		// based on filename find the index of entry based on filename
		for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
				if(strcmp(filename, fs->RootDirectory[i].filename) == 0){
						return i;
				}
		}
		return -1;
}

int FindUnusedRootLocation(){
	// return the first find ununsed root location for file
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
				if(strlen(fs->RootDirectory[i].filename) == 0){
						return i;
				}
		}
	return -1;
}

int fs_create(const char *filename)
{
		// check if FS is not mount, filename invalid
		if(FileCheck((char*)filename) == -1){
				return -1;
		}
		// get the root index of this new file
		// -1 if all root location are used(already have 128 files)
		int startIndexOfRootDirectory = FindUnusedRootLocation();
		// check if the filename has been used
		// check if root directory already contains 128 files
		if(FindFileLocation(filename) != -1 || startIndexOfRootDirectory == -1){
				return -1;
		}
		// initialization of new file
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
		// check if FS is not mount, filename invalid
		if(FileCheck(filename) == -1){
				return -1;
		}
		// check if file not exist
		int indexOfRootDirectory = FindFileLocation(filename);
		if(indexOfRootDirectory == -1){
				return -1;
		}
		// need to check if file is open
		// if the file is open, return -1
		for (int i = 0; i < FS_OPEN_MAX_COUNT; i++){
				if(strcmp(filename, fs->fdWithFileName[i]) == 0){
						return -1;
				}
		}
		// set the size of file to 0
		fs->RootDirectory[indexOfRootDirectory].sizeOfFile = 0;
		// clean the filename inside root directory
		for(int i = 0; i < FS_FILENAME_LEN; i++){
				strcpy(fs->RootDirectory[indexOfRootDirectory].filename + i, "\0");
		}
		int16_t indexOfCurrentFat = fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock;
		int* indexOfBlock = (int*)malloc(sizeof(int));
		int* indexInBlock = (int*)malloc(sizeof(int));
		// get the fat index
		FindFatNextLocation(indexOfCurrentFat, indexOfBlock, indexInBlock);
		uint16_t* currentFat = (uint16_t*)malloc(sizeof(uint16_t));
		currentFat = &(fs->fatBlocks[*indexOfBlock].fat[*indexInBlock]);
		uint16_t* nextFat = (uint16_t*)malloc(sizeof(uint16_t));
		// set the fat block belong to this file to 0
		while(*currentFat != FAT_EOC){
				FindFatNextLocation(*currentFat, indexOfBlock, indexInBlock);
				nextFat = &(fs->fatBlocks[*indexOfBlock].fat[*indexInBlock]);
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
		// set corrosponding offset to 0
		fs->numOfOpenFiles += 1;
		for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
			
				if(strlen(fs->fdWithFileName[i]) == 0){
						strcpy(fs->fdWithFileName[i], filename);
						fs->fdWithOffset[i] = 0;
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
		if(strlen(fs->fdWithFileName[fd]) == 0){
				return -1;
		}
		return 0;
}

int fs_close(int fd)
{
	// check fd
	// (including check if fs is mount, fd>32, file not exist)
	if(FdCheck(fd) == -1){
		return -1;
	}
	// set offset to 0. name to empty, number of exist file -1
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
	// get the filename based on fd
	char filename[FS_FILENAME_LEN];
	strcpy(filename, fs->fdWithFileName[fd]);
	const char* constFilename = filename;
	// find file's place in root diractory
	// if not find return -1
	int indexOfFile = FindFileLocation(constFilename);
	if(indexOfFile == -1){
		return -1;
	}
	// if find, return the size of file
	return fs->RootDirectory[indexOfFile].sizeOfFile;
}

int fs_lseek(int fd, size_t offset)
{
	if(FdCheck(fd) == -1){
		return -1;
	}
	size_t checkSize = fs_stat(fd);
	// offset cannot be larger than the file size
	if(checkSize < offset){
		return -1;
	}
	// move to the new offset
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
		if(count == 0){
				return -1;
		}
		if(!buf){
				return -1;
		}
		//set filename
		char filename[FS_FILENAME_LEN];
		strcpy(filename, fs->fdWithFileName[fd]);
		//set offset
		uint64_t offsetOfFile = fs->fdWithOffset[fd];
		const char *constFilename = filename;
		//find RootDirectory by filename
		int indexOfRootDirectory = FindFileLocation(constFilename);
		int indexOfUnusedFatBlock;
		//check whether offset is bigger than file
		if(fs->fdWithOffset[fd] > (uint64_t)fs->RootDirectory[indexOfRootDirectory].sizeOfFile){
				return -1;
		}
		//number of block need to read from disk
		int numOfReadTimes = (count + (int)offsetOfFile) / BLOCK_SIZE;
		size_t checkSize = numOfReadTimes * BLOCK_SIZE;
		if((count + (int)offsetOfFile) > checkSize){
				numOfReadTimes += 1;
				checkSize = (count + (int)offsetOfFile - checkSize) + numOfReadTimes * BLOCK_SIZE;
		}

		int* indexOfBlock = (int*)malloc(sizeof(int));
		int* indexInBlock = (int*)malloc(sizeof(int));
		int remainSize = count;
		int actualSize = 0;
		int* actualSizeInEachBlock = (int*)malloc(sizeof(int) * numOfReadTimes);
		int indexOfFat = fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock;
		int startOffsetInBlock = 0;
		//find index of block and start position in block by offset
		if(offsetOfFile != 0){
				int fatOffsetPoint = offsetOfFile / BLOCK_SIZE;
				indexOfFat = indexOfFat + fatOffsetPoint;
				startOffsetInBlock = offsetOfFile - (fatOffsetPoint * BLOCK_SIZE);
				//numOfReadTimes += 1;
		}
		void* partOfBuffer = malloc(BLOCK_SIZE);
		void* bufferStoreLargeBlock = malloc(BLOCK_SIZE * numOfReadTimes);
		uint16_t* currentFat = (uint16_t*)malloc(sizeof(uint16_t));
		//read block from disk
		for(int i = 0; i < numOfReadTimes; i++){
				if(indexOfFat == -1 || indexOfFat == FAT_EOC){
						break;
				}
				block_read(fs->superBlock->indexOfStartBlock + indexOfFat , partOfBuffer);
				FindFatNextLocation(indexOfFat, indexOfBlock, indexInBlock);
				indexOfFat = fs->fatBlocks[*indexOfBlock].fat[*indexInBlock];
				if(remainSize > BLOCK_SIZE){
						memcpy(bufferStoreLargeBlock +  BLOCK_SIZE * i, partOfBuffer, BLOCK_SIZE);
						actualSize += BLOCK_SIZE;
						remainSize -= BLOCK_SIZE;
				}else{
						memcpy(bufferStoreLargeBlock + BLOCK_SIZE * i, partOfBuffer, BLOCK_SIZE);
						actualSize += remainSize;
						remainSize -= remainSize;
				}
				
				
		}
		
		//overwrite block
		int countOfBlock = 0;
		int sizeOfDataInBlock = 0;
		for(int i = 0; (size_t)i < count; i++){
			
				memcpy(bufferStoreLargeBlock + startOffsetInBlock + i, buf + i, 1);
				sizeOfDataInBlock += 1;
				if((size_t)i == count - 1){
						actualSizeInEachBlock[countOfBlock] = sizeOfDataInBlock;
						countOfBlock += 1;
						sizeOfDataInBlock = 0;
				}else if ((startOffsetInBlock + i) % BLOCK_SIZE == 0 && (startOffsetInBlock + i) != 0){
						actualSizeInEachBlock[countOfBlock] = sizeOfDataInBlock;
						countOfBlock += 1;
						sizeOfDataInBlock = 0;
				}
			
		}
		indexOfFat = fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock;
		if(offsetOfFile != 0){
				int fatOffsetPoint = offsetOfFile / BLOCK_SIZE;
				indexOfFat = indexOfFat + fatOffsetPoint;
		}
		
		if(indexOfFat == FAT_EOC){
				indexOfUnusedFatBlock = FindUnusedFatLocation();
				if(indexOfUnusedFatBlock == -1){
						return 0;
				}
				*currentFat = indexOfUnusedFatBlock;
				fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock = *currentFat;
		}else{
			
			*currentFat = indexOfFat;
		}
		actualSize = 0;
		//write block back to the disk
		for(int i = 0; i < numOfReadTimes; i++){
				memcpy(partOfBuffer, bufferStoreLargeBlock + BLOCK_SIZE * i, BLOCK_SIZE);
				block_write(fs->superBlock->indexOfStartBlock + *currentFat , partOfBuffer);
				actualSize += actualSizeInEachBlock[i];
				if(i == numOfReadTimes - 1){
					
						break;
				}
				if(*currentFat == FAT_EOC){
						indexOfUnusedFatBlock = FindUnusedFatLocation();
						
						if(indexOfUnusedFatBlock == -1){
								break;
						}
						*currentFat = indexOfUnusedFatBlock;
				}
				FindFatNextLocation(*currentFat, indexOfBlock, indexInBlock);
				currentFat = &(fs->fatBlocks[*indexOfBlock].fat[*indexInBlock]);

		}
		
		fs->RootDirectory[indexOfRootDirectory].sizeOfFile += actualSize;
		fs->fdWithOffset[fd]  += actualSize;
		free(bufferStoreLargeBlock);
		free(partOfBuffer);
		return actualSize;

}

int fs_read(int fd, void *buf, size_t count)
{
		if(FdCheck(fd) == -1){
				return -1;
		}
		if(count == 0){
				return -1;
		}
		if(!buf){
				return -1;
		}
		char filename[FS_FILENAME_LEN];
		strcpy(filename, fs->fdWithFileName[fd]);
		uint64_t offsetOfFile = fs->fdWithOffset[fd];
		const char *constFilename = filename;
		int indexOfRootDirectory = FindFileLocation(constFilename);
		if(fs->fdWithOffset[fd] > (uint64_t)fs->RootDirectory[indexOfRootDirectory].sizeOfFile){
				return -1;
		}
		int numOfReadTimes = (count + (int)offsetOfFile) / BLOCK_SIZE;
		size_t checkSize = numOfReadTimes * BLOCK_SIZE;
		if((count + (int)offsetOfFile) > checkSize){
				numOfReadTimes += 1;
				checkSize = (count + (int)offsetOfFile - checkSize) + numOfReadTimes * BLOCK_SIZE;
		}
		int* indexOfBlock = (int*)malloc(sizeof(int));
		int* indexInBlock = (int*)malloc(sizeof(int));
		int remainSize = count;
		int actualSize = 0;
		void* partOfBuffer = malloc(BLOCK_SIZE);
		void* bufferStoreLargeBlock = malloc(BLOCK_SIZE * numOfReadTimes);
		int indexOfFat = fs->RootDirectory[indexOfRootDirectory].indexOfFirstBlock;
		int startOffsetInBlock = 0;
	
		if(offsetOfFile != 0){
				int fatOffsetPoint = offsetOfFile / BLOCK_SIZE;
				indexOfFat = indexOfFat + fatOffsetPoint;
				startOffsetInBlock = offsetOfFile - (fatOffsetPoint * BLOCK_SIZE);
		}

		FindFatNextLocation(indexOfFat, indexOfBlock, indexInBlock);
		for(int i = 0; i < numOfReadTimes; i++){
				block_read(fs->superBlock->indexOfStartBlock + indexOfFat , partOfBuffer);
				FindFatNextLocation(indexOfFat, indexOfBlock, indexInBlock);
				indexOfFat = fs->fatBlocks[*indexOfBlock].fat[*indexInBlock];
				if(remainSize > BLOCK_SIZE){
						memcpy(bufferStoreLargeBlock +  BLOCK_SIZE * i, partOfBuffer, BLOCK_SIZE);
						actualSize += BLOCK_SIZE;
						remainSize -= BLOCK_SIZE;
				}else{
						memcpy(bufferStoreLargeBlock + BLOCK_SIZE * i, partOfBuffer, BLOCK_SIZE);
						actualSize += remainSize;
						remainSize -= remainSize;
				}
				if(indexOfFat == -1){
						break;
				}
				
		}
		uint64_t endOffset = offsetOfFile + actualSize;
		
		memcpy(buf, bufferStoreLargeBlock + startOffsetInBlock, actualSize);
		free(bufferStoreLargeBlock);
		free(partOfBuffer);

		fs->fdWithOffset[fd] = endOffset;
		return actualSize;
}
