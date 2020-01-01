/*
 * memlib.c - a module that simulates the memory system.  Needed because it 
 *            allows us to interleave calls from the student's malloc package 
 *            with the system's malloc package in libc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#include "memlib.h"
#include "config.h"

/* private variables */
static unsigned char heap[MAX_HEAP];
static char *mem_brk = heap; /* points to last byte of heap */
static char *mem_max_addr = heap + MAX_HEAP;  /* largest legal heap address */ 

/* 
 * mem_init - initialize the memory system model
 */
void mem_init(void)
{
  mem_brk = heap;                  /* heap is empty initially */
}

/* 
 * mem_deinit - free the storage used by the memory system model
 */
void mem_deinit(void)
{
  /*    free(mem_start_brk); */
}

/*
 * mem_reset_brk - reset the simulated brk pointer to make an empty heap
 */
void mem_reset_brk()
{
    mem_brk = heap;
}

/* 
 * mem_sbrk - simple model of the sbrk function. Extends the heap 
 *    by incr bytes and returns the start address of the new area. In
 *    this model, the heap cannot be shrunk.
 */
void *mem_sbrk(int incr) 
{
    char *old_brk = mem_brk;												//힙포인터 

    if ( (incr < 0) || ((mem_brk + incr) > mem_max_addr)) {					//인자로 입력된 사이즈가 0보다 작거나 그만큼 힙을 늘렸을때 최대 주소를 넘겨버리면 에러
	errno = ENOMEM;
	fprintf(stderr, "ERROR: mem_sbrk failed. Ran out of memory...\n");
	return (void *)-1;
    }
    mem_brk += incr;														//그렇지않다면 힙 사이즈를 증가시키고 
    return (void *)old_brk;													//이전 힙 포인터, 즉 사이즈 증가되기 전 위치, 새로 할당된부분의 시작지점을 반환  
}

/*
 * mem_heap_lo - return address of the first heap byte
 */
void *mem_heap_lo()
{
    return (void *)heap;													//힙의 가장 낮은부분인 시작주소 반환 
}

/* 
 * mem_heap_hi - return address of last heap byte
 */
void *mem_heap_hi()
{
    return (void *)(mem_brk - 1);											//힙의 가장 높은부분인 끝 주소 반환 
}

/*
 * mem_heapsize() - returns the heap size in bytes
 */
size_t mem_heapsize() 
{
    return (size_t)((void *)mem_brk - (void *)heap);						//힙포인터와 힙시작점 사이의 공간인 힙 사이즈를 반환 
}

/*
 * mem_pagesize() - returns the page size of the system
 */
size_t mem_pagesize()
{
    return (size_t)getpagesize();											//시스템의 페이지 사이즈를 반환 
}
