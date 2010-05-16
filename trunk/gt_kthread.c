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

#include "gt_include.h"

#define KTHREAD_DEFAULT_SSIZE (256*1024)

/**********************************************************************/
/** DECLARATIONS **/
/**********************************************************************/

/**********************************************************************/
/* kthread context identification */
kthread_context_t *kthread_cpu_map[GT_MAX_KTHREADS];

/* kthread schedule information */
ksched_shared_info_t ksched_shared_info;
/**********************************************************************/
/* kthread */
extern int kthread_create(kthread_t *tid, void (*start_fun)(void *), void *arg);
static void kthread_handler(void *arg);
static void kthread_init(kthread_context_t *k_ctx);
static void kthread_exit();

/**********************************************************************/
/* kthread schedule */
static inline void ksched_info_init(ksched_shared_info_t *ksched_info);
static void ksched_announce_cosched_group();
static void ksched_priority(int );
static void ksched_cosched(int );
static void ksched_replenish();
extern kthread_runqueue_t *ksched_find_target(uthread_struct_t *);
extern void ksched_load_balance();

/**********************************************************************/
/* gtthread application (over kthreads and uthreads) */
static void gtthread_app_start(void *arg);

/**********************************************************************/


void ksched_load_balance()
{
     kthread_context_t *k_ctx;
     int cpu_max_num_uthread = 0; /*cpu id with maximum number of uthreads*/
     int num_uthread = 0;        /*number of uthreads for cpu with maximum number of uthreads*/
     uthread_head_t *u_head;
     uthread_struct_t *u_obj;
     kthread_runqueue_t *kthread_runq, *kthread_runq_from, *kthread_runq_to;
     runqueue_t *runq_from, *runq_to;
     int i;
     int curr = kthread_apic_id();

     int num_cpus = (int)sysconf(_SC_NPROCESSORS_CONF);

     KLOG("waiting for lock...[%s]\n",__FUNCTION__);

     gt_spin_lock(&(ksched_shared_info.ksched_load_balance_lock));

     KLOG("[%s] got lock\n",__FUNCTION__);
  
     /*find cpu with max number of uthreads*/
     for (i = 0; i < num_cpus; i++)
     {
	  k_ctx = kthread_cpu_map[i];
	  if(NULL == k_ctx)
	      continue;

	  kthread_runq = &(k_ctx->krunqueue);

	  if(i == curr || (k_ctx->kthread_flags & KTHREAD_DONE))
	       continue;
      
	  gt_spin_lock(&(kthread_runq->kthread_runqlock));
      
	  if(num_uthread < (kthread_runq->active_runq->uthread_tot + kthread_runq->expires_runq->uthread_tot))
	  {
	       cpu_max_num_uthread = i;
	       num_uthread = kthread_runq->active_runq->uthread_tot + kthread_runq->expires_runq->uthread_tot;
	  }

	  gt_spin_unlock(&(kthread_runq->kthread_runqlock));
     }
  
     k_ctx = kthread_cpu_map[cpu_max_num_uthread];
     kthread_runq_from = &(k_ctx->krunqueue);
     kthread_runq_to = &(kthread_cpu_map[curr]->krunqueue);

#if DEBUG
     uthread_struct_t *p_uthread;

     gt_spin_lock(&(kthread_runq_from->kthread_runqlock));
     TAILQ_FOREACH(p_uthread, &(kthread_runq_from->active_runq->tq_head), uthread_runq)
     {
	  KLOG("from inside activeQ uid[%d]\n", p_uthread->uthread_tid);
     }

     TAILQ_FOREACH(p_uthread, &(kthread_runq_from->expires_runq->tq_head), uthread_runq)
     {
	  KLOG("from inside expireQ uid[%d]\n", p_uthread->uthread_tid);
     }
     gt_spin_unlock(&(kthread_runq_from->kthread_runqlock));

     gt_spin_lock(&(kthread_runq_to->kthread_runqlock));
     TAILQ_FOREACH(p_uthread, &(kthread_runq_to->active_runq->tq_head), uthread_runq)
     {
	  KLOG("to inside activeQ uid[%d]\n", p_uthread->uthread_tid);
     }

     TAILQ_FOREACH(p_uthread, &(kthread_runq_to->expires_runq->tq_head), uthread_runq)
     {
	  KLOG("to inside expireQ uid[%d]\n", p_uthread->uthread_tid);
     }
     gt_spin_unlock(&(kthread_runq_to->kthread_runqlock));    
#endif

     i = 0;
  
     gt_spin_lock(&(kthread_runq_from->kthread_runqlock));
  
     while( i < (num_uthread / 2))
     {   
        
	  if(kthread_runq_from->active_runq->uthread_tot)
	  {
	       runq_from = kthread_runq_from->active_runq;
	       runq_to = kthread_runq_to->active_runq;
	  }
	  else if ( kthread_runq_from->expires_runq->uthread_tot)
	  {
	       runq_from = kthread_runq_from->expires_runq;
	       runq_to = kthread_runq_to->expires_runq;
	  }
	  else
	  {
	       printf("Error in number of uthreads\n");
	       exit(1);
	  }

	  u_head = &runq_from->tq_head;
	  u_obj = TAILQ_FIRST(u_head);
	  __rem_from_runqueue(runq_from, u_obj);

	  add_to_runqueue(runq_to, &(kthread_runq_to->kthread_runqlock), u_obj);

	  i++;
     }
  
     gt_spin_unlock(&(kthread_runq_from->kthread_runqlock));

#if DEBUG
     gt_spin_lock(&(kthread_runq_from->kthread_runqlock));
     TAILQ_FOREACH(p_uthread, &(kthread_runq_from->active_runq->tq_head), uthread_runq)
     {
	  KLOG("from inside activeQ uid[%d]\n", p_uthread->uthread_tid);
     }

     TAILQ_FOREACH(p_uthread, &(kthread_runq_from->expires_runq->tq_head), uthread_runq)
     {
	  KLOG("from inside expireQ uid[%d]\n", p_uthread->uthread_tid);
     }
     gt_spin_unlock(&(kthread_runq_from->kthread_runqlock));

     gt_spin_lock(&(kthread_runq_to->kthread_runqlock));
     TAILQ_FOREACH(p_uthread, &(kthread_runq_to->active_runq->tq_head), uthread_runq)
     {
	  KLOG("to inside activeQ uid[%d]\n", p_uthread->uthread_tid);
     }

     TAILQ_FOREACH(p_uthread, &(kthread_runq_to->expires_runq->tq_head), uthread_runq)
     {
	  KLOG("to inside expireQ uid[%d]\n", p_uthread->uthread_tid);
     }
     gt_spin_unlock(&(kthread_runq_to->kthread_runqlock));    
#endif

     gt_spin_unlock(&(ksched_shared_info.ksched_load_balance_lock));
     KLOG("[%s] un-lock\n",__FUNCTION__);

}

