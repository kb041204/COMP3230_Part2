/* 
2019-20 Programming Project Part2

File name:	part2-mandelbrot.c 
Name:		Chan Tik Shun
Student ID:	3035536553
Date: 		23/11/2019 
Version: 	1.2
Platform:	X2GO (Xfce 4.12, distributed by Xubuntu)
Compilation:	gcc part2-mandelbrot.c -o part2-mandelbrot -l SDL2 -l m -pthread
*/

//Using SDL2 and standard IO
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
#include <unistd.h>
#include "Mandel.h"
#include "draw.h"
#include <pthread.h>
#include <semaphore.h>

typedef struct task {
	int start_row;
	int num_of_rows;
} TASK;


typedef struct message {
	int row_index;
	float rowdata[IMAGE_WIDTH];
} MSG;

float * pixels; //store the 2D image as a linear array of pixels (in row-major format)

TASK ** pool; //create buffer array
int pool_size = 0;
int task_in_pool = 0;
int next_to_put = 0;
int next_to_pull = 0;

pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t new_task = PTHREAD_COND_INITIALIZER;
pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

int total_row_assigned = 0;
int total_row_done = 0;
int all_tasks_put_into_buffer = 0;
//====================CONSUMER====================
void *thread_func (void *arg) {

	int id = *((int *)arg);

	printf("Worker(%d): Start up. Wait for task!\n", id);

	int * individual_tasks_done = (int *)malloc(sizeof(int)); //tasks done from this thread

	while(total_row_assigned != IMAGE_HEIGHT || !all_tasks_put_into_buffer) {
		pthread_mutex_lock(&pool_lock); //1.1: acquire pool lock
		while(task_in_pool == 0) //1.2: task pool is empty
			pthread_cond_wait(&new_task, &pool_lock); //1.21: wait for signal new_task

		TASK * curr_task = *(pool + next_to_pull); //1.3: copy the task from buffer
		next_to_pull = (next_to_pull + 1) % pool_size; //1.4: change the next pointer
		task_in_pool--;
		
		total_row_assigned += curr_task->num_of_rows; //update statistics for other threads

		pthread_cond_signal(&not_full); //1.5: signal producer the buffer is not full
		pthread_mutex_unlock(&pool_lock); //1.6: release the lock

		//1.7 process the task
		int x, y;
		struct timespec child_start_compute, child_end_compute;
		printf("Worker(%d): Start the computation...\n", id);
		clock_gettime(CLOCK_MONOTONIC, &child_start_compute); //record start time
		for (y=curr_task->start_row; y<(curr_task->start_row + curr_task->num_of_rows); y++) {
    			for (x=0; x<IMAGE_WIDTH; x++) {
				*(pixels + (y*IMAGE_HEIGHT+x)) = Mandelbrot(x, y); //1.9: return the result
			}
    		}
		
		//1.8: display computation time
		clock_gettime(CLOCK_MONOTONIC, &child_end_compute);
		float child_difftime = (child_end_compute.tv_nsec - child_start_compute.tv_nsec)/1000000.0 + (child_end_compute.tv_sec - child_start_compute.tv_sec)*1000.0;
		printf("Worker(%d): ... completed. Elapsed time = %.3f ms\n", id, child_difftime);

		(*individual_tasks_done)++;
		total_row_done += curr_task->num_of_rows; //update statistics for main thread
	}
	pthread_exit((void *)individual_tasks_done); //terminate thread and return pointer to tasks done
}

