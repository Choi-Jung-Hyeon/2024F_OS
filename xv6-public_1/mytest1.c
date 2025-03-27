#include "types.h"
#include "user.h"
#include "stat.h"

int main()
{
  int i = 0;

  // DEFAULT PS
  printf(1, "ps(0)\n");
  ps(0); printf(1, "\n");
  for(i=1; i<11; i++){
    printf(1, "%d: ", i);
    if(getpname(i))
      printf(1, "Wrong pid\n");
    ps(i); 
    printf(1, "getnice: %d\n\n", getnice(i));
  }

  // PROPER SETNICE
  printf(1, "proper setnice\n");
  setnice(1, 21);
  setnice(2, 39);
  setnice(3, 1);
  
  printf(1, "ps(0)\n");
  ps(0); printf(1, "\n");
  for(i=1; i<11; i++){
    printf(1, "%d: ", i);
    if(getpname(i))
      printf(1, "Wrong pid\n");
    ps(i);
    printf(1, "getnice: %d\n\n", getnice(i));
  }

  // IMPROPER SETNICE
  printf(1, "improper setnice\n");
  setnice(1, 0);
  setnice(2, 44);
  setnice(3, -3);
  setnice(4, 29);
  setnice(5, -3);
  setnice(6, 45);

  printf(1, "ps(0)\n");
  ps(0); printf(1, "\n");
  for(i=1; i<11; i++){
    printf(1, "%d: ", i);
    if(getpname(i))
      printf(1, "Wrong pid\n");
    ps(i);
    printf(1, "getnice: %d\n\n", getnice(i));
  }

  //test
  printf(1, "test\n");
  setnice(0, 29);
  setnice(0, 44);
  ps(0); printf(1, "\n");
  exit();
}