/* kthread creation */
int kthread_create(kthread_t *tid, void (*kthread_start_func)(void *), void *arg)
{
     int retval = 0;
     void **stack;
     int stacksize;

     stacksize = KTHREAD_DEFAULT_SSIZE;

     /* Create the new thread's stack */
     if(!(stack = (void **)MALLOC_SAFE(stacksize)))
     {
	  perror("No memory !!");
	  return -1;
     }

     /* Push the arg onto stack */	
     stack = (void **)((char *)stack + stacksize);
     *--stack = arg;

     /* Trap to clone() system call
      * %eax - __NR_clone, system call number (linux/unistd.h)
      * %ebx - clone flags
      * %ecx - new stack for thread (process)
      * Return value:
      * %eax - pid of child process for parent, 0 for child
      */
     __asm__ __volatile__ (
	  "int $0x80\n\t"
	  "testl %0,%0\n\t" /* return val ( 0=child,other=parent ) */
	  "jne 1f\n\t"
	  "call *%3\n\t"    /* call start_fun() for child */
	  "movl %2,%0\n\t"  /* exit() system call number */
	  "int $0x80\n\t"   /* exit */
	  "1:\t"
	  :"=a" (retval)
	  :"0" (__NR_clone),
	   "i" (__NR_exit),
	   "r" (kthread_start_func),
	   "b" (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | SIGCHLD),
	   "c" (stack)
	  );

     if(retval > 0)
     {
	  *tid = retval;
	  return retval;
     }
	
     return -1;
}

