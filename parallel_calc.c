#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <math.h>

#define STEP 1e-9

//Structure information about the thread
typedef struct 
{
	int cpu;
	size_t number_of_threads;
	double begin_of_interval;
	double end_of_interval;
	double val;
	pthread_t pid;
} thread_info;

//Error output function
void error(char* str)
{
	perror(str);
	exit(-1);
}

//Integration function
void integrate(thread_info* th_info) 
{
	double x = 0;
	double val = 0;
	double end_of_interval = th_info->end_of_interval;
	
	for (x = th_info->begin_of_interval; x < end_of_interval; x += STEP) 
	{
		val += (x * x) * STEP;
	}

	th_info->val = val;
}

//Set affinity between the thread and the cpu(the core) and starting integration
void* parallel_calc(void* arg) 
{
	thread_info* th_info = (thread_info*) arg;

	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(th_info->cpu, &cpuset);
	pthread_setaffinity_np(th_info->pid, sizeof (cpu_set_t), &cpuset);

	integrate(th_info);
	pthread_exit(NULL);
}

//Structure information about the logical cores (the topology) of the processor
typedef struct 
{
	int f;
	int s;
} proc_t;

//Getting information about the processor topology
static void parse_proctopology(proc_t* proc, int num_core, int num_thread) 
{
	int i = 0;
	int val = 0;
	char err;
	FILE* file;
	char* path = (char*) calloc(256, sizeof (char));
	for (i = 0; i < num_core; i++) 
	{
		sprintf(path, "/sys/bus/cpu/devices/cpu%d/topology/thread_siblings_list", i);
		file = fopen(path, "r");
		fscanf(file, "%d%c", &val, &err);
		proc[i].f = val;
		fscanf(file, "%d%c", &val, &err);
		proc[i].s = val;
		fclose(file);
	}
}

//Finding a free core
static int find_free_proc(proc_t* proc, int* thread_to_cpu, int num_core, int num_thread) 
{
	int i = 0;
	int j = 0;
	int flag = 0;
	for (i = 0; i < num_core; i++) 
	{
		for (j = 0; j < num_thread; j++) 
		{
			if (thread_to_cpu[j] != -1) 
			{
				if (proc[i].f == thread_to_cpu[j] || proc[i].s == thread_to_cpu[j]) 
				{
					flag = 1;
					break;
				}
			}
		}
		if (flag == 0)
			return i;
        flag = 0;
	}
	return -1;
}

//Spreading threads on cores
static void spread_threads(proc_t* proc, int* thread_to_cpu, int num_core, int num_thread) 
{
	int i = 0;
	int free_proc = 0;
	for (i = 0; i < num_thread; i++) 
	{
		if ((free_proc = find_free_proc(proc, thread_to_cpu, num_core, num_thread)) != -1)
			thread_to_cpu[i] = free_proc;
		else
			thread_to_cpu[i] = i % num_core;
	}
}

//Parallelization control function
int start_parallel(double begin, double end, double* res, int number_of_threads) 
{
	int i = 0;
	int j = 0;

	proc_t* proc = NULL;
	int* thread_to_cpu = NULL;
	int num = sysconf(_SC_NPROCESSORS_ONLN);
	double width_per_thread = (end - begin) / number_of_threads;

	thread_info** th_info = (thread_info**) calloc(number_of_threads, sizeof (thread_info *));

	if (th_info == NULL)
		return -1;

	for (i = 0; i < number_of_threads; i++) 
	{
		th_info[i] = (thread_info*) calloc(1, sizeof (thread_info));
		if (th_info[i] == NULL) 
		{
			for (j = 0; j < i; j++)
				free(th_info[j]);
			free(th_info);
			return -1;
		}
	}

	proc = (proc_t*) calloc(num, sizeof (proc_t));
	if (proc == NULL)
	{
		for (i = 0; i < number_of_threads; i++)
			free(th_info[i]);
		free(th_info);

		free(proc);
		return -1;
	}

	thread_to_cpu = (int*) calloc(number_of_threads, sizeof (int));
	if (thread_to_cpu == NULL)
	{
		for (i = 0; i < number_of_threads; i++)
			free(th_info[i]);
		free(th_info);

		free(proc);
		free(thread_to_cpu);
	}

	for (i = 0; i < number_of_threads; i++)
		thread_to_cpu[i] = -1;
	parse_proctopology(proc, num, number_of_threads);
	spread_threads(proc, thread_to_cpu, num, number_of_threads);

	for (i = 0; i < number_of_threads; i++) 
	{
		th_info[i]->begin_of_interval = i * width_per_thread + begin;
		th_info[i]->end_of_interval = (i + 1) * width_per_thread + begin;
		th_info[i]->val = 0.0;
		th_info[i]->cpu = thread_to_cpu[i];
	}

	for (i = 0; i < number_of_threads; i++) 
	{
		pthread_create(&(th_info[i]->pid), NULL, parallel_calc, th_info[i]);
    }
    
	for (i = 0; i < number_of_threads; i++) 
	{
		void* ret = NULL;
		pthread_join(th_info[i]->pid, &ret);
		*res += th_info[i]->val;
	}

	for (i = 0; i < number_of_threads; i++)
		free(th_info[i]);
	free(th_info);

	free(proc);
	free(thread_to_cpu);

	return 0;
}
