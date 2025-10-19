/*
 * Exercise on thread synchronization.
 *
 * Assume a half-duplex communication bus with limited capacity, measured in
 * tasks, and 2 priority levels:
 *
 * - tasks: A task signifies a unit of data communication over the bus
 *
 * - half-duplex: All tasks using the bus should have the same direction
 *
 * - limited capacity: There can be only 3 tasks using the bus at the same time.
 *                     In other words, the bus has only 3 slots.
 *
 *  - 2 priority levels: Priority tasks take precedence over non-priority tasks
 *
 *  Fill-in your code after the TODO comments
 */

#include <stdio.h>
#include <string.h>

#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "timer.h"

/* This is where the API for the condition variables is defined */
#include "threads/synch.h"

/* This is the API for random number generation.
 * Random numbers are used to simulate a task's transfer duration
 */
#include "lib/random.h"

#define MAX_NUM_OF_TASKS 200

#define BUS_CAPACITY 3

typedef enum {
  SEND,
  RECEIVE,

  NUM_OF_DIRECTIONS
} direction_t;

typedef enum {
  NORMAL,
  PRIORITY,

  NUM_OF_PRIORITIES
} priority_t;

typedef struct {
  direction_t direction;
  priority_t priority;
  unsigned long transfer_duration;
} task_t;

void init_bus (void);
void batch_scheduler (unsigned int num_priority_send,
                      unsigned int num_priority_receive,
                      unsigned int num_tasks_send,
                      unsigned int num_tasks_receive);

/* Thread function for running a task: Gets a slot, transfers data and finally
 * releases slot */
static void run_task (void *task_);

/* WARNING: This function may suspend the calling thread, depending on slot
 * availability */
static void get_slot (const task_t *task);

/* Simulates transfering of data */
static void transfer_data (const task_t *task);

/* Releases the slot */
static void release_slot (const task_t *task);

int counter;
direction_t currentDir;
struct condition waitingToGo[2];
struct condition PrioWaitingToGo[2];
int waiters[2];
int prioWaiters[2];
struct lock busLock;

void init_bus (void) {

  random_init ((unsigned int)123456789);

  /*counter to count how many are on the bus.
    current direction of the waiters.
    curretnt priority of the waiters.
    a lock for the bus, so one thread accesses the shared states.
    */
  counter = 0;
  currentDir = SEND;
  cond_init(&waitingToGo[0]);
  cond_init(&waitingToGo[1]);
  cond_init(&PrioWaitingToGo[0]);
  cond_init(&PrioWaitingToGo[1]);
  waiters[0] = 0;
  waiters[1] = 0;
  prioWaiters[0] = 0;
  prioWaiters[1] = 0;
  lock_init(&busLock);

}

void batch_scheduler (unsigned int num_priority_send,
                      unsigned int num_priority_receive,
                      unsigned int num_tasks_send,
                      unsigned int num_tasks_receive) {
  ASSERT (num_tasks_send + num_tasks_receive + num_priority_send +
             num_priority_receive <= MAX_NUM_OF_TASKS);

  static task_t tasks[MAX_NUM_OF_TASKS] = {0};

  char thread_name[32] = {0};

  unsigned long total_transfer_dur = 0;

  int j = 0;

  /* create priority sender threads */
  for (unsigned i = 0; i < num_priority_send; i++) {
    tasks[j].direction = SEND;
    tasks[j].priority = PRIORITY;
    tasks[j].transfer_duration = random_ulong() % 244;

    total_transfer_dur += tasks[j].transfer_duration;

    snprintf (thread_name, sizeof thread_name, "sender-prio");
    thread_create (thread_name, PRI_DEFAULT, run_task, (void *)&tasks[j]);

    j++;
  }

  /* create priority receiver threads */
  for (unsigned i = 0; i < num_priority_receive; i++) {
    tasks[j].direction = RECEIVE;
    tasks[j].priority = PRIORITY;
    tasks[j].transfer_duration = random_ulong() % 244;

    total_transfer_dur += tasks[j].transfer_duration;

    snprintf (thread_name, sizeof thread_name, "receiver-prio");
    thread_create (thread_name, PRI_DEFAULT, run_task, (void *)&tasks[j]);

    j++;
  }

  /* create normal sender threads */
  for (unsigned i = 0; i < num_tasks_send; i++) {
    tasks[j].direction = SEND;
    tasks[j].priority = NORMAL;
    tasks[j].transfer_duration = random_ulong () % 244;

    total_transfer_dur += tasks[j].transfer_duration;

    snprintf (thread_name, sizeof thread_name, "sender");
    thread_create (thread_name, PRI_DEFAULT, run_task, (void *)&tasks[j]);

    j++;
  }

  /* create normal receiver threads */
  for (unsigned i = 0; i < num_tasks_receive; i++) {
    tasks[j].direction = RECEIVE;
    tasks[j].priority = NORMAL;
    tasks[j].transfer_duration = random_ulong() % 244;

    total_transfer_dur += tasks[j].transfer_duration;

    snprintf (thread_name, sizeof thread_name, "receiver");
    thread_create (thread_name, PRI_DEFAULT, run_task, (void *)&tasks[j]);

    j++;
  }

  /* Sleep until all tasks are complete */
  timer_sleep (2 * total_transfer_dur);
}

