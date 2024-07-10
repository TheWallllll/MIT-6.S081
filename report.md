# Uthread: switching between threads (moderate)

In this exercise you will design the context switch mechanism for a user-level threading system, and then implement it. To get you started, your xv6 has two files user/uthread.c and user/uthread\_switch.S, and a rule in the Makefile to build a uthread program. uthread.c contains most of a user-level threading package, and code for three simple test threads. The threading package is missing some of the code to create a thread and to switch between threads.

You will need to add code to thread\_create() and thread\_schedule() in user/uthread.c, and thread\_switch in user/uthread\_switch.S. One goal is ensure that when thread\_schedule() runs a given thread for the first time, the thread executes the function passed to thread\_create(), on its own stack. Another goal is to ensure that thread\_switch saves the registers of the thread being switched away from, restores the registers of the thread being switched to, and returns to the point in the latter thread's instructions where it last left off. You will have to decide where to save/restore registers; modifying struct thread to hold registers is a good plan. You'll need to add a call to thread\_switch in thread\_schedule; you can pass whatever arguments you need to thread\_switch, but the intent is to switch from thread t to next\_thread.

**hints**

- thread\_switch needs to save/restore only the callee-save registers. Why?
- You can see the assembly code for uthread in user/uthread.asm, which may be handy for debugging.

**solution**

为用户级线程设置上下文切换机制。确保当 `thread_schedule()`第一次运行给定线程时，该线程在自己的栈上执行传递给 `thread_create()`的函数。确保 `thread_switch`保存被切换线程的寄存器，恢复切换到线程的寄存器，并返回到后一个线程指令中最后停止的点。只需要保存callee saved reg.

Add a structural：context，to save 'callee saved reg'. And add it to thread structural.

Init ra and sp register at thread_create() so that the thread executes the function passed to thread\_create() after thread_schedule() runs the thread for the first time.

Add call to thread_switch() and pass the current thread's context and the next thread's.

参考kernel的switch()实现user的switch()。

# Using threads (moderate)

In this assignment you will explore parallel programming with threads and locks using a hash table. You should do this assignment on a real Linux or MacOS computer (not xv6, not qemu) that has multiple cores. The file notxv6/ph.c contains a simple hash table that is correct if used from a single thread, but incorrect when used from multiple threads.

Why are there missing keys with 2 threads, but not with 1 thread? Identify a sequence of events with 2 threads that can lead to a key being missing. Submit your sequence with a short explanation in answers-thread.txt

To avoid this sequence of events, insert lock and unlock statements in put and get in notxv6/ph.c so that the number of keys missing is always 0 with two threads. The relevant pthread calls are:

```
pthread_mutex_t lock;            // declare a lock
pthread_mutex_init(&lock, NULL); // initialize the lock
pthread_mutex_lock(&lock);       // acquire lock
pthread_mutex_unlock(&lock);     // release lock
```

To avoid this sequence of events, insert lock and unlock statements in put and get in notxv6/ph.c so that the number of keys missing is always 0 with two threads. The relevant pthread calls are:

```
pthread_mutex_t lock;            // declare a lock
pthread_mutex_init(&lock, NULL); // initialize the lock
pthread_mutex_lock(&lock);       // acquire lock
pthread_mutex_unlock(&lock);     // release lock
```

There are situations where concurrent put()s have no overlap in the memory they read or write in the hash table, and thus don't need a lock to protect against each other. Can you change ph.c to take advantage of such situations to obtain parallel speedup for some put()s? Hint: how about a lock per hash bucket?

**solution**

定义一个全局的锁数组，即每个散列桶一个锁，在main() init，出线插入故障的原因在[answer-thread.txt](answers-thread.txt)中说明。在put插入时添加锁。

# Barrier(moderate)

In this assignment you'll implement a barrier: a point in an application at which all participating threads must wait until all other participating threads reach that point too. You'll use pthread condition variables, which are a sequence coordination technique similar to xv6's sleep and wakeup.You should do this assignment on a real computer (not xv6, not qemu).

Your goal is to achieve the desired barrier behavior. In addition to the lock primitives that you have seen in the ph assignment, you will need the following new pthread primitives; look [here](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_cond_wait.html) and [here](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_cond_broadcast.html) for details.

```
pthread_cond_wait(&cond, &mutex);  // go to sleep on cond, releasing lock mutex, acquiring upon wake up
pthread_cond_broadcast(&cond);     // wake up every thread sleeping on cond
```

There are two issues that complicate your task:

* You have to deal with a succession of barrier calls, each of which we'll call a round. bstate.round records the current round. You should increment bstate.round each time all threads have reached the barrier.
* You have to handle the case in which one thread races around the loop before the others have exited the barrier. In particular, you are re-using the bstate.nthread variable from one round to the next. Make sure that a thread that leaves the barrier and races around the loop doesn't increase bstate.nthread while a previous round is still using it.

**solution**

在barrier()中，对到达该处的线程数量+1，如果到达该处的线程数<所有进程数，调用等待信号函数睡眠，如果所有进程都到了，那么将round+1，到达该处的进程数=0，唤醒所有进程，注意这些操作应该是原子的，添加锁。