//====================MAIN====================
int main( int argc, char* args[] )
{
	pthread_t thread_id[atoi(args[1])]; //for pthread_create

	if (argc != 4) { //not enough/too many arguments
		printf("Invalid argument!\n");
		printf("Usage: ./part2-mandelbrot <number of workers> <number of rows in a task> <number of buffers>\n");
		exit(0);
	}

	if(atoi(args[1])*atoi(args[2])>IMAGE_HEIGHT) {
		printf("Number of workers times number of line is larger than %d, please try again.\n", IMAGE_HEIGHT);
		exit(0);
	}

	//data structure to store the start and end times of the whole program
	struct timespec start_time, end_time;
	//get the start time
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	
	//data structure to store the start and end times of the computation
	struct timespec start_compute, end_compute;
	
	pixels = (float *) malloc(sizeof(float) * IMAGE_WIDTH * IMAGE_HEIGHT); //allocate memory to store the pixels

	if (pixels == NULL) {
		printf("Out of memory!!\n");
		exit(1);
	}
	
	clock_gettime(CLOCK_MONOTONIC, &start_compute);

	float difftime, child_difftime;
	
	int rows_to_complete = atoi(args[2]);

	//====================PRODUCER====================
	int * thread_number = (int *)malloc(sizeof(int)*atoi(args[1])); // array for storing thread number (from 0 to n-1), used for storing the (void * arg) of thread func

	pool_size = atoi(args[3]);

	pool = malloc(pool_size * sizeof(pool)); // allocate memory to pool

	//create threads
	for(int i=0; i<atoi(args[1]); i++) {
		*(thread_number + i) = i; // for recording the thread number
		int status = pthread_create(&(thread_id[i]), NULL, thread_func, (void*)(thread_number + i)); // passing the thread number into the pointer for consistent result
		if(status != 0) {
			printf("Fail to create thread %d, terminate program now...\n", i);
			exit(0);
		}
	}

	//processing
	int curr_row = 0;
	while(curr_row <= IMAGE_HEIGHT) {
		pthread_mutex_lock(&pool_lock); //1.1: acquire the lock
		while(task_in_pool == pool_size) //1.2: task poll is full
			pthread_cond_wait(&not_full, &pool_lock); //1.21 wait for signal not_full
		
		//create new task
		TASK * temp_task = (TASK *)malloc(sizeof(temp_task));
		temp_task->start_row = curr_row;
		if(curr_row + rows_to_complete > IMAGE_HEIGHT) { //large than the image
			temp_task->num_of_rows = IMAGE_HEIGHT - curr_row;
		} else {
			temp_task->num_of_rows = rows_to_complete;
		}
		curr_row += rows_to_complete;

		*(pool + next_to_put) = temp_task; //1.3: put the task into the pool
		next_to_put = (next_to_put + 1) % pool_size; //1.4: update the next pointer
		task_in_pool++;
		
		pthread_cond_signal(&new_task); //1.5: signal consumers
		pthread_mutex_unlock(&pool_lock); //1.6: release the lock
	}

	pthread_mutex_lock(&pool_lock);
	all_tasks_put_into_buffer = 1; //2: inform all consumers no more tasks is sent out
	pthread_mutex_unlock(&pool_lock);

	while(total_row_done != IMAGE_HEIGHT); //wait for all the thread to terminate

	//3: join threads, collect and print tasks completed foe each thread
	for(int i=0; i<atoi(args[1]); i++) {
		int * task_completed;
		pthread_join(thread_id[i], (void **) &task_completed);
		printf("Worker thread %d has terminated and completed %d tasks\n", i, *task_completed);
	}	

	printf("All worker threads have terminated\n");

	//report process timing in user & system mode
	struct rusage temp;
	getrusage(RUSAGE_SELF, &temp);
	printf("Total time spent by process in user mode = %.3f ms\n", (float) ((float)temp.ru_utime.tv_sec*(float)1000 + (float)temp.ru_utime.tv_usec/(float)1000));
	printf("Total time spent by process in system mode = %.3f ms\n", (float) ((float)temp.ru_stime.tv_sec*(float)1000 + (float)temp.ru_stime.tv_usec/(float)1000));

	//report total process timing
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	difftime = (end_time.tv_nsec - start_time.tv_nsec)/1000000.0 + (end_time.tv_sec - start_time.tv_sec)*1000.0;
	printf("Total elapse time measured by the process = %.3f ms\n", difftime);
	
	//draw the image by using the SDL2 library
	printf("Draw the image\n");
	DrawImage(pixels, IMAGE_WIDTH, IMAGE_HEIGHT, "Mandelbrot demo", 3000);

	return 0;
}
