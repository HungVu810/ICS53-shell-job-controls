#include <stdio.h>
#include <unistd.h>

int main(void){
	for(int i = 0; i < 5; i++){
		printf("hello\n");
		sleep(2);
	}
}