static void kthread_handler(void *arg)
{

#if 0
     printf("Thread to be scheduled on cpu %d\n", k_ctx->cpuid);
#endif
#define k_ctx ((kthread_context_t *)arg)
     kthread_init(k_ctx);
     OPEN_KTHREAD_LOG_FILE();
#if 0
     printf("\nThread (tid : %u, pid : %u,  cpu : %d, cpu-apic-id %d) ready to run !!\n\n", 
	    k_ctx->tid, k_ctx->pid, k_ctx->cpuid, k_ctx->cpu_apic_id);
#endif
     PRN("kthread app func called\n");
     k_ctx->kthread_app_func(NULL);
#undef k_ctx
     free(arg);
     return;
}

static void kthread_init(kthread_context_t *k_ctx)
{
     int cpu_affinity_mask, cur_cpu_apic_id;

     /* cpuid and kthread_app_func are set by the application 
      * over kthread (eg. gtthread). */

     k_ctx->pid = syscall(SYS_getpid);
     k_ctx->tid = syscall(SYS_gettid);

     /* For priority co-scheduling */
     k_ctx->kthread_sched_timer = ksched_priority;
     k_ctx->kthread_sched_relay = ksched_cosched;

     /* XXX: kthread runqueue balancing (TBD) */
     k_ctx->kthread_runqueue_balance = NULL;

     /* Initialize kthread runqueue */
     kthread_init_runqueue(&(k_ctx->krunqueue));

     cpu_affinity_mask = (1 << k_ctx->cpuid);
     sched_setaffinity(k_ctx->tid,sizeof(unsigned long),&cpu_affinity_mask);

     sched_yield();

     /* Scheduled on target cpu */
     k_ctx->cpu_apic_id = kthread_apic_id();
     kthread_cpu_map[k_ctx->cpu_apic_id] = k_ctx;

     return;
}

static inline void kthread_exit()
{
     return;
}
/**********************************************************************/
/* kthread schedule */

/* Initialize the ksched_shared_info */
static inline void ksched_info_init(ksched_shared_info_t *ksched_info)
{
     gt_spinlock_init(&(ksched_info->ksched_lock));
     gt_spinlock_init(&(ksched_info->uthread_init_lock));
     gt_spinlock_init(&(ksched_info->__malloc_lock));
     return;
}

static inline void KTHREAD_PRINT_SCHED_DEBUGINFO(kthread_context_t *k_ctx, char *str)
{
#if 0
     struct timeval tv;
     /* Thread-safe ?? */
     gettimeofday(&tv, NULL);
     printf("\n%s SIGNAL (cpu : %d, apic_id : %d) (ts : %d)\n",
	    str, k_ctx->cpuid, k_ctx->cpu_apic_id, tv.tv_usec);
#endif
     return;
}