/* Thread function for the communication tasks */
void run_task(void *task_) {
  task_t *task = (task_t *)task_;

  get_slot (task);

  msg ("%s acquired slot", thread_name());
  transfer_data (task);

  release_slot (task);
}

static direction_t other_direction(direction_t this_direction) {
  return this_direction == SEND ? RECEIVE : SEND;
}

void get_slot (const task_t *task) {


   /*Lock before.... If the bus is full, then no one can enter. 
                     If there are tasks on the bus and the current direction is not equal to the tasks direction, then it can not enter either.  
                     If the task has normal priority and there are tasks waiting with higher priority, then that normal task can not enter either.
     */
   lock_acquire(&busLock);
   while (
    counter == BUS_CAPACITY || // Always wait if the queue is full
    (counter > 0 && currentDir != task->direction) || // Always wait if the queue is going another direction than what you want to go
    ( task->priority != PRIORITY  && (prioWaiters[SEND] != 0 || prioWaiters[RECEIVE] != 0))) // Always wait if you're a normal task and there exists any kind of priority task
  {
      if(task->priority == NORMAL){ // Normal Task
        waiters[task->direction]++;
        cond_wait(&waitingToGo[task->direction], &busLock);
        waiters[task->direction]--;
      } else { // Priority Task
        prioWaiters[task->direction]++;
        cond_wait(&PrioWaitingToGo[task->direction], &busLock);
        prioWaiters[task->direction]--;
      }
   }
   counter++;
   currentDir = task->direction;
   lock_release(&busLock);


}

void transfer_data (const task_t *task) {
  /* Simulate bus send/receive */
  timer_sleep (task->transfer_duration);
}

void release_slot (const task_t *task) {

   /*Lock before... decrement the counter (releasing a slot)
                    if there are any tasks that want to go same direction, wake them up
                    otherwise, if there are no one on the bus, and there is someone who wants to go the other direction, wake them up*/

  lock_acquire(&busLock);
  counter--;
  
  // Technically we shouldn't need if statements here and we could just broadcast notify all and get_slot() make them wait
  // However we can prevent that by also trying to wake up the correct threads here.
  // If no one is on the bus
  if(counter == 0){
    // are any priority tasks waiting?
    if(prioWaiters[RECEIVE] > 0 || prioWaiters[SEND] > 0){
      // Tell the priority tasks that they can go,
      cond_broadcast(&PrioWaitingToGo[SEND], &busLock); 
      cond_broadcast(&PrioWaitingToGo[RECEIVE], &busLock);
    } else { // else Let normal tasks go
      cond_broadcast(&waitingToGo[SEND], &busLock);
      cond_broadcast(&waitingToGo[RECEIVE], &busLock);
    }

  } else { // If there are tasks on the bus

    if(prioWaiters[currentDir] > 0) // Prioritize first the priority tasks that are in the same direction as us and are priority
    {
      cond_signal(&PrioWaitingToGo[currentDir], &busLock);
    } else if(prioWaiters[other_direction(currentDir)] > 0) // Prioritize second any Priority tasks that want to go the opposite direction
    {
      cond_signal(&PrioWaitingToGo[other_direction(currentDir)], &busLock);
    } else if(waiters[currentDir] > 0) // Prioritize third any task that want to go the same direction, priority ones are already handled in first if statement
    { 
      cond_signal(&waitingToGo[currentDir], &busLock);
    } else if((waiters[other_direction(currentDir)] > 0)){ // let other side go
      cond_signal(&waitingToGo[other_direction(currentDir)], &busLock);
    }

  }

  lock_release(&busLock);
}
