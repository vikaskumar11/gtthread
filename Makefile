matrix.exe : gt_matrix.o gt_kthread.o gt_pq.o gt_signal.o gt_spinlock.o gt_uthread.o
	gcc gt_matrix.o gt_kthread.o gt_pq.o gt_signal.o gt_spinlock.o gt_uthread.o -o matrix.exe

gt_matrix.o : matrix/gt_matrix.c 
	gcc -g -c  matrix/gt_matrix.c

gt_pq.o : gt_pq.c 
	gcc -g -c gt_pq.c

gt_signal.o : gt_signal.c 
	gcc -g -c gt_signal.c

gt_spinlock.o : gt_spinlock.c 
	gcc -g -c gt_spinlock.c

gt_uthread.o : gt_uthread.c 
	gcc -g -c gt_uthread.c

gt_kthread.o : gt_kthread.c 
	gcc -g -c gt_kthread.c

clean:
	rm -rf *.o