extern kthread_runqueue_t *ksched_find_target(uthread_struct_t *u_obj)
{
     ksched_shared_info_t *ksched_info;
     unsigned int target_cpu, u_gid;

     ksched_info = &ksched_shared_info;
     //u_gid = u_obj->uthread_gid;

     //target_cpu = ksched_info->last_ugroup_kthread[u_gid];
     target_cpu = ksched_info->last_cpu;
     do
     {
	  /* How dumb to assume there is atleast one cpu (haha) !! :-D */
	  target_cpu = ((target_cpu + 1) % GT_MAX_CORES);
     } while(!kthread_cpu_map[target_cpu]);

     gt_spin_lock(&(ksched_info->ksched_lock));
     //ksched_info->last_ugroup_kthread[u_gid] = target_cpu;
     ksched_info->last_cpu = target_cpu;
     gt_spin_unlock(&(ksched_info->ksched_lock));

     u_obj->cpu_id = kthread_cpu_map[target_cpu]->cpuid;
     //u_obj->last_cpu_id = kthread_cpu_map[target_cpu]->cpuid;

     //#if 0
     PRN("Target uthread (id:%d, group:%d) : cpu(%d)\n", u_obj->uthread_tid, u_obj->uthread_gid, kthread_cpu_map[target_cpu]->cpuid);
     //#endif

     return(&(kthread_cpu_map[target_cpu]->krunqueue));
}

static void ksched_cosched(int signal)
{
     /* [1] Reads the uthread-select-criterion set by schedule-master.
      * [2] Read NULL. Jump to [5]
      * [3] Tries to find a matching uthread.
      * [4] Found - Jump to [FOUND]
      * [5] Tries to find the best uthread (by DEFAULT priority method) 
      * [6] Found - Jump to [FOUND]
      * [NOT FOUND] Return.
      * [FOUND] Return. 
      * [[NOTE]] {uthread_select_criterion == match_uthread_group_id} */

     kthread_context_t *cur_k_ctx;

     kthread_block_signal(SIGVTALRM);
     kthread_block_signal(SIGUSR1);

     /* This virtual processor (thread) was not
      * picked by kernel for vtalrm signal.
      * USR1 signal has been relayed to it. */

     cur_k_ctx = kthread_cpu_map[kthread_apic_id()];
     //KTHREAD_PRINT_SCHED_DEBUGINFO(cur_k_ctx, "RELAY(USR)");
     //printf("[%s][%s][%d][%p]\n\n",__FILE__,__FUNCTION__,__LINE__,cur_k_ctx);

     cur_k_ctx->counter = cur_k_ctx->counter + 1;

     if(REPLENISH_PERIOD == cur_k_ctx->counter)
     {
	  //printf("[%s][%s][%d][%p][%d] inside\n\n",__FILE__,__FUNCTION__,__LINE__,cur_k_ctx, cur_k_ctx->counter);
	
	  ksched_replenish();
	  cur_k_ctx->counter = 0;
     }

     uthread_schedule(&sched_find_best_uthread);

#if 0
#ifdef CO_SCHED
     uthread_schedule(&sched_find_best_uthread_group);
#else
     uthread_schedule(&sched_find_best_uthread);
#endif
#endif

     kthread_unblock_signal(SIGVTALRM);
     kthread_unblock_signal(SIGUSR1);
     return;
}

