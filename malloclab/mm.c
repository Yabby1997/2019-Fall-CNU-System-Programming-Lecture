/*
 * mm-implicit.c - an empty malloc package
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 *
 * @id : a201602022 
 * @name : SEUNGHUN YANG 
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
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

//매크로 정의 
#define WSIZE 4																	//워드사이즈 4
#define DSIZE 8																	//더블워드 사이즈 8
#define CHUNKSIZE (1<<12)														//청크사이즈 4096
#define OVERHEAD 8																//header + footer 를 위한 오버헤드 공간 8
#define MAX(x, y) ((x) > (y) ? (x) : (y))										//x, y중 큰 값 
#define PACK(size, alloc) ((size) | (alloc))									//size or alloc으로 size와 alloc값을 더한다. size 는 8배수라 하위 3비트를 안쓴다.
#define GET(p) (*(unsigned int*)(p))											//포인터 p의 위치의 unsigned int를 가져온다. 
#define PUT(p, val) (*(unsigned int*)(p) = (val))								//포인터 p의 위치에 unsigned int를 쓴다.
#define GET_SIZE(p) (GET(p) & ~(ALIGNMENT - 1))									//p의 위치에서 가져온 unsigned int는 사이즈alloc을 모두 포함한다. 1 ... 1000과 and해 사이즈만 가져옴.
#define GET_ALLOC(p) (GET(p) & 0x1)												//p의 위치에서 가져온 unsigned int에 0000 ... 0001을 and해 alloc정보만 가져온다. 
#define HDRP(bp) ((char*)(bp) - WSIZE)											//블럭 포인터 (payload의 시작주소) 부터 한 워드만큼 빼주면 헤더의 주소가 된다. 헤더는 한워드 크기다.
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - OVERHEAD)					//사이즈 + bp - 오버헤드(뒷header + 현footer) 해주면 푸터의 주소가 된다. 푸터도 한워드 크기.
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)))						//현 블럭 포인터에서 사이즈만큼만큼 더하면 다음블럭의 주소
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char*)(bp) - OVERHEAD)))		//앞 블럭의 푸터에서 사이즈를 얻어내서 현블럭에서 빼면 앞블럭의 주소


static char *heap_listp = 0;											//힙리스트의 포인터
static char *fit_ptr = 0;												//next_fit을 위한 fit포인터

static void *coalesce(void *bp){
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));                 //앞블럭의 footer로부터 alloc여부를 얻어온다.
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));                 //다음블럭의 header로부터 alloc여부를 얻어온다.
	size_t size = GET_SIZE(HDRP(bp));                                   //현재블럭의 사이즈도 구한다.

	if(prev_alloc && next_alloc){                                       //앞 뒤 모두 할당되어있다면 coalesce할 수 없다. 그냥 인자로 입력된 bp 반환
		return bp;
	}

	else if(prev_alloc && !next_alloc){                                 //앞은 할당되어있지만 뒤는 가용일 경우 현재와 뒤를 합쳐줄 수 있다.
	    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));							//사이즈를 증가시킨다.
		PUT(HDRP(bp), PACK(size, 0));                                   //현재 블럭부터 뒷블럭까지 포함시키는것이므로 현재블럭의 헤더에 새로운 사이즈를 넣어준다.
		PUT(FTRP(bp), PACK(size, 0));                                   //footer에도 마찬가지로 넣어준다. 현재의 헤더 사이즈값이 이미 바뀌었으므로 현재 푸터의 값을 바꾸면 됨.
	}

	else if(!prev_alloc && next_alloc){                                 //뒤는 할당되어있지만 앞은 가용일 경우
	    size += GET_SIZE(HDRP(PREV_BLKP(bp)));							//사이즈를 증가시킨다.
		PUT(FTRP(bp), PACK(size, 0));                                   //푸터는 현재 블럭의 것을 변경해야하고
	    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));                        //헤더는 이전 블럭의 것을 변경해야한다. 
	    bp = PREV_BLKP(bp);                                             //block 의 시작점이 이전 블럭으로 변경되었으므로 bp가 가리키는곳을 이전 블럭으로 옮겨준다. 
	}

	else if(!prev_alloc && !next_alloc){                                                                           //뒤와 앞 모두 가용인 경우
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));		//사이즈를 증가시킨다.
	    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));                                    //헤더는 이전 블럭의 것을 변경해야하고
	    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));                                    //푸터는 이후 블럭의 것을 변경해야한다.
	    bp = PREV_BLKP(bp);                                                         //block 의 시작지점이 이전 블럭으로 변경되었으므로 bp가 가리키는곳을 이전 블럭으로 옮겨준다.
	}
	
	if(fit_ptr > bp){
   		fit_ptr = bp; 													//coalesce로 통합되어 새로운 가용블럭이 되었다. fit 포인터를 해당 블럭의 위치에 맞춰준다. 
	}

	return bp;                                                          //coalescing이 끝난 블럭의 주소를 반환해준다. 
}

static void *extend_heap(size_t words){
   char *bp;
   size_t size;

   size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;           //워드가 홀수개라면(mod연산 결과 1) 홀수개만큼 워드를 할당, 아니라면 짝수개만큼 할당한다. 

   if ((long)(bp = mem_sbrk(size)) == -1)
	   return NULL;                                                    //만약 할당에 실패하면 NULL반환 

   PUT(HDRP(bp), PACK(size, 0));                                       //free block header
   PUT(FTRP(bp), PACK(size, 0));                                       //free block footer
   PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));                               //new header

   return coalesce(bp);                                                //가용블럭이 있다면 합쳐준다.
}

static void *find_fit(size_t asize){									//next-fit 방식으로 구현한다. 
	char *bp = fit_ptr;

	for(bp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){				//next fit의 탐색 위치를 기록하는 전역변수 fit_ptr을 이용, 탐색이 끝난곳부터 재탐색. 
		if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){		//탐색된 위치 뒤쪽에서 사이즈가 0보다 크고 할당할 사이즈보다 작으며 할당되지 않은 가용블럭을 찾아 넣어준다. 
			return bp;													//해당 블럭의 포인터를 반환해줌 
		}
	}
	return NULL;                                                        //찾지못한다면 NULL반환
}

static void place(void *bp, size_t asize){
	size_t csize = GET_SIZE(HDRP(bp));                                  //인자로 입력된 블럭의 사이즈 csize

	if((csize - asize) >= (DSIZE + OVERHEAD)){                          //csize에서 할당할 사이즈인 asize를 빼고도 한 블럭이 들어올 최소 사이즈 (오버헤드와 데이터해서 16)이 된다면
		PUT(HDRP(bp), PACK(asize, 1));                                  //bp에 asize만큼만 블럭을 새로만들어 header footer를 해주고 남은 사이즈에 새로운 free블럭을 만들어줘도 된다.
	    PUT(FTRP(bp), PACK(asize, 1));                                  //split하는 것.
	    bp = NEXT_BLKP(bp);                                             //split하기위해 방금 할당한 블럭의 다음 블럭으로 bp를 이동한다.
	    PUT(HDRP(bp), PACK(csize - asize, 0));                          //뒤의 남은 공간인 csize-asize 만큼의 공간에 새로운 가용블럭을 만들어준다 (split) 
	    PUT(FTRP(bp), PACK(csize - asize, 0));                          //할당한것과 동일하게 header와 footer를 설정해준다.
	}
	else{
	    PUT(HDRP(bp), PACK(csize, 1));                                  //만약 가용공간을 split하기에 충분한 공간이 없다면 그냥 주어진 크기로 만들어만 준다. 
	    PUT(FTRP(bp), PACK(csize, 1));
	}
}

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
	if((heap_listp = mem_sbrk(4 * WSIZE)) == NULL){						//초기 블럭 16바이트 줌 
		return -1;
	}

	PUT(heap_listp, 0);													//0번째에 0
	PUT(heap_listp + WSIZE, PACK(OVERHEAD, 1));         				//prologue header 이후 할당하는 블럭들은 prologue header와 footer 사이에 위치한다. 
	PUT(heap_listp + DSIZE, PACK(OVERHEAD, 1));         				//prologue footer
	PUT(heap_listp + WSIZE + DSIZE, PACK(0, 1));        				//epilogue header
	
	heap_listp += DSIZE;												//확장할 bp의 위치 이동 
	fit_ptr = heap_listp;												//next-fit의 탐색위치를 나타낼 fit_ptr도 초기엔 heap_listp와 같은곳을 가리킨다. 

	if((extend_heap(CHUNKSIZE / WSIZE)) == NULL)						//chunk바이트 만큼의 가용블럭만큼 확장 
		return -1;

	return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
	size_t asize;
	size_t extendsize;
	char *bp;

	if(size == 0)
		return NULL;
	
	asize = ALIGN(size + OVERHEAD);										//pdf에 있는 내용 대신, macro로 정의된걸 쓴다. 할당할 데이터 사이즈에 오버헤드를 더해 열맞춤 한것과 같다. 

	if((bp = find_fit(asize)) != NULL){									//계산된 블럭 사이즈에 알맞는 위치를 찾아서 찾아지면 
		place(bp, asize);												//위치시키고 주소를 반환함. place 함수 내부에서 split을 진행한다.
		return bp;		
	}

	extendsize = MAX(asize, CHUNKSIZE);									//asize가 chunksize보다 작다면 chunksize가되고 아니면 asize가 된다. 
	
	if((bp = extend_heap(extendsize/WSIZE)) == NULL)					//extendsize 만큼의 가용블럭만큼 확장 
		return NULL;
	
	place(bp, asize);													//place 내부에서 split한다.
	fit_ptr = NEXT_BLKP(bp);											//bp에 할당했으므로 다음 위치부터 탐색하면 된다. fit_ptr을 bp의 다음 블럭 주소로 설정 
	
	return bp;															//할당한 위치 bp를 반환한다.
}

/*
 * free
 */
