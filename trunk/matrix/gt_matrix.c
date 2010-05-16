#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>

#include "../gt_include.h"

#define true 1
#define false 0
#define ROWS 256
#define COLS ROWS
#define SIZE COLS

#define NUM_CPUS 4
#define NUM_GROUPS NUM_CPUS
#define PER_GROUP_COLS (SIZE/NUM_GROUPS)
#define NUM_THREADS 512

#define PER_THREAD_ROWS (SIZE/NUM_THREADS)

#define GT_THREADS 1

FILE *Log;

/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

typedef struct 
{
     int m[64][64];

     int rows;
     int cols;
     unsigned int reserved[2];
} matrix_64;

typedef struct
{
     int m[128][128];

     int rows;
     int cols;
     unsigned int reserved[2];
} matrix_128;

typedef struct 
{
     int m[256][256];

     int rows;
     int cols;
     unsigned int reserved[2];
} matrix_256;

typedef struct
{
     int m[512][512];

     int rows;
     int cols;
     unsigned int reserved[2];
} matrix_512;


typedef struct __uthread_arg
{
     void *_A, *_B, *_C;
     unsigned int reserved0;

     unsigned int tid;
     unsigned int gid;
     unsigned int weight;
     unsigned int size;
     int start_row; /* start_row -> (start_row + PER_THREAD_ROWS) */
     int start_col; /* start_col -> (start_col + PER_GROUP_COLS) */
	
}uthread_arg_t;
	
//struct timeval tv1;

static void generate_matrix(void *mat, int val, int size)
{
     matrix_64 *ptr_m64;
     matrix_128 *ptr_m128;
     matrix_256 *ptr_m256;
     matrix_512 *ptr_m512;
     int i,j;
     if(64 == size)
     {
	  ptr_m64 = mat; 
	  ptr_m64->rows = size;
	  ptr_m64->cols = size;
	  for(i = 0; i < ptr_m64->rows;i++)
	       for( j = 0; j < ptr_m64->cols; j++ )
	       {
		    ptr_m64->m[i][j] = val;
	       }
     }
     else if(128 == size)
     {
	  ptr_m128 = mat; 
	  ptr_m128->rows = size;
	  ptr_m128->cols = size;
	  for(i = 0; i < ptr_m128->rows;i++)
	       for( j = 0; j < ptr_m128->cols; j++ )
	       {
		    ptr_m128->m[i][j] = val;
	       }
     }
     else if(256 == size)
     {
	  ptr_m256 = mat; 
	  ptr_m256->rows = size;
	  ptr_m256->cols = size;
	  for(i = 0; i < ptr_m256->rows;i++)
	       for( j = 0; j < ptr_m256->cols; j++ )
	       {
		    ptr_m256->m[i][j] = val;
	       }
     }
     else if(512 == size)
     {
	  ptr_m512 = mat; 
	  ptr_m512->rows = size;
	  ptr_m512->cols = size;
	  for(i = 0; i < ptr_m512->rows;i++)
	       for( j = 0; j < ptr_m512->cols; j++ )
	       {
		    ptr_m512->m[i][j] = val;
	       }
     }

     return;
}

#if 0
static void print_matrix(matrix_t *mat)
{
     int i, j;

     for(i=0;i<SIZE;i++)
     {
	  for(j=0;j<SIZE;j++)
	       printf(" %d ",mat->m[i][j]);
	  printf("\n");
     }

     return;
}
#endif

