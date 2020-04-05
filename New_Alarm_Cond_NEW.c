#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <stdio.h>
#include "errors.h"

#define ADD 0
#define CHANGE 1
#define NO_MATCH 2

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
} append_list


/*
  display thread only needs to hold the informations regarding the
  alarm requested.
*/
typedef struct display_tag {
  alarm_t *           alarm;
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


  int alarm_thread_flag = 0;
  int reader_flag = 0;

  alarm_t *alarm_list = NULL;
  append_list *list_to_append = NULL;

  sem_t main_semaphore;
  sem_t display_sem;

  int append_flag = 0;
  int change_flag = 0;


void change_alarm(alarm_t *new)
 {
   int changed = time(NULL);
   alarm_t **last, *next;

   last = &alarm_list;
   next = *last;

   while(next!=NULL)
   {
     fprintf(stdout,"here\n");
     if (next->alarm_id == new->alarm_id)
     {
       next->changed = 1;
       next->group_id = new->group_id;
       next->time = time(NULL) + new->time;
       strcpy(next->message, new->message);
       curr_alarm_t = next;
       break;
     }
     last = &next->link;
     next = next->link;
   }
 }

void alarm_insert (alarm_t *alarm)
{
    int status;
    alarm_t **last, *next;
    last = &alarm_list;
    next = *last;
    while (next != NULL) {
        if (next->alarm_id >= alarm->alarm_id) {
            alarm->link = next;
            *last = alarm;

            //defines current alarm
            curr_alarm_t = alarm;
            break;
        }
        last = &next->link;
        next = next->link;
    }
    if (next == NULL) {
        *last = alarm;
        alarm->link = NULL;
    }
}

 void * display_thread(void * arg){

       thread_alarm * alarm = (thread_alarm *) arg;

       char msg[128];
       strcpy(msg, alarm->alarm->message);
       int alarm_id = alarm->alarm->alarm_id;
       int group_id = alarm->alarm->group_id;
       int interval = alarm->alarm->seconds;
       int has_changed =0;

       time_t now;
       time_t display_interval = time(NULL);

       while(1){
           //Readers-first synchronization
           sem_wait(&display_sem);
           reader_flag++;
           if(reader_flag == 1)
               sem_wait(&main_semaphore);
           sem_post(&display_sem);
           now = time(NULL);

           //If alarm was removed, exit.
           if(alarm->removed == 1){
               flockfile(stdout);
               printf("Display Thread <%d> Has Stopped Printing Message of Alarm<%d> at %d:Group<%d> %s\n",
                      pthread_self(), alarm_id, (int) now, group_id, msg);
               fflush(stdout);
               funlockfile(stdout);

               //Exit safely and not abruptly
               //If this is the last thread to exit, make sure it unlocks the semaphore as well
               sem_wait(&display_sem);
               reader_flag--;
               if(reader_flag ==0)
                   sem_post(&main_semaphore);
               sem_post(&display_sem);
               //Free the thread_alarm struct used in this thread
               free(alarm);
               pthread_exit(NULL);


           }

           //If the alarm has been altered, display the message and then set the altered flag.
           if(alarm->alarm->changed == 1){
               printf("Alarm With Message Number (%d) Replaced at %d: %d Message(%d) %s\n",
                      alarm->alarm->alarm_number, (int) now, alarm->alarm->seconds,alarm->alarm->alarm_number, alarm->alarm->message);
               interval = alarm->alarm->seconds;
               display_interval = now + interval;
               has_changed = 1;
               alarm->alarm->changed = 0;
               strcpy(msg, alarm->alarm->message);
           } else if(now >= display_interval){
               //Check if the interval has been satisfied and
               if(has_changed == 1){
                   printf("Replacement Alarm With Message Number (%d) Displayed at %d: %d Message(%d) %s\n",
                          alarm->alarm->alarm_number, (int) now, alarm->alarm->seconds,alarm->alarm->alarm_number, alarm->alarm->message);

               } else {
                   printf("Alarm With Message Number (%d) Displayed at %d: %d Message(%d) %s\n",
                          alarm->alarm->alarm_number, (int) now, alarm->alarm->seconds,alarm->alarm->alarm_number, alarm->alarm->message);
               }
               display_interval = now+ alarm->alarm->seconds;
           }

           //Readers-first synchro
           sem_wait(&display_sem);
           reader_flag--;
           if(reader_flag ==0)
               sem_post(&main_semaphore);
           sem_post(&display_sem);
       }

   }

   /*
    * Creates the list of display threads based on the last appended to the list.
    * It functions as a queue, creating threads in the order which they were added to the list.
    * We are assuming the `last` element is will not be passed as null, otherwise this could result in a
    * segfault.
    *
    */
   display_thread_list * create_display_threads(display_thread_list * last){
       if(last == NULL)
           errno_abort("Last should not be null!");

       //Reference to old element
       append_list * old;
       //New list node for the display thread list
       display_thread_list * new_list_node;
       //New thread_alarm
       thread_alarm * new_thread_alarm;
       //create a new thread. We don't care about the reference so we can discard it after the loop
       pthread_t new_thread;
       int counter = 0;
       while(list_to_append != NULL){


           old = list_to_append;
           new_list_node = (display_thread_list *)malloc(sizeof(display_thread_list));
           if (new_list_node == NULL) {
               printf("Out of memory!\n");
               exit(1);
           }
           new_list_node->previous = last;
           new_list_node->next = NULL;
           //Initialize the new thread's data
           new_thread_alarm = (thread_alarm *) malloc(sizeof(thread_alarm));
           if (new_thread_alarm == NULL) {
               printf("Out of memory!\n");
               exit(1);
           }
           new_thread_alarm->alarm = old->alarm;
           new_thread_alarm->removed = 0;
           //Add the new thread data to a list struct
           last->data = new_thread_alarm;
           //Append the
           last->next = new_list_node;
           last = last->next;

           list_to_append = list_to_append->next;
           pthread_create(&new_thread, NULL, display_thread, (void *) new_thread_alarm);
           free(old);
           counter++;
       }

       //Return the reference to the last element
       return last;

 }

 /*
  * The alarm thread's start routine.
  */
void *alarm_thread (void *arg)
{
  thread_list = (display_thread_list *) malloc(sizeof(display_thread_list));

  if (thread_list == NULL)
      errno_abort("Out of memory\n");
  display_thread_list * last = thread_list;
  /*
   * Loop forever, processing commands. The alarm thread will
   * be disintegrated when the process exits.
   * The default alarm_thread_flag will be 0, so the thread will busy wait
   * until there are tasks for it
   *
   */
  while (1) {

      //Busy wait while the flag is 0
      while(alarm_thread_flag == 0);
      sem_wait(&main_semaphore);

      if(append_flag == 1) {
          //Create display threads
          last = create_display_threads(last);
          append_flag = 0;

      }

      if (change_flag == 1){
          change_alarm();
          change_flag = 0;
      }
      alarm_thread_flag = 0;
      sem_post(&main_semaphore);
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


    if(sem_init(&main_semaphore,0,1) < 0){
        printf("Error creating semaphore!");
        exit(1);
    }

    if(sem_init(&display_sem,0,1) < 0){
        printf("Error creating semaphore!");
        exit(1);
    }

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
    if (sscanf(line, "%30[^(](%d): group(%d) %d %128[^\n]",
        keyword,&alarm->alarm_id, &alarm->group_id, &alarm->seconds, alarm->message) == 4) {

          //The strcmp() compares two strings character by character.
          if (strcmp(keyword, "Start_Alarm") == 0) {

              now = time(NULL);
              alarm->link = NULL;
              alarm->changed = 0;

              status = sem_wait(&main_semaphore);
                  if (status != 0)
                      err_abort (status, "Lock mutex");

              alarm_insert (alarm);
              fprintf(stdout, "Alarm(%d) Inserted by Main Thread %d Into Alarm List at %d: Group(%d) %d %s\n",
                      alarm->alarm_id, pthread_self(), alarm->seconds, alarm->group_id, now, alarm->message);

              to_append = (append_list *) malloc(sizeof(append_list));
              to_append->next = NULL;
              to_append->alarm = alarm;
              to_append->last = NULL;
              //If the list is null, make the list reference the element
              if(list_to_append == NULL) {
                  list_to_append = to_append;
              } else {
                  //Otherwise, append in the next available
                  if(list_to_append->next == NULL){
                    list_to_append->next = to_append;
                    list_to_append->last = to_append;
                  } else {
                list_to_append->last->next = to_append;
                list_to_append->last = to_append;
              }
              }
              append_flag = 1;
              alarm_thread_flag = 1;

                  status = sem_post(&main_semaphore);
                  if (status != 0)
                      err_abort (status, "Unlock mutex");
            }

            else if (strcmp(keyword, "Change_Alarm") == 0) {

                  now = time(NULL);

                  status = sem_wait(&main_semaphore);
                  if (status != 0)
                    err_abort (status, "Lock mutex");

                  change_alarm(alarm);

                  fprintf(stdout, "Alarm(%d) Changed at %d: Group(%d) %d %s\n",
                          next->alarm_id, now, next->group_id, next->time, next->message);
                  alarm_thread_flag = 1;
                  change_flag = 1;

                  status = sem_post(&main_semaphore);
                  if (status != 0)
                      err_abort (status, "Unlock mutex");
            }
          } else {
              fprintf (stderr, "Bad command\n");
              free (alarm);
            }
      }
}
