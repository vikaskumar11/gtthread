#ifndef __GT_INCLUDE_H
#define __GT_INCLUDE_H

extern FILE* Log;

#define DEBUG 0

#if DEBUG
#define PRN(X ...) fprintf(Log, "[%s][%s][%d]\t",__FILE__,__FUNCTION__,__LINE__); fprintf(Log, X); fflush(Log);
#else
#define PRN(X ...) //fprintf(Log, "[%s][%s][%d]\t",__FILE__,__FUNCTION__,__LINE__); fprintf(Log, X); fflush(Log);
#endif

#if DEBUG
#define OPEN_KTHREAD_LOG_FILE()						\
  do                                                                    \
    {                                                                   \
      char thread_log_filename[50];                                     \
      FILE* thread_log;                                                 \
      sprintf (thread_log_filename, "%d.log", kthread_cpu_map[kthread_apic_id()]->cpuid);	\
      thread_log = fopen (thread_log_filename, "w");                    \
      if(NULL == thread_log)						\
	{								\
	  perror("fopen");						\
	  exit(1);							\
	}								\
      kthread_cpu_map[kthread_apic_id()]->log = thread_log;		\
    }while(0)

#else
#define OPEN_KTHREAD_LOG_FILE()	///
#endif

#if DEBUG
#define KLOG(X ...)							\
  do                                                                    \
    {                                                                   \
      fprintf(kthread_cpu_map[kthread_apic_id()]->log, "[%s %s %d] ",__FILE__,__FUNCTION__, __LINE__); \
      fprintf(kthread_cpu_map[kthread_apic_id()]->log, X);		\
      fflush(Log);                                                      \
    }while(0)
#else
#define KLOG(X ...) ///
#endif

#include <stdlib.h>
#include "gt_signal.h"
#include "gt_spinlock.h"
#include "gt_tailq.h"
#include "gt_bitops.h"

#include "gt_uthread.h"
#include "gt_pq.h"
#include "gt_kthread.h"

#endif
