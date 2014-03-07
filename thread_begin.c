#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#define THREAD_NUM 20
#define STRING_SIZE 100
#define LOG_SIZE 256
#define MEMLIST_NUM 10
#define MAX_READ_DATA_THREAD 10

//#define THREAD_CANCEL
//#define THREAD_CANCEL_ASYNC
//#define THREAD_NONE_CANCEL

//#define MUTEX_RECURSIVE
//#define MUTEX_ERRORCHECK

//#define COND_BROADCAST

#define GLOB_PRINT

#define THREAD_COND
static pthread_t thread_list[THREAD_NUM];

static pthread_mutex_t main_string_mutex;
static pthread_mutexattr_t main_string_mutex_attr;

static pthread_mutex_t print_count_mutex;
static pthread_mutexattr_t print_count_mutex_attr;
static int print_count = 0;//with mutex

static pthread_key_t thread_log_key = 0;

static pthread_mutex_t glob_log_mutex;
static pthread_mutexattr_t glob_log_mutex_attr;
static char glob_log_buff[THREAD_NUM * LOG_SIZE];//with mutex
static int glob_log_count = 0;//with mutex, exclude \0

static pthread_cond_t thread_cond;
static pthread_mutex_t thread_cond_mutex;
static int thread_count = 0;

struct thread_read_data
{
	int data;
	sem_t sem;
};
static struct thread_read_data read_data;

void init_read_data(void)
{
	read_data.data = 0;
	sem_init(&(read_data.sem), 0, MAX_READ_DATA_THREAD);
}

void release_glob_log(void)
{
	pthread_mutex_lock(&glob_log_mutex);
	printf(glob_log_buff);
	memset(glob_log_buff, 0, sizeof(glob_log_buff));
	glob_log_count = 0;
	pthread_mutex_unlock(&glob_log_mutex);
}

void print_glob_log(char *string)
{
	#ifdef GLOB_PRINT
	int string_len = strlen(string);
	int glob_log_remain_count = 0;
	
	//glob_log_buff glob_log_count mutex lock
	while(pthread_mutex_trylock(&glob_log_mutex) != 0);
	
	glob_log_remain_count = sizeof(glob_log_buff) - glob_log_count;
	
	if(glob_log_remain_count > 1)//1 remain for \0
	{
		strncat(glob_log_buff, string, glob_log_remain_count - 1);
		glob_log_count = glob_log_count + strlen(string);
	}
	else
	{//glob log is full
		printf(glob_log_buff);
		memset(glob_log_buff, 0, sizeof(glob_log_buff));
		glob_log_count = 0;
		strncat(glob_log_buff, string, sizeof(glob_log_buff) - 1);
	}
	
	//glob_log_buff glob_log_count mutex lock
	pthread_mutex_unlock(&glob_log_mutex);
	#else
	printf(string);
	#endif
}

void log_close(void * nothing)//it will be call when thread end or canceled
{//thread-specific value should not be null, otherwise function will not be called
	char *thread_log = (char *)pthread_getspecific(thread_log_key);
	
	print_glob_log(thread_log);
		
	free(thread_log);
}

void memlist_cleanup(void *mem)
{
	free(mem);
	print_glob_log("free memlist\n");
}

