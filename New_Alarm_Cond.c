/*
 * alarm_cond.c
 *
 * This is an enhancement to the alarm_mutex.c program, which
 * used only a mutex to synchronize access to the shared alarm
 * list. This version adds a condition variable. The alarm
 * thread waits on this condition variable, with a timeout that
 * corresponds to the earliest timer request. If the main thread
 * enters an earlier timeout, it signals the condition variable
 * so that the alarm thread will wake up and process the earlier
 * timeout first, requeueing the later request.
 */
#include <pthread.h>
#include <time.h>
#include "errors.h"
#include <semaphore.h>


#define UNCHANGED 0
#define CHANGE 1
#define CHANGE2 2
/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
typedef struct alarm_tag
{
    struct alarm_tag *link;
    int seconds;
    int alarm_id;
    int group_id;
    int changed;
    time_t time; /* seconds from EPOCH */
    char message[64];
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
alarm_t *curr_alarm_t;
time_t current_alarm = 0;

sem_t main_sem;
sem_t display_sem;



/*
 * Insert alarm entry on list, in order.
 */
void alarm_insert(alarm_t *alarm)
{
    int status;
    alarm_t **last, *next;

    /*
     * LOCKING PROTOCOL:
     *
     * This routine requires that the caller have locked the
     * alarm_mutex!
     */
    last = &alarm_list;
    next = *last;
    while (next != NULL)
    {
        if (next->alarm_id >= alarm->alarm_id)
        {
            alarm->link = next;
            *last = alarm;
            curr_alarm_t = next;
            break;
        }
        last = &next->link;
        next = next->link;
    }
    /*
     * If we reached the end of the list, insert the new alarm
     * there.  ("next" is NULL, and "last" points to the link
     * field of the last item, or to the list header.)
     */
    if (next == NULL)
    {
        *last = alarm;
        alarm->link = NULL;
    }
#ifdef DEBUG
    printf("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf("%d(%d)[\"%s\"] ", next->time,
               next->time - time(NULL), next->message);
    printf("]\n");
#endif
    /*
     * Wake the alarm thread if it is not busy (that is, if
     * current_alarm is 0, signifying that it's waiting for
     * work), or if the new alarm comes before the one on
     * which the alarm thread is waiting.
     */
    if (current_alarm == 0 || alarm->time < current_alarm)
    {
        current_alarm = alarm->time;
        status = pthread_cond_signal(&alarm_cond);
        if (status != 0)
            err_abort(status, "Signal cond");
    }
}


void getSmallestAlarmTime(){
    alarm_t *smallestAlarm, *next, *current_alarm;
    alarm_t *last;
    int beenRemoved;

    smallestAlarm = alarm_list;
    next = alarm_list;
    last = NULL;

    while (next->link != NULL)
    {
        if (next->time < smallestAlarm->time){
            smallestAlarm = next;
        }
        next = next->link;
    }

    if (next->time < smallestAlarm->time){
        smallestAlarm = next;
    }

    current_alarm = smallestAlarm;

    next = alarm_list;
    beenRemoved=0;

    while(next!=NULL && beenRemoved!=1){
        if(next->time == smallestAlarm->time){
            if(last == NULL){
                alarm_list = next->link;
            }
            else{
                last->link = next->link;
            }
        beenRemoved = 1;
        }
        if(next->link!=NULL){
            last=next;
            next=next->link;
        }
    }
}




/*
  A.3.2.2
  Change alarm specified by alarm_id
  group_id to new specified group_id
  time to new specified time
  message to new specified message
*/
void change_alarm(alarm_t *new)
{
    alarm_t **last, *next;

    int status;

    last = &alarm_list;
    next = *last;

    status=sem_wait(&main_sem);
    if (status != 0)
    {
        err_abort(status,"Lock mutex");
    }

    while (next != NULL)
    {
        if (next->alarm_id == new->alarm_id)
        {
            if (next->group_id == new->group_id){
                strcpy(next->message, new->message);
                next->changed = CHANGE;
                curr_alarm_t = next;
                next->time = time(NULL) + new->time;
                new->link = next;
            }
            else{
                next->changed = CHANGE2;
                next->group_id = new->group_id;
                next->time = time(NULL) + new->time;
                strcpy(next->message, new->message);
                new->link = next;
            }
            break;
        }
        last = &next->link;
        next = next->link;
    }

#ifdef DEBUG
       printf ("[list: ");
       for (next = alarm_list; next != NULL; next = next->link)
           printf ("(%d)[\"%s\"] ", next->change, next->message);
        printf ("]\n");
#endif
    if (curr_alarm_t->alarm_id == new->alarm_id) {
       if(curr_alarm_t->group_id != new->group_id) {
          fprintf(stdout,"Display Thread <thread-id> Has Stopped Printing Message of Alarm(%d at %ld: Changed Group(%d) %s\n",
          new->alarm_id, curr_alarm_t->time, new->group_id, new->message);
        }
      else {
          fprintf(stdout,"Alarm(%d) Changed at %ld: Group(%d) %s\n",
                  new->alarm_id, time (NULL), new->group_id, new->message);
        }
        alarm_insert(new);
        getSmallestAlarmTime();
      }
    else{
        alarm_insert(new);
    }
    
    status=sem_post(&main_sem);
    if (status != 0)
    {
        err_abort(status,"Unlock mutex");
    }
}


 void *alarm_thread (void *arg)
  {
      alarm_t *alarm, *next;
      struct timespec cond_time;
      time_t now;
      int status, expired;

      /*
       * Loop forever, processing commands. The alarm thread will
       * be disintegrated when the process exits. Lock the mutex
       * at the start -- it will be unlocked during condition
       * waits, so the main thread can insert alarms.
       */
      status = pthread_mutex_lock (&alarm_mutex);
      if (status != 0)
          err_abort (status, "Lock mutex");
      while (1) {
          /*
           * If the alarm list is empty, wait until an alarm is
           * added. Setting current_alarm to 0 informs the insert
           * routine that the thread is not busy.
           */
          current_alarm = 0;
          while (alarm_list == NULL) {
              status = pthread_cond_wait (&alarm_cond, &alarm_mutex);
              if (status != 0)
                  err_abort (status, "Wait on cond");
          }

          curr_alarm_t = alarm_list;

          getSmallestAlarmTime();
  #ifdef DEBUG
          next = alarm_list;
          printf ("[list: ");
          for (next = alarm_list; next != NULL; next = next->link)
              printf ("(%d)[\"%s\"] ", next->change,
              next->message);
          printf ("]\n");
  #endif
        now = time (NULL);
          expired = 0;

if (curr_alarm_t->time > now) {
  #ifdef DEBUG
              printf ("[waiting: %d(%d)\"%s\"]\n", curr_alarm->time,
                  curr_alarm->time - time (NULL), curr_alarm->message);
  #endif

   cond_time.tv_sec = curr_alarm_t->time;
              cond_time.tv_nsec = 0;
              current_alarm = curr_alarm_t->time;
              while (current_alarm == curr_alarm_t->time) {
                  status = pthread_cond_timedwait (
                      &alarm_cond, &alarm_mutex, &cond_time);
                  if (status == ETIMEDOUT) {
                      expired = 1;
                      break;
                  }
                  if (status != 0)
                      err_abort (status, "Cond timedwait");
              }
              if (!expired)
                  alarm_insert(curr_alarm_t);
                  } else
              expired = 1;
          if (expired) {
              printf ("(%d) %s\n", curr_alarm_t->seconds, curr_alarm_t->message);
              free (curr_alarm_t);
          }
      }
  }
/*
 * The alarm thread's start routine.
 */
// void *alarm_thread(void *arg)
// {
//     struct timespec cond_time;
//     time_t now;
//     int status, expired;
//     pthread_t pthread;

//     /*
//      * Loop forever, processing commands. The alarm thread will
//      * be disintegrated when the process exits. Lock the mutex
//      * at the start -- it will be unlocked during condition
//      * waits, so the main thread can insert alarms.
//      */
//     status = sem_wait(&main_sem);
//     if (status != 0)
//         err_abort(status, "Lock mutex");


//     while (1)
//     {
//         /*
//          * If the alarm list is empty, wait until an alarm is
//          * added. Setting current_alarm to 0 informs the insert
//          * routine that the thread is not busy.
//          */
//         current_alarm = 0;
//         while (alarm_list == NULL)
//         {
//             status = pthread_cond_wait(&alarm_cond, &alarm_mutex);
//             if (status != 0)
//                 err_abort(status, "Wait on cond");
//         }
//         curr_alarm_t = alarm_list;

//         getSmallestAlarmTime();

//         expired = 0;
//         if (curr_alarm_t->time > now)
//         {
// #ifdef DEBUG
//             printf("[waiting: %d(%d)\"%s\"]\n", curr_alarm_t->time,
//                    curr_alarm_t->time - time(NULL), curr_alarm_t->message);
// #endif
//             cond_time.tv_sec = curr_alarm_t->time;
//             cond_time.tv_nsec = 0;
//             current_alarm = curr_alarm_t->time;
//             while (current_alarm == curr_alarm_t->time)
//             {
//                 status = pthread_cond_timedwait(
//                     &alarm_cond, &alarm_mutex, &cond_time);
//                 if (status == ETIMEDOUT)
//                 {
//                     expired = 1;
//                     break;
//                 }
//                 if (status != 0)
//                     err_abort(status, "Cond timedwait");
//             }
//             if (!expired)
//                 alarm_insert(curr_alarm_t);
//         }
//         else
//             expired = 1;
//         if (expired)
//         {
//             fprintf(stdout,"(%d) %s\n", curr_alarm_t->seconds, curr_alarm_t->message);
//             free(curr_alarm_t);
//         }
//     }
// }

int main(int argc, char *argv[])
{
    int status;
    char line[128];
    alarm_t *alarm;
    pthread_t thread;


    if (sem_init(&main_sem,0,1)<0){
        printf("Error creating sempahore");
        exit(1);
    }
        if (sem_init(&display_sem,0,1)<0){
        printf("Error creating sempahore");
        exit(1);
    }

    status = pthread_create(
        &thread, NULL, alarm_thread, NULL);
    if (status != 0)
        err_abort(status, "Create alarm thread");

    while (1)
    {
        printf("Alarm> ");
        if (fgets(line, sizeof(line), stdin) == NULL)
            exit(0);
        if (strlen(line) <= 1)
            continue;
        alarm = (alarm_t *)malloc(sizeof(alarm_t));
        if (alarm == NULL)
            errno_abort("Allocate alarm");

        if ((sscanf(line, "start(%d): group(%d) %d %128[^\n]", &alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message) < 4)
        && (sscanf(line, "change(%d): group(%d) %d %128[^\n]", &alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message) < 4))
        {
            fprintf(stderr, "Bad command\n");
            free(alarm);
        }
        else if (!(sscanf(line, "start(%d): group(%d) %d %128[^\n]", &alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message) < 4))
        //else if (!(sscanf(line, "Start_Alarm(%d): Group(%d) %d %128[^\n]",&alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message)<4))
        {
            status = sem_wait(&main_sem);
            if (status!=0)
                err_abort(status,"Lock mutex");

            alarm->changed = UNCHANGED;

            /*
               * Insert the new alarm into the list of alarms,
               * sorted by alarm id.
               */
            //A3.2.1
            //Prints out the required message and a new line is prompted
            alarm_insert(alarm);
            fprintf(stdout, "Alarm(%d) Inserted by Main Thread %d Into Alarm List at %d: Group(%d) %d %s\n", alarm->alarm_id, pthread_self(), alarm->seconds, alarm->group_id, alarm->time, alarm->message);
            //

            status = sem_post(&main_sem);
            if (status!=0)
                err_abort(status,"Unlock mutex");
        }
        else if (!(sscanf(line, "change(%d): group(%d) %d %128[^\n]", &alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message) < 4))
        //else if(!(sscanf(line, "Change_Alarm(%d): Group(%d) %d %128[^\n]",&alarm->alarm_id, &alarm->group_id, alarm->seconds, alarm->message)<4))
        {
            //Change alarm
            status = sem_wait(&main_sem);
            if (status!=0)
                err_abort(status,"Lock mutex");

            alarm->time = time(NULL) + alarm->seconds;

            //Change alarm settings to new alarm
            change_alarm(alarm);

            status = sem_post(&main_sem);
            if (status!=0)
                err_abort(status,"Unlock mutex");
        }
    }
}