static void ksched_replenish()
{
     KLOG("%s enter\n", __FUNCTION__);
     kthread_context_t *cur_k_ctx;
     uthread_struct_t *p_uthread, *tmp_uthread;
     cur_k_ctx = kthread_cpu_map[kthread_apic_id()];

     gt_spin_lock(&(cur_k_ctx->krunqueue.kthread_runqlock));
  
     p_uthread = cur_k_ctx->krunqueue.cur_uthread;
     p_uthread->uthread_credit += ( ( (float)p_uthread->uthread_weight / cur_k_ctx->krunqueue.total_uthread_weight ) * TOTAL_CREDIT_IN_TIME_SLICE );

     TAILQ_FOREACH(p_uthread, &(cur_k_ctx->krunqueue.runqueues[0].tq_head), uthread_runq)
     {
	  KLOG("Inside activeQ u_wt[%d] t_wt[%d] ratio[%f] t_c_t_s[%d] uid[%d] u_credits=[%d]\n", \
	       p_uthread->uthread_weight,cur_k_ctx->krunqueue.total_uthread_weight,	\
	       (float) p_uthread->uthread_weight / cur_k_ctx->krunqueue.total_uthread_weight, \
	       TOTAL_CREDIT_IN_TIME_SLICE, p_uthread->uthread_tid, p_uthread->uthread_credit);
      
	  p_uthread->uthread_credit += ( ( (float)p_uthread->uthread_weight / cur_k_ctx->krunqueue.total_uthread_weight ) * TOTAL_CREDIT_IN_TIME_SLICE );

	  KLOG("u_c[%d] after repl\n", p_uthread->uthread_credit);
     }

     TAILQ_FOREACH(p_uthread, &(cur_k_ctx->krunqueue.runqueues[1].tq_head), uthread_runq)
     {
	  KLOG("Inside expireQ u_wt[%d] t_wt[%d] ratio[%f] t_c_t_s[%d] uid[%d] u_credits=[%d]\n", \
	       p_uthread->uthread_weight,cur_k_ctx->krunqueue.total_uthread_weight,	\
	       (float) p_uthread->uthread_weight / cur_k_ctx->krunqueue.total_uthread_weight, \
	       TOTAL_CREDIT_IN_TIME_SLICE, p_uthread->uthread_tid, p_uthread->uthread_credit);

	  p_uthread->uthread_credit += ( ( (float)p_uthread->uthread_weight / cur_k_ctx->krunqueue.total_uthread_weight ) *  TOTAL_CREDIT_IN_TIME_SLICE );

	  KLOG("u_c[%d] after repl\n", p_uthread->uthread_credit);
     }
  
     for (p_uthread = TAILQ_FIRST(&(cur_k_ctx->krunqueue.runqueues[1].tq_head)); p_uthread != NULL; p_uthread = tmp_uthread)
     {
	  tmp_uthread = TAILQ_NEXT(p_uthread, uthread_runq);
	  if (p_uthread->uthread_credit > 0) 
	  {
	       KLOG("swaping uid=[%d]\n", p_uthread->uthread_tid);
	  
	       /* Remove the item from the tail queue. */
	       TAILQ_REMOVE(&(cur_k_ctx->krunqueue.runqueues[1].tq_head), p_uthread, uthread_runq);
	       cur_k_ctx->krunqueue.runqueues[1].uthread_tot--;
	       TAILQ_INSERT_TAIL(&cur_k_ctx->krunqueue.runqueues[0].tq_head, p_uthread, uthread_runq);
	       cur_k_ctx->krunqueue.runqueues[0].uthread_tot++;
	  }
     }

     gt_spin_unlock(&(cur_k_ctx->krunqueue.kthread_runqlock));
  
     return;
}

static void ksched_announce_cosched_group()
{
     /* Set the current running uthread_group  */
     return;
}

