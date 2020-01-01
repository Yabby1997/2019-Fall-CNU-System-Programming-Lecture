/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  Blocks are never coalesced or reused.  The size of
 * a block is found at the first aligned word before the block (we need
 * it for realloc).
 *
 * This code is correct and blazingly fast, but very bad usage-wise since
 * it never frees anything.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT - 1))		//사이즈에 7을 더한 후 8배수 수로 올림한다.(~0x7과 and연산이 하위 3비트를 날려줌) 

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))								//사이즈를 나타내기위한 헤더크기 

#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))			//사이즈 정보의 위치

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void)
{
  return 0;
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
void *malloc(size_t size)
{
  int newsize = ALIGN(size + SIZE_T_SIZE);								//인자로 전달받은 할당할 사이즈와 사이즈 정보를 더해서 열맞춤
  unsigned char *p = mem_sbrk(newsize);									//그만큼 메모리의 힙 포인터를 이동시켜 사이즈 확보, 확보된 영역의 시작지점을 p로 받는다.
  //dbg_printf("malloc %u => %p\n", size, p);

  if ((long)p < 0)														
    return NULL;
  else {
    p += SIZE_T_SIZE;													//그렇지 않다면 포인터를 SIZE_T_SIZE만큼 증가시키고
    *SIZE_PTR(p) = size;												//사이즈를 써준다
    return p;
  }
}

/*
 * free - We don't know how to free a block.  So we ignore this call.
 *      Computers have big memories; surely it won't be a problem.
 */
void free(void *ptr)
{
}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.  I'm too lazy
 *      to do better.
 */
void *realloc(void *oldptr, size_t size)
{
  size_t oldsize;
  void *newptr;

  /* If size == 0 then this is just free, and we return NULL. */
  if(size == 0) {														//재할당하는 사이즈가 0이라면 free와 같다
    free(oldptr);														//구현은...안돼있다.
    return 0;
  }

  /* If oldptr is NULL, then this is just malloc. */
  if(oldptr == NULL) {													//재할당할 pointer가 null이라면 malloc과 같다. 
    return malloc(size);
  }

  newptr = malloc(size);												//사이즈 크기의 새로운 메모리를 할당한다. 

  /* If realloc() fails the original block is left untouched  */
  if(!newptr) {															//malloc에 실패하게되면 realloc 종료 
    return 0;
  }

  /* Copy the old data. */
  oldsize = *SIZE_PTR(oldptr);											//oldptr의 사이즈 oldsize
  if(size < oldsize) oldsize = size;									//새로주어진 size가 oldsize보다 작다면 size만큼만 옮겨가야한다. 
  memcpy(newptr, oldptr, oldsize);										//새로 만든 메모리공간 newptr에 oldptr부터 oldsize만큼의 데이터를 카피한다.

  /* Free the old block. */
  free(oldptr);															//새로만든 메모리공간으로 옮겨갔으므로 oldptr의 메모리공간은 free해준다.

  return newptr;														//새로만들어진 메모리의 ptr을 반환해준다. 
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc (size_t nmemb, size_t size)
{
  size_t bytes = nmemb * size;
  void *newptr;

  newptr = malloc(bytes);
  memset(newptr, 0, bytes);

  return newptr;
}

/*
 * mm_checkheap - There are no bugs in my code, so I don't need to check,
 *      so nah!
 */
void mm_checkheap(int verbose)
{
}