static void * uthread_mulmat(void *p)
{
     int i, j, k;
     int start_row, end_row;
     int start_col, end_col;
     unsigned int cpuid;
     struct timeval tv1, tv2, t;

     matrix_64 *ptr_m64_A, *ptr_m64_B, *ptr_m64_C;
     matrix_128 *ptr_m128_A, *ptr_m128_B, *ptr_m128_C;
     matrix_256 *ptr_m256_A, *ptr_m256_B, *ptr_m256_C;
     matrix_512 *ptr_m512_A, *ptr_m512_B, *ptr_m512_C;

     timerclear(&tv1);
     timerclear(&tv2);
     timerclear(&t);
     

#define ptr ((uthread_arg_t *)p)

     i=0; j= 0; k=0;

     start_row = 0;//ptr->start_row;
     end_row = ptr->size;//(ptr->start_row + PER_THREAD_ROWS);

#ifdef GT_GROUP_SPLIT
     start_col = 0;//ptr->start_col;
     end_col = ptr->size;//(ptr->start_col + PER_THREAD_ROWS);
#else
     start_col = 0;
     end_col = ptr->size;
#endif


     cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
    
#if 0
#ifdef GT_THREADS
     cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
     fprintf(stdout, "\n[%d]Thread(id:%d, weight:%d, cpu:%d) tv_sec[%ld] tv_usec[%ld] started",__LINE__, ptr->tid, ptr->weight, cpuid, tv1.tv_sec,tv1.tv_usec);
#else
     fprintf(stdout, "\nThread(id:%d, weight:%d) started",ptr->tid, ptr->weight);
#endif
#endif

     // printf("%d %d %d %d size[%d]\n", start_row, start_col, end_row, end_col, ptr->size);

     if(0 != gettimeofday(&tv1,NULL))
     {
	  perror("gettimeofday");
	  exit(1);
     }

     if(64 == ptr->size)
     {
	  ptr_m64_A = ptr->_A;
	  ptr_m64_B = ptr->_B;
	  ptr_m64_C = ptr->_C;
	  for(i = start_row; i < end_row; i++)
	       for(j = start_col; j < end_col; j++)
		    for(k = 0; k < SIZE; k++)
			 ptr_m64_C->m[i][j] += ptr_m64_A->m[i][k] * ptr_m64_B->m[k][j];
     }
     else if(128 == ptr->size)
     {
	  ptr_m128_A = ptr->_A;
	  ptr_m128_B = ptr->_B;
	  ptr_m128_C = ptr->_C;
	  for(i = start_row; i < end_row; i++)
	       for(j = start_col; j < end_col; j++)
		    for(k = 0; k < SIZE; k++)
			 ptr_m128_C->m[i][j] += ptr_m128_A->m[i][k] * ptr_m128_B->m[k][j];
     }
     else if(256 == ptr->size)
     {
	  ptr_m256_A = ptr->_A;
	  ptr_m256_B = ptr->_B;
	  ptr_m256_C = ptr->_C;
	  for(i = start_row; i < end_row; i++)
	       for(j = start_col; j < end_col; j++)
		    for(k = 0; k < SIZE; k++)
			 ptr_m256_C->m[i][j] += ptr_m256_A->m[i][k] * ptr_m256_B->m[k][j];
     }
     else if(512 == ptr->size)
     {
	  ptr_m512_A = ptr->_A;
	  ptr_m512_B = ptr->_B;
	  ptr_m512_C = ptr->_C;
	  for(i = start_row; i < end_row; i++)
	       for(j = start_col; j < end_col; j++)
		    for(k = 0; k < SIZE; k++)
			 ptr_m512_C->m[i][j] += ptr_m512_A->m[i][k] * ptr_m512_B->m[k][j];
     }

     if(0 != gettimeofday(&tv2,NULL))
     {
	  perror("gettimeofday");
	  exit(1);
     }

     timersub(&tv2,&tv1,&t);
#if 0
#ifdef GT_THREADS	
     fprintf(stdout, "\nThread(id:%d, weight:%d, cpu:%d size:%d) finished (TIME : %lu s and %lu us)",
	     ptr->tid, ptr->weight, cpuid, ptr->size,(t.tv_sec), (t.tv_usec));
#else
     fprintf(stdout, "\nThread(id:%d, weight:%d) finished (TIME : %lu s and %lu us)",
	     ptr->tid, ptr->weight, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#endif
#endif
     fprintf(Log, "uid[%d]  weight=[%d] size[%d]  cpuid[%d]  time[%lu s %lu us]  sched_count[%u]\n", ptr->tid, ptr->weight, ptr->size, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec), kthread_cpu_map[kthread_apic_id()]->krunqueue.cur_uthread->uthread_schedule_count); 
     fflush(Log);

#undef ptr
     return 0;
}