void free (void *ptr) {
	if(ptr == 0) return;
	size_t size = GET_SIZE(HDRP(ptr));									//ptr의 사이즈를 받아온다. 

	PUT(HDRP(ptr), PACK(size, 0));										//size크기의 할당되지 않은(0) 헤더값을 갖도록 수정한다. -> 헤더가 크기는 동일하지만 할당되지않은게 됨.
	PUT(FTRP(ptr), PACK(size, 0));										//헤더와 동일하게 푸터도 처리해준다. 
	coalesce(ptr);														//coalescing하여 free블럭들을 통합해준다.
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) {
	size_t oldsize;														
	void *newptr;

	if(size == 0){														//새로 할당할 사이즈가 0이면 free하는것과 같다. 
		free(oldptr);
		return 0;
	}

	if(oldptr == NULL){													//인자로 받은 포인터가 NULL이라면 새로 할당하는것과 같다. 
		return malloc(size);
	}

	newptr = malloc(size);												//새로운 공간에 일단 할당하고

	if(!newptr){														//유효하지않다면 종료시키고 
		return 0;
	}
	
	oldsize = GET_SIZE(oldptr);											//그렇지않다면 이전 포인터에서 사이즈를 얻어오고 

	if(size < oldsize)													//사이즈를 비교한다. 이전 사이즈가 더 컸다면 사이즈를 단축시킨다.
		oldsize = size;

	memcpy(newptr, oldptr, oldsize);									//새로 할당해놓은 공간에 복사해넣는다. 

	free(oldptr);														//인자로받은 이전 공간은 free시킨다. 

	return newptr;														//새롭게 할당된 공간의 포인터를 반환해준다. 
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) {
	size_t bytes = nmemb * size;
	void *newptr;

	newptr = malloc(bytes);
	memset(newptr, 0, bytes);

	return newptr;
}


/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p < mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int verbose) {
}
