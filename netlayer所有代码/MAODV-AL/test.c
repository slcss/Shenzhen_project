#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static void hand_ler(int signo)
{
    printf("Hello\n");
  	signal(SIGALRM, hand_ler); //让内核做好准备，一旦接受到SIGALARM信号,就执行 handler
    alarm(1);

}

int main()
{
    hand_ler(0);
    int i;
    alarm(1);
    signal(SIGALRM, hand_ler);
    for(i=1;i<10;i++)
    {
        printf("sleep %d ...\n",i);
        sleep(1);
    }

    return 0;
}
