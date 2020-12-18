#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */

#define SHMSZ 27


int main()
{
  int ch= 0;
  int randomvalue = 0;
  int shmid;
  key_t key;
  int *shm,*s;
  sem_t *mutex;

  //name the shared memory segment
  key = 1000;

  //create & initialize semaphore
  mutex = sem_open(SEM_NAME,O_CREAT,0644,1);
  if(mutex == SEM_FAILED)
    {
      perror("unable to create semaphore");
      sem_unlink(SEM_NAME);
      exit(-1);
    }

  //create the shared memory segment with this key
  shmid = shmget(key,SHMSZ,IPC_CREAT|0666);

  if(shmid<0)
    {
      perror("failure in segmentt");
      exit(-1);
    }

  //attach this segment to virtual memory
  shm = shmat(shmid,NULL,0);

  //start writing into memory
  s = shm;
  while (ch < 1000){
      randomvalue =  rand() % 10 + 1;
      if(randomvalue == 2){
        sem_wait(mutex);
        *s++;
        sem_post(mutex);
      }
      ch++;
  }
  

//   //the below loop could be replaced by binary semaphore
//   while(*shm != '*')
//     {
//       sleep(1);
//     }
//   sem_close(mutex);
//   sem_unlink(SEM_NAME);
//   shmctl(shmid, IPC_RMID, 0);
//   _exit(0);
}