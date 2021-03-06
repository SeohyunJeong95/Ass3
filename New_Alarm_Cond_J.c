#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <stdio.h>
#include "errors.h"

/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */

typedef struct alarm_tag {
    struct alarm_tag    *link;
	  int 				        alarm_id;
	  int					        group_id;
    int                 seconds;
    time_t              time;   /* seconds from EPOCH */
    char                message[128];
    int                 changed;
} alarm_t;


/*
  append_list keeps all the requested alarm from users and append it to
  the new or existing threads.
*/
typedef struct append_list{
    alarm_t *           alarm;
    struct append_list* next;
    struct append_list* last;
} append_list;


/*
  display thread only needs to hold the informations regarding the
  alarm requested.
*/
typedef struct display_tag {
  struct display_tag * link;
  pthread_t            thread_id;
  alarm_t *            alarm;
} display_t;


 /*
  Linked List structure holding reference to all display thread together
  for easier search
 */
typedef struct display_list {
  struct display_list*    next;
  struct display_list*    previous;
  display_t *             item;
} display_list;


  pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
  sem_t rw_mutex;
  sem_t mutex;

  alarm_t * alarm_list = NULL;
  alarm_t * curr_alarm = NULL;
  display_t * thread_list = NULL;
  int alarm_thread_created = 0;

  time_t current_alarm = 0;

  int read_count = 0;
  int append_flag = 0;
  int change_flag = 0;


void insert_alarm (alarm_t *alarm) {
    int status;
    alarm_t **last, *next;

     sem_wait(&rw_mutex);

    last = &alarm_list;
    next = *last;

    while (next != NULL) {
        if (next->alarm_id >= alarm->alarm_id) {
            alarm->link = next;
            *last = alarm;
            break;
        }
        last = &next->link;
        next = next->link;
    }
    if (next == NULL) {
        *last = alarm;
        alarm->link = NULL;
    }

#ifdef DEBUG
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("(%d)[\"%s\"] ", next->change, next->message);
    printf ("]\n");
#endif
    /*
     * Wake the alarm thread if it is not busy (that is, if
     * current_alarm is 0, signifying that it's waiting for
     * work), or if the new alarm comes before the one on
     * which the alarm thread is waiting.
     */
    if (current_alarm == 0 || alarm->time < current_alarm) {
        current_alarm = alarm->time;
        status = pthread_cond_signal (&alarm_cond);
        if (status != 0)
            err_abort (status, "Signal cond");
    }

    sem_post(&rw_mutex);
}