static void ksched_priority(int signo)
{
     /* [1] Tries to find the next schedulable uthread.
      * [2] Not Found - Sets uthread-select-criterion to NULL. Jump to [4].
      * [3] Found -  Sets uthread-select-criterion(if any) {Eg. uthread_group}.
      * [4] Announces uthread-select-criterion to other kthreads.
      * [5] Relays the scheduling signal to other kthreads. 
      * [RETURN] */
     kthread_context_t *cur_k_ctx, *tmp_k_ctx;
     pid_t pid;
     int inx;

     kthread_block_signal(SIGVTALRM);
     kthread_block_signal(SIGUSR1);

     cur_k_ctx = kthread_cpu_map[kthread_apic_id()];

     ksched_announce_cosched_group();


     KTHREAD_PRINT_SCHED_DEBUGINFO(cur_k_ctx, "VTALRM");
     //printf("[%s][%s][%d][%p][%d][%d]\n\n",__FILE__,__FUNCTION__,__LINE__,cur_k_ctx, cur_k_ctx->counter, REPLENISH_PERIOD);
     /* Relay the signal to all other virtual processors(kthreads) */	
       
     cur_k_ctx->counter = cur_k_ctx->counter + 1;

     if(REPLENISH_PERIOD == cur_k_ctx->counter)
     {
	  //printf("[%s][%s][%d][%p][%d] inside\n\n",__FILE__,__FUNCTION__,__LINE__,cur_k_ctx, cur_k_ctx->counter);
	
	  ksched_replenish();
	  cur_k_ctx->counter = 0;
     }
	
     for(inx=0; inx<GT_MAX_KTHREADS; inx++)
     {
	  /* XXX: We can avoid the last check (tmp to cur) by
	   * temporarily marking cur as DONE. But chuck it !! */
	  if((tmp_k_ctx = kthread_cpu_map[inx]) && (tmp_k_ctx != cur_k_ctx))
	  {
	       if(tmp_k_ctx->kthread_flags & KTHREAD_DONE)
		    continue;
	       /* tkill : send signal to specific threads */
	       syscall(__NR_tkill, tmp_k_ctx->tid, SIGUSR1);
	  }
     }

     uthread_schedule(&sched_find_best_uthread);

     kthread_unblock_signal(SIGVTALRM);
     kthread_unblock_signal(SIGUSR1);
     return;
}

/**********************************************************************/

/* gtthread_app_start (kthread_app_func for gtthreads).
 * All application cleanup must be done at the end of this function. */
extern unsigned int gtthread_app_running;

static void gtthread_app_start(void *arg)
{
     PRN("Enter\n");
     kthread_context_t *k_ctx;

     k_ctx = kthread_cpu_map[kthread_apic_id()];
     assert((k_ctx->cpu_apic_id == kthread_apic_id()));

#if 0
     printf("kthread (%d) ready to schedule", k_ctx->cpuid);
#endif
     while(!(k_ctx->kthread_flags & KTHREAD_DONE))
     {
	  __asm__ __volatile__ ("pause\n");
	  if(sigsetjmp(k_ctx->kthread_env, 0))
	  {
	       /* siglongjmp to this point is done when there
		* are no more uthreads to schedule.*/
	       /* XXX: gtthread app cleanup has to be done. */
	       continue;
	  }
	  PRN("calling u sched\n");
	  uthread_schedule(&sched_find_best_uthread);
	  PRN("returned fro u sched\n");
     }
     kthread_exit();

     return;
}


extern void gtthread_app_init()
{
     PRN("enter\n");
     kthread_context_t *k_ctx, *k_ctx_main;
     kthread_t k_tid;
     unsigned int num_cpus, inx;
	

     /* Initialize shared schedule information */
     ksched_info_init(&ksched_shared_info);

     /* kthread (virtual processor) on the first logical processor */
     k_ctx_main = (kthread_context_t *)MALLOCZ_SAFE(sizeof(kthread_context_t));
     k_ctx_main->cpuid = 0;
     k_ctx_main->kthread_app_func = &gtthread_app_start;
     kthread_init(k_ctx_main);
     OPEN_KTHREAD_LOG_FILE();

     kthread_init_vtalrm_timeslice();
     kthread_install_sighandler(SIGVTALRM, k_ctx_main->kthread_sched_timer);
     kthread_install_sighandler(SIGUSR1, k_ctx_main->kthread_sched_relay);

     /* Num of logical processors (cpus/cores) */
     num_cpus = (int)sysconf(_SC_NPROCESSORS_CONF);
     PRN("Number of cores : %d\n", num_cpus);
#if 0
     fprintf(stderr, "Number of cores : %d\n", num_cores);
#endif
     /* kthreads (virtual processors) on all other logical processors */
     for(inx=1; inx<num_cpus; inx++)
     {
	  k_ctx = (kthread_context_t *)MALLOCZ_SAFE(sizeof(kthread_context_t));
	  k_ctx->cpuid = inx;
	  k_ctx->kthread_app_func = &gtthread_app_start;
	  /* kthread_init called inside kthread_handler */
	  if(kthread_create(&k_tid, kthread_handler, (void *)k_ctx) < 0)
	  {
	       fprintf(stderr, "kthread creation failed (errno:%d)\n", errno );
	       perror("clone");
	       exit(0);
	  }

	  PRN( "kthread(%d) created !!\n", inx);
     }

     {
	  /* yield till other kthreads initialize */
	  int init_done;
     yield_again:
	  sched_yield();
	  init_done = 0;
	  for(inx=0; inx<GT_MAX_KTHREADS; inx++)
	  {
	       /* XXX: We can avoid the last check (tmp to cur) by
		* temporarily marking cur as DONE. But chuck it !! */
	       if(kthread_cpu_map[inx])
		    init_done++;
	  }
	  assert(init_done <= num_cpus);
	  if(init_done < num_cpus)
	       goto yield_again;
     }

#if 0
     /* app-func is called for main in gthread_app_exit */
     k_ctx_main->kthread_app_func(NULL);
#endif
     PRN("Exit\n");
     return;
}

