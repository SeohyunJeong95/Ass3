Readme 
1. First copy the files "New_Alarm_Cond.c", and "errors.h" into your
   own directory.

2. To compile the program "alarm_cond.c", use the following command:

      cc New_Alarm_Cond.c -D_POSIX_PTHREAD_SEMANTICS -lpthread

3. Type "a.out" to run the executable code.

4. At the prompt "ALARM>", two commands are available: 
Start_Alarm with the syntax Alarm> Start_Alarm(Alarm_ID): Group(Group_ID) Time Message, where
Alarm_ID, Group_ID, and Time are positive integer inputs, and
Message is any string of length up to 128 characters
Change_Alarm with the syntax Alarm> Change_Alarm(Alarm_ID): Group(Group_ID) Time Message
Ex.
  ALARM> Start_Alarm(2345): Group(13) 50 Will meet you at Grandma’s house at 6pm.
Or
  ALARM> Change_Alarm(2345): Group(21) 80 Will meet you at Grandma’s house later at 8 pm
If the user types in something other than one of the above two types of valid alarm requests, then an error message will be displayed, and the invalid request will be discarded.

  (To exit from the program, type Ctrl-d.)