void change_alarm(alarm_t *alarm) {
   int status;
   alarm_t **last, *next;

    sem_wait(&rw_mutex);

   last = &alarm_list;
   next = *last;

   while(next!=NULL) {
     if (next->alarm_id == alarm->alarm_id) {
       if(next->group_id ==  alarm-> group_id){
         alarm->changed = 1;
         next->time = time(NULL) + alarm->time;
         strcpy(next->message, alarm->message);
         alarm->link = next;
       }
       alarm->changed = 2;
       next->group_id = alarm->group_id;
       next->time = time(NULL) + alarm->time;
       strcpy(next->message, alarm->message);
       alarm->link = next;
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
      if (curr_alarm->alarm_id == alarm->alarm_id) {
      if(curr_alarm->group_id != alarm->group_id) {
          printf("Display Thread <thread-id> Has Stopped Printing Message of Alarm(%d at %ld: Changed Group(%d) %s\n",
          alarm->alarm_id, curr_alarm->time, alarm->group_id, alarm->message);
        }
      else {
          printf("Alarm(%d) Changed at %ld: Group(%d) %s\n",
                  alarm->alarm_id, time (NULL), alarm->group_id, alarm->message);
        }
      }

    sem_post(&rw_mutex);

}

 // void * display_thread(void * arg) {
 //      int status;
 //      alarm_t * alarm = (alarm_t *) arg;
 //      alarm_t * next;
 //      int alarm_replaced = 0;
 //
 //      sem_wait(&mutex);
 //      read_count++;
 //      if(read_count == 1)
 //          sem_wait(&rw_mutex);
 //      sem_post(&mutex);
 //
 //      while(1) {
 //
 //        sem_wait(&mutex);
 //        read_count++;
 //        if(read_count == 1)
 //            sem_wait(&rw_mutex);
 //        sem_post(&mutex);
 //
 //        //If alarm was removed, exit.
 //        if(alarm->removed == 1){
 //               printf("Display Thread <%d> Has Stopped Printing Message of Alarm<%d> at %d:Group<%d> %s\n",
 //                      pthread_self(), alarm_id, (int) now, group_id, msg);
 //               fflush(stdout);
 //               funlockfile(stdout);
 //
 //               //Exit safely and not abruptly
 //               //If this is the last thread to exit, make sure it unlocks the semaphore as well
 //               sem_wait(&display_sem);
 //               reader_flag--;
 //               if(reader_flag ==0)
 //                   sem_post(&main_semaphore);
 //               sem_post(&display_sem);
 //               //Free the thread_alarm struct used in this thread
 //               free(alarm);
 //               pthread_exit(NULL);
 //
 //           }
 //
 //           //If the alarm has been altered, display the message and then set the altered flag.
 //           if(alarm->alarm->changed == 1){
 //               printf("Alarm With Message Number (%d) Replaced at %d: %d Message(%d) %s\n",
 //                      alarm->alarm->alarm_number, (int) now, alarm->alarm->seconds,alarm->alarm->alarm_number, alarm->alarm->message);
 //               interval = alarm->alarm->seconds;
 //               display_interval = now + interval;
 //               has_changed = 1;
 //               alarm->alarm->changed = 0;
 //               strcpy(msg, alarm->alarm->message);
 //           } else if(now >= display_interval){
 //               //Check if the interval has been satisfied and
 //               if(has_changed == 1){
 //                   printf("Replacement Alarm With Message Number (%d) Displayed at %d: %d Message(%d) %s\n",
 //                          alarm->alarm->alarm_number, (int) now, alarm->alarm->seconds,alarm->alarm->alarm_number, alarm->alarm->message);
 //
 //               } else {
 //                   printf("Alarm With Message Number (%d) Displayed at %d: %d Message(%d) %s\n",
 //                          alarm->alarm->alarm_number, (int) now, alarm->alarm->seconds,alarm->alarm->alarm_number, alarm->alarm->message);
 //               }
 //               display_interval = now+ alarm->alarm->seconds;
 //           }
 //
 //           //Readers-first synchro
 //           sem_wait(&display_sem);
 //           reader_flag--;
 //           if(reader_flag ==0)
 //               sem_post(&main_semaphore);
 //           sem_post(&display_sem);
 //       }
 //
 //   }
 //
 //   /*
 //    * Creates the list of display threads based on the last appended to the list.
 //    * It functions as a queue, creating threads in the order which they were added to the list.
 //    * We are assuming the `last` element is will not be passed as null, otherwise this could result in a
 //    * segfault.
 //    *
 //    */
 //   display_thread_list * create_display_threads(display_thread_list * last){
 //       if(last == NULL)
 //           errno_abort("Last should not be null!");
 //
 //       //Reference to old element
 //       append_list * old;
 //       //New list node for the display thread list
 //       display_thread_list * new_list_node;
 //       //New thread_alarm
 //       thread_alarm * new_thread_alarm;
 //       //create a new thread. We don't care about the reference so we can discard it after the loop
 //       pthread_t new_thread;
 //       int counter = 0;
 //       while(list_to_append != NULL){
 //
 //
 //           old = list_to_append;
 //           new_list_node = (display_thread_list *)malloc(sizeof(display_thread_list));
 //           if (new_list_node == NULL) {
 //               printf("Out of memory!\n");
 //               exit(1);
 //           }
 //           new_list_node->previous = last;
 //           new_list_node->next = NULL;
 //           //Initialize the new thread's data
 //           new_thread_alarm = (thread_alarm *) malloc(sizeof(thread_alarm));
 //           if (new_thread_alarm == NULL) {
 //               printf("Out of memory!\n");
 //               exit(1);
 //           }
 //           new_thread_alarm->alarm = old->alarm;
 //           new_thread_alarm->removed = 0;
 //           //Add the new thread data to a list struct
 //           last->data = new_thread_alarm;
 //           //Append the
 //           last->next = new_list_node;
 //           last = last->next;
 //
 //           list_to_append = list_to_append->next;
 //           pthread_create(&new_thread, NULL, display_thread, (void *) new_thread_alarm);
 //           free(old);
 //           counter++;
 //       }
 //
 //       //Return the reference to the last element
 //       return last;
 //
 // }

 /*
  * The alarm thread's start routine.
  */
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

          curr_alarm = alarm_list;

          //findSmallest();
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

          if (curr_alarm->time > now) {
  #ifdef DEBUG
              printf ("[waiting: %d(%d)\"%s\"]\n", curr_alarm->time,
                  curr_alarm->time - time (NULL), curr_alarm->message);
  #endif
              cond_time.tv_sec = curr_alarm->time;
              cond_time.tv_nsec = 0;
              current_alarm = curr_alarm->time;
              while (current_alarm == curr_alarm->time) {
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
                  insert_alarm(curr_alarm);
          } else
              expired = 1;
          if (expired) {
              printf ("(%d) %s\n", curr_alarm->seconds, curr_alarm->message);
              free (curr_alarm);
          }
      }
  }

int main (int argc, char *argv[])
{
    int status;
    char line[128];
    char keyword[30]; // used to indicate the keyword in the request.

    alarm_t *alarm;
    pthread_t thread;
    time_t now;
    append_list * to_append;

    status = pthread_create (&thread, NULL, alarm_thread, NULL);
    if (status != 0)
        err_abort (status, "Create alarm thread");

    while (1) {
        printf ("Alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;
        alarm = (alarm_t*)malloc (sizeof (alarm_t));
        if (alarm == NULL)
            errno_abort ("Allocate alarm");

    //parse input into two alarm request. "Start_Alarm" and "Change_Alarm"
      if ((sscanf(line, "start(%d): group(%d) %d %128[^\n]",&alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message)<4)
          && (sscanf(line, "change(%d): group(%d) %d %128[^\n]",&alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message)<4))
          {
            fprintf (stderr, "Bad command\n");
            free (alarm);
          }

      else if (!(sscanf(line, "start(%d): group(%d) %d %128[^\n]",&alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message)<4))
         {
            now = time(NULL);
            alarm->link = NULL;
            alarm->changed = 0;

            status = sem_wait(&rw_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");

            insert_alarm (alarm);
            fprintf(stdout, "Alarm(%d) Inserted by Main Thread %d Into Alarm List at %d: Group(%d) %d %s\n",
                      alarm->alarm_id, pthread_self(), alarm->seconds, alarm->group_id, now, alarm->message);

            status = sem_post(&rw_mutex);
            if (status != 0)
            err_abort (status, "Unlock mutex");
          }

        else if (!(sscanf(line, "change(%d): group(%d) %d %128[^\n]",&alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message)<4))
             {
                  now = time(NULL);

                  status = sem_wait(&rw_mutex);
                  if (status != 0)
                    err_abort (status, "Lock mutex");

                  change_alarm(alarm);

                  fprintf(stdout, "Alarm(%d) Changed at %d: Group(%d) %d %s\n",
                          alarm->alarm_id, now, alarm->group_id, alarm->time, alarm->message);


                  status = sem_post(&rw_mutex);
                  if (status != 0)
                      err_abort (status, "Unlock mutex");
            }
        }
}