extern void gtthread_app_exit()
{
     /* gtthread_app_exit called by only main thread. */
     /* For main thread, trigger start again. */
     kthread_context_t *k_ctx;

     k_ctx = kthread_cpu_map[kthread_apic_id()];
     k_ctx->kthread_flags &= ~KTHREAD_DONE;

     while(!(k_ctx->kthread_flags & KTHREAD_DONE))
     {
	  __asm__ __volatile__ ("pause\n");
	  if(sigsetjmp(k_ctx->kthread_env, 0))
	  {
	       /* siglongjmp to this point is done when there
		* are no more uthreads to schedule.*/
	       /* XXX: gtthread app cleanup has to be done. */
	       continue;
	  }
	  uthread_schedule(&sched_find_best_uthread);
     }

     kthread_exit();

     while(ksched_shared_info.kthread_cur_uthreads)
     {
	  /* Main thread has to wait for other kthreads */
	  __asm__ __volatile__ ("pause\n");
     }
     return;	
}

/**********************************************************************/
/* Main Test */

#if 0
/* Main Test */
typedef struct uthread_arg
{
     int num1;
     int num2;
     int num3;
     int num4;	
} uthread_arg_t;

#define NUM_THREADS 1000
static int func(void *arg);

int main()
{
     uthread_struct_t *uthread;
     uthread_t u_tid;
     uthread_arg_t *uarg;

     int inx;

     gtthread_app_init();

     for(inx=0; inx<NUM_THREADS; inx++)
     {
	  uarg = (uthread_arg_t *)MALLOC_SAFE(sizeof(uthread_arg_t));
	  uarg->num2 = (inx % MAX_UTHREAD_GROUPS);
	  uarg->num3 = 0x55;
	  uarg->num4 = 0x77;
	  uthread_create(&u_tid, func, uarg, (inx % MAX_UTHREAD_GROUPS));
	  uarg->num1 = u_tid;
     }

     gtthread_app_exit();

     return(0);
}

static int func(void *arg)
{
     unsigned int count;
     kthread_context_t *k_ctx = kthread_cpu_map[kthread_apic_id()];
#define u_info ((uthread_arg_t *)arg)
     printf("Thread (id:%d, group:%d, cpu:%d) created\n", u_info->num1, u_info->num2, k_ctx->cpuid);
     count = 0;
     while(count <= 0x1fffffff)
     {
#if 0
	  if(!(count % 5000000))
	  {
	       printf("uthread(id:%d, group:%d, cpu:%d) => count : %d\n", 
		      u_info->num1, u_info->num2, k_ctx->cpuid, count);
	  }
#endif
	  count++;
     }
#undef u_info
     return 0;
}

#endif
