#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"
#include "fcntl.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "proc.h"
#include "syscall.h"

#define MMAPBASE 0x40000000

int main(void) {
	int size;
	int fd;
	
	/*	
	printf(1, "TEST1\n");
	size = 4096;
	char* text1 = (char*)mmap(0, size, PROT_READ, MAP_POPULATE, -1, 0);
	printf(1, "mmap return %d\n", text1);
	*/
	
	/*	
	printf(1, "\nTEST2\n");
	size = 4096;
	fd = open("README", O_RDONLY);
	char* text2 = (char*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_POPULATE, fd, 0);
	printf(1, "mmap return %d\n", text2);
	*/

	/*	
	printf(1, "\nTEST3\n");
	size = 8192;
	fd = open("README", O_RDONLY);
	char* text3 = (char*)mmap(0, size, PROT_READ, MAP_POPULATE, fd, 0);
	printf(1, "mmap return %d\n", text3);
	printf(1, "text[0]: %c\n", text3[0]);
	*/
	
	/*
	printf(1, "\nTEST4\n");
	size = 8192;
	fd = open("README", O_RDONLY);
	char* text4 = (char*)mmap(0, size, PROT_READ, MAP_ANONYMOUS|MAP_POPULATE, fd, 0);
	printf(1, "mmap return %d\n", text4);
	printf(1, "text[0]: %d\n", text4[0]);
	*/

	/*	
	printf(1, "\nTEST5\n");
	size = 8192;
	fd = open("README", O_RDONLY);
	printf(1, "freemem %d\n", freemem());
	char* text5 = (char*)mmap(0, size, PROT_READ, 0, fd, 0);
	printf(1, "mmap return %d\n", text5);
	printf(1, "freemem %d\n", freemem());
	printf(1, "text[4100]: %c\n", text5[4100]);
	printf(1, "freemem %d\n", freemem());
	printf(1, "text[300]: %c\n", text5[300]);
	printf(1, "freemem %d\n", freemem());
	*/
	
	/*	
	printf(1, "\nTEST6\n");
	size = 8192;
	fd = open("README", O_RDONLY);
	char* text6 = (char*)mmap(0, size, PROT_READ, 0, fd, 0);
	printf(1, "mmap %d\n", text6);
	printf(1, "text[9000]: %c\n", text6[9000]);
	*/

	/*
	printf(1, "\nTEST7\n");
	size = 8192;
	fd = open("README", O_RDONLY);
	char* text7 = (char*)mmap(0, size, PROT_READ, 0, fd, 0);
	printf(1, "mmap return %d\n", text7);
	text7[5000] = '5';
	*/
	
	// int Return;

	/*	
	printf(1, "\nTEST8\n");
	size = 8192;
	fd = open("README", O_RDONLY);
	printf(1, "freemem %d\n", freemem());
	char* text8 = (char*)mmap(0, size, PROT_READ, MAP_POPULATE, fd, 0);
	printf(1, "mmap return %d\n", text8);
	printf(1, "freeemem %d\n", freemem());
	Return = munmap(0 + MMAPBASE);
	printf(1, "munmap return %d\n", Return);
	printf(1, "freemem %d\n", freemem());
	*/

	/*	
	printf(1, "\nTEST9\n");
	size = 8192;
	fd = open("README", O_RDONLY);
	printf(1, "freemem %d\n", freemem());
	char* text9 = (char*)mmap(size, size, PROT_READ, MAP_POPULATE, fd, 0);
	printf(1, "mmap return %d\n", text9);
	printf(1, "freemem %d\n", freemem());
	Return = munmap(0 + MMAPBASE);
	printf(1, "munmap return %d\n", Return);
	printf(1, "freemem %d\n", freemem());
	*/
	
	
	printf(1, "\nTest10\n");
	size = 8192;
	fd = open("README", O_RDWR);
	printf(1, "freemem %d\n", freemem());
	char* text10 = (char*)mmap(0, size, PROT_READ | PROT_WRITE, MAP_POPULATE, fd, 0);
	printf(1, "mmap return %d\n", text10);
	printf(1, "freemem %d\n", freemem());
	printf(1, "parent text[110]: %c\n", text10[110]);
	text10[110] = '9';
	printf(1, "parent text[110]: %c\n", text10[110]);

	int Fork;
	if( (Fork = fork()) == 0){
		printf(1, "child freemem %d \n", freemem());
		printf(1, "child text[110]: %c\n", text10[110]);
		text10[110] = '7';
		printf(1, "child text[110]: %c\n", text10[110]);
	}
	else{
		wait();
		printf(1, "parent freemem %d\n", freemem());
		printf(1, "parent text[110]: %c\n", text10[110]);
	}
	
	
	return 0;
	exit();
}