void * thread_print(void * string)
{
	char * string_return = NULL;
	char string_return_buff[STRING_SIZE];
	char string_print[STRING_SIZE];
	char *log_buff = NULL;
	char * mem_list[MEMLIST_NUM];
	char print_count_string_buff[5];
	int i = 0;
	
	init_read_data();
	
	#ifdef THREAD_COND
	//wait for cond to start thread
	{	
		int current_thread_count = 0;
		for(current_thread_count = 0; current_thread_count < THREAD_NUM; current_thread_count ++)
		{
			if(pthread_equal(pthread_self(), thread_list[current_thread_count]))
			{
				break;
			}
		}
		
		pthread_mutex_lock(&thread_cond_mutex);
		while(1)
		{
			if(thread_count < current_thread_count)
			{
				pthread_cond_wait(&thread_cond, &thread_cond_mutex);		
			}
			else
			{
				break;
			}
		}
		pthread_mutex_unlock(&thread_cond_mutex);
	}
	#endif
	
	while(sem_trywait(&(read_data.sem)) != 0)//nonblock version is sem_wait
	{
		print_glob_log("reading thread maxium\n");
	}
	pthread_cleanup_push(sem_post, &(read_data.sem));
	
	#ifdef THREAD_CANCEL
	int cancel_type_prev;
		#ifdef THREAD_CANCEL_ASYNC
		pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &cancel_type_pre);
		#else
		pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &cancel_type_pre);
		#endif
	
	#else
	int cancel_state_prev;
		#ifdef THREAD_NONE_CANCEL
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancel_state_prev);	
		#else
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancel_state_prev);
		#endif	
	#endif	
	
	memset(string_print, 0, sizeof(string_print));
	strncpy(string_print, string, sizeof(string_print));
	//main:string_buff mutex unlock, here is string_print
	pthread_mutex_unlock(&main_string_mutex);
	
	//test for exit clean up function
	//clean up function will be done when cancel or pthread_exit()
	memset(mem_list, 0, sizeof(mem_list));
	for(i = 0; i < MEMLIST_NUM; i++)
		mem_list[i] = (char *)malloc(1);
	for(i = 0; i < MEMLIST_NUM; i++)		
		pthread_cleanup_push(memlist_cleanup, mem_list[i]);

	memset(string_return_buff, 0, sizeof(string_return_buff));
	
	pthread_setspecific(thread_log_key, log_buff);
	log_buff = malloc(LOG_SIZE);
	memset(log_buff, 0, sizeof(log_buff));
	
	#ifdef THREAD_CANCEL
	#ifndef THREAD_CANCEL_ASYNC
	pthread_testcancel();//cancel point for synchronous cancel
	#endif
	#endif
	
	print_glob_log(string_print);
	//lock mutex of print_count
	pthread_mutex_lock(&print_count_mutex);
	memset(print_count_string_buff, 0, sizeof(print_count_string_buff));
	snprintf(print_count_string_buff, sizeof(print_count_string_buff), "%d\n", print_count);
	print_glob_log(print_count_string_buff);
	snprintf(string_return_buff, sizeof(string_return_buff), "thread return %d\n", print_count);
	print_count ++;
	pthread_mutex_lock(&thread_cond_mutex);
	thread_count ++;
	#ifdef THREAD_COND
	#ifdef COND_BROADCAST
	pthread_cond_broadcast(&thread_cond);
	#else
	pthread_cond_signal(&thread_cond);
	#endif
	#endif	
	pthread_mutex_unlock(&thread_cond_mutex);
	
	//unlock mutex of print_count
	pthread_mutex_unlock(&print_count_mutex);
	
	#ifdef THREAD_CANCEL
	#ifndef THREAD_CANCEL_ASYNC
	pthread_testcancel();//cancel point for synchronous cancel
	#endif
	#endif
	
	string_return = malloc(strlen(string_return_buff) + 1);
	
	if(string_return != NULL)
	{
		strncpy(string_return, string_return_buff, strlen(string_return_buff) + 1);
	}
	
	//set the cancel type and state back
	#ifdef THREAD_CANCEL
		pthread_setcanceltype(cancel_type_prev, NULL);
	#else
		pthread_setcancelstate(cancel_state_prev, NULL);	
	#endif		
	
	//pop out cleanup function
	for(i = 0; i < MEMLIST_NUM; i ++)
		pthread_cleanup_pop(1);//if 0, do not execute cleanup function
	
	pthread_cleanup_pop(1);//post the sem
	return string_return;
	//pthread_exit(string_return);//this one will call the cleanup function
	//pthread_detach(pthread_self());//return, but do not return to join
}

