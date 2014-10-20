#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h> 
#include <sys/stat.h> 
#include <fcntl.h>

#define LED_TEST "/dev/bfspi0.5"
#define MAX_BUF	60

#define FLASH_LED_COMMAND 	0x01
#define POLL_CMD 			0x02

char *fname;

int main (int argc, char *argv[])
{
	int i;
	int flt = -1, fld = -1;
	int st = 0;
	int dled = 0;
	uint8_t din[MAX_BUF], dpoll[MAX_BUF], rx_cnt = 0, cmd_cnt = 0;
	printf("..bfspi test..\n");
	if (argc >= 2) {
		fname = strdup(argv[1]);
		cmd_cnt = (uint8_t) atoi(argv[2]);
		for (i = 0; i < cmd_cnt; i++) {
			din[i] = atoi(argv[i+3]);
		} 
		//din = (uint8_t) atoi(argv[3]);
		//dport = (uint8_t) atoi(argv[4]);
	} else {
		printf("Usage: bfspi-test <dev> <cmd_length> <>....\n");
		return 0;
	}
	printf("..opening %s ..\n",(const char *)fname);
	//flt = open(LED_TEST,O_WRONLY);
	fld = open(fname,O_RDWR);
	if ((fld < 0)) {
	   printf("opening device error!!\n");
	} else {
		dled = 1;
		//st = write(flt, &dled,1);
		{
			//printf("..Writing data %d:%d:%d\n",fcmd,din,dport);
			printf("..Writing data %d\n",cmd_cnt);
			st = write(fld, &cmd_cnt,1);
			if (st < 0) {
				printf("Writing cmd_cnt error; %d!!!\n",st);
				goto out;
			}
			printf("..Writing cmd_cnt: %d st %d\n",cmd_cnt, st);
			for (i = 0; i < cmd_cnt; i++) {
				st = write(fld, &din[i],1);
				if (st < 0) {
					printf("Writing din[%d] error: %d!!!\n",i,st);
					goto out;
				}
				printf("..Writing din[%d] st %d\n",i, st);
			}
			i = 0;
			if (din[0] == POLL_CMD) {
				printf("..Reading poll msg wait slave to response..\n");
				usleep(20);
				printf("..Reading poll msg started..\n");
				
				#if 1
				st = read(fld, &rx_cnt, 1);
				if (st < 0) {
					printf("Reading din[%d] error: %d!!!\n",i,st);
					goto out;
				}
				printf("..Reading dummy rx data : %d st %d\n",rx_cnt, st);
				#endif
				
				st = read(fld, &rx_cnt, 1);
				if (st < 0) {
					printf("Reading din[%d] error: %d!!!\n",i,st);
					goto out;
				}
				printf("..Reading rx cnt : %d st %d\n",rx_cnt, st);
				
				if (rx_cnt <= 0)
					rx_cnt = 10;
				for (i = 0; i < rx_cnt; i++) {
					st = read(fld, &dpoll[i], 1);
					if (st < 0) {
						printf("Reading rx data[%d] error: %d!!!\n",i,st);
						goto out;
					}
					printf("..Reading rx data[%d]: 0x%02X st %d\n",i,dpoll[i] , st);					
				}
			}
			/*
			st = write(fld, &din,1);
			if (st < 0) {
				printf("Writing din error: %d!!!\n",st);
				goto out;
			}
			printf("..Writing din st %d\n",st);
			st = write(fld, &dport,1);
			if (st < 0) {
				printf("Writing dport error: %d!!!\n",st);
				goto out;
			}
			printf("..Writing data st %d\n",st);
			*/
		}
		//sleep(5);
		dled = 4;
		//st = write(flt, &dled,1);
out:
		close(fld);
		//close(flt);
	}
	return 0;
}