matrix_64 m_64[128 * 3];
matrix_128 m_128[128 * 3];
matrix_256 m_256[128 * 3];
matrix_512 m_512[128 * 3];

static void init_matrices()
{
     int i;
     for(i = 0; i < (128 * 3); i += 3)
     {
	  generate_matrix(&m_64[i], 1, 64);
	  generate_matrix(&m_64[i+1], 1, 64);
	  generate_matrix(&m_64[i+2], 0, 64);
     }

     for(i = 0; i < (128 * 3); i += 3)
     {
	  generate_matrix(&m_128[i], 1, 128);
	  generate_matrix(&m_128[i+1], 1, 128);
	  generate_matrix(&m_128[i+2], 0, 128);
     }

     for(i = 0; i < (128 * 3); i += 3)
     {
	  generate_matrix(&m_256[i], 1, 256);
	  generate_matrix(&m_256[i+1], 1, 256);
	  generate_matrix(&m_256[i+2], 0, 256);
     }

     for(i = 0; i < (128 * 3); i += 3)
     {
	  generate_matrix(&m_512[i], 1, 512);
	  generate_matrix(&m_512[i+1], 1, 512);
	  generate_matrix(&m_512[i+2], 0, 512);
     }

     return;
}

uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];


int main()
{
     uthread_arg_t *uarg;
     int inx;
     Log = fopen("log.txt","w");
     if(NULL == Log)
     {
	  fprintf(stderr, "can not open log file\n");
	  exit(1);
     }

     PRN("Enter\n");
     init_matrices();

     gtthread_app_init();

     int arr[4]={25,50,75,100};
     int cnt = 0;

     for(inx=0; inx < (NUM_THREADS); inx++)
     {
	  uarg = &uargs[inx];
	  if(inx < 128)
	  {
	       uarg->_A = &m_64[cnt];
	       uarg->_B = &m_64[cnt+1];
	       uarg->_C = &m_64[cnt+2];
	       uarg->size = 64;
	  }
	  else if(inx < 256)
	  {
	       uarg->_A = &m_128[cnt];
	       uarg->_B = &m_128[cnt+1];
	       uarg->_C = &m_128[cnt+2];
	       uarg->size = 128;
	  }  
	  else if(inx < 384)
	  {
	       uarg->_A = &m_256[cnt];
	       uarg->_B = &m_256[cnt+1];
	       uarg->_C = &m_256[cnt+2];
	       uarg->size = 256;
	  }  
	  else if(inx < 512)
	  {
	       uarg->_A = &m_512[cnt];
	       uarg->_B = &m_512[cnt+1];
	       uarg->_C = &m_512[cnt+2];
	       uarg->size = 512;
	  }
	  cnt += 3;
	  cnt = cnt % 128;

	  uarg->tid = utids[inx];

	  if ((inx % 128) < 32)
	       uarg->weight = 25;
	  else if((inx % 128) < 64)
	       uarg->weight = 50;
	  else if((inx%128) < 96)
	       uarg->weight = 75;
	  else if((inx%128) < 128)
	       uarg->weight = 100;

	  uthread_create(&(uarg->tid), uthread_mulmat, uarg, uarg->weight);
     }
     
     gtthread_app_exit();
     fclose(Log);
     printf("\n");
#if 0
     print_matrix(&A);
     fprintf(stderr, "********************************");
     print_matrix(&B);
     fprintf(stderr, "********************************");
     print_matrix(&C);
     fprintf(stderr, "********************************");
#endif
     return(0);
}