int main(int argc, char **argv)
{
	pthread_attr_t thread_attr;
	char string_buff[STRING_SIZE];//mutex
	int count = 0;
	
	memset(thread_list, 0, sizeof(thread_list));
	memset(glob_log_buff, 0, sizeof(glob_log_buff));
	
	pthread_cond_init(&thread_cond, NULL);
	pthread_mutex_init(&thread_cond_mutex, NULL);
	
	pthread_key_create(&thread_log_key, log_close);
	
	pthread_mutexattr_init(&print_count_mutex_attr);
	pthread_mutexattr_init(&glob_log_mutex_attr);
	pthread_mutexattr_init(&main_string_mutex_attr);
	
	#ifdef MUTEX_RECURSIVE
	pthread_mutex_setkind_np(&print_count_mutex_attr, PTHREAD_MUTEX_RECURSIVE_NP);
	pthread_mutex_setkind_np(&glob_log_mutex_attr, PTHREAD_MUTEX_RECURSIVE_NP);
	pthread_mutex_setkind_np(&main_string_mutex_attr, PTHREAD_MUTEX_RECURSIVE_NP);
	#endif	
	#ifdef MUTEX_ERRORCHECK
	pthread_mutex_setkind_np(&print_count_mutex_attr, PTHREAD_MUTEX_ERRORCHECK_NP);
	pthread_mutex_setkind_np(&glob_log_mutex_attr, PTHREAD_MUTEX_ERRORCHECK_NP);
	pthread_mutex_setkind_np(&main_string_mutex_attr, PTHREAD_MUTEX_ERRORCHECK_NP);
	#endif
	
	//can be instead of pthread_mutex_t glob_log_mutex = PTHREAD_MUTEX_INITIALIZER while declare
	pthread_mutex_init(&print_count_mutex, (const pthread_mutexattr_t *)(&print_count_mutex_attr));//second argument is attribute
	pthread_mutex_init(&glob_log_mutex, (const pthread_mutexattr_t *)(&glob_log_mutex_attr));
	pthread_mutex_init(&main_string_mutex, (const pthread_mutexattr_t *)(&main_string_mutex_attr));
	
	pthread_attr_init(&thread_attr);
	/*
	pthread_attr_setaffinity_np
	pthread_attr_setguardsize
	pthread_attr_setschedparam
	pthread_attr_setscope
	pthread_attr_setdetachstate
	pthread_attr_setinheritsched
	pthread_attr_setschedpolicy
	pthread_attr_setstack
	pthread_attr_setstackaddr
	pthread_attr_setstacksize
	*/
	
	for(count = 0; count < THREAD_NUM; count ++)
	{//create thread
		//string_buff mutex lock
		pthread_mutex_lock(&main_string_mutex);
		
		memset(string_buff, 0, sizeof(string_buff));
		snprintf(string_buff, sizeof(string_buff), "print thread %d, print", count);
		
		pthread_create(&(thread_list[count]), &thread_attr, thread_print, string_buff);
		//pthread_create(&(thread_list[count]), &thread_attr, thread_print, string_buff);//use attribute for create
	}
	pthread_attr_destroy(&thread_attr);
	
	#ifdef THREAD_CANCEL
	for(count = 0; count < THREAD_NUM; count ++)
	{
		pthread_cancel(thread_list[count]);
	}
	#endif
	
	for(count = 0; count < THREAD_NUM; count ++)
	{//make sure the thread is finished
		char * pstring_return = NULL;
		
		if(!pthread_equal(pthread_self(), thread_list[count]))//check the join thread is not itself
		{
			pthread_join(thread_list[count], (void **)(&pstring_return));//wait and get the return value
			
			if(pstring_return != PTHREAD_CANCELED)
			{
				print_glob_log(pstring_return);
				free(pstring_return);	
			}
		}
	}

	{//check is all the semaphore be released
		int sem_value = 0;
		sem_getvalue(&(read_data.sem), &sem_value);	
		if(sem_value != MAX_READ_DATA_THREAD)
		{
			print_glob_log("ERROR: some semaphore is leaking\n");
		}
		else
		{
			print_glob_log("semaphore status ok\n");
		}
	}
	
	release_glob_log();
	
	sem_destroy(&(read_data.sem));
	
	pthread_cond_destroy(&thread_cond);
	pthread_mutex_destroy(&thread_cond_mutex);
	
	pthread_mutexattr_destroy(&print_count_mutex_attr);
	pthread_mutexattr_destroy(&glob_log_mutex_attr);
	pthread_mutexattr_destroy(&main_string_mutex_attr);
		
	return 0;
}
