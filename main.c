#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include "utlist.h"

/* SETTINGS */
#define SERVER_PORT 65533
#define MAXTHREADS 15
#define CLIENT_VERSION 0
#define CLIENT_MD5 "098f6bcd4621d373cade4e832627b4f6"
#define LAUNCH_STRING "cd server && java -Xms512M -Xmx512M -jar craftbukkit.jar"
#define TIME_TO_ENTER 90

#define BUFSIZE 8192
#define READ 0
#define WRITE 1

struct playertime {
	short unsigned int time;
	char name[20];
};

typedef struct el {
	struct playertime pt;
    struct el *next;
} el;

FILE * usersfile;
int lsock = 0, serverpipe[2];
el *head = NULL;

pid_t popen2(const char *command, int *infp, int *outfp) {
	int p_stdin[2], p_stdout[2];
	pid_t pid;

	if (pipe(p_stdin) != 0 || pipe(p_stdout) != 0)
		return -1;

	pid = fork();

	if (pid < 0)
		return pid;
	else if (pid == 0) {
		close(p_stdin[WRITE]);
		dup2(p_stdin[READ], READ);
		close(p_stdout[READ]);
		dup2(p_stdout[WRITE], WRITE);

		execl("/bin/sh", "sh", "-c", command, NULL );
		perror("execl");
		exit(1);
	}

	if (infp == NULL )
		close(p_stdin[WRITE]);
	else
		*infp = p_stdin[WRITE];

	if (outfp == NULL )
		close(p_stdout[READ]);
	else
		*outfp = p_stdout[READ];

	return pid;
}

void strcut(char *str, int begin, int len) {
	int l = strlen(str);

	if (len < 0)
		len = l - begin;
	if (begin + len > l)
		len = l - begin;
	memmove(str, str + begin, len);
	str[len] = '\0';
}

int strpos(char *str, char *substr) {
	int pos = strstr(str, substr);
	if (!pos)
		return 0;
	return pos - str;
}

bool isXML(char *string) {
	int a, b, c, i;

	a = 0;
	b = 0;
	c = 0;
	i = 0;

	for (; i < strlen(string); i++) {
		if (string[i] == '<')
			a++;
		if (string[i] == '>')
			b++;
		if (string[i] == '/')
			c++;
	}
	return (a == b && a == (c * 2) && a != 0);
}

bool hasXMLKey(char *string, char *key) {
	char buf1[strlen(key) + 3], buf2[strlen(key) + 4];

	sprintf(buf1, "<%s>", key);
	sprintf(buf2, "</%s>", key);
	return (strstr(string, buf1) != NULL && strstr(string, buf2) != NULL );
}

void getXMLData(char *string, char *key, char *result) {
	char buf[strlen(key) + 4], buf2[strlen(string)];
	int p, s;

	strcpy(buf2, string);
	sprintf(buf, "<%s>", key);
	p = strpos(buf2, buf) + strlen(buf);
	sprintf(buf, "</%s>", key);
	s = strpos(buf2, buf);
	if(s - p > 0){
		strcut(buf2, p, s - p);
		strcpy(result, buf2);
	} else {
		strcpy(result, "");
	}
}

void stop() {
	puts("Stopping server...");
	fclose(usersfile);
	close(serverpipe[0]);
	close(serverpipe[1]);
	close(lsock);

	exit(0);
}

void exitListener(int sig) {
	signal(sig, SIG_IGN );
	stop();
}

bool haveLoginAndPassword(char *login, char *password) {
	char buf[128], buf1[64], buf2[64];

	fseek(usersfile, 0, SEEK_SET);
	while (fgets(buf, sizeof(buf), usersfile) != NULL ) {
		getXMLData(buf, "login", buf1);
		getXMLData(buf, "password", buf2);
		if (strcmp(buf1, login) == 0 && strcmp(buf2, password) == 0)
			return true;
	}
	return false;
}

void sendMessage(char *message) {
	write(serverpipe[WRITE], message, strlen(message));
}

void addToTimeList(char *name) {
	el *element;
	struct playertime ptime;

	if ((element = (el*)malloc(sizeof(el))) == NULL) stop();
	strcpy(ptime.name, name);
	ptime.time = TIME_TO_ENTER;
	element->pt = ptime;
	LL_APPEND(head, element);
}

void removeFromTimeList(el *elem) {
	LL_DELETE(head, elem);
}

void addPlayer(char *player) {
	char message[strlen(player) + 16];

	sprintf(message, "whitelist add %s\n", player);
	sendMessage(message);
	addToTimeList(player);
}

void removePlayer(char *player) {
	char message[strlen(player) + 21];

	sprintf(message, "whitelist remove %s\n", player);
	sendMessage(message);
}

void processAnswer(char *result, char *message) {
	char buf[100], print[BUFSIZE];

	sprintf(print, "=============================================================\nClient send message.\nRaw message: %s\n", message);
	if (isXML(message) && hasXMLKey(message, "type")
			&& hasXMLKey(message, "login") && hasXMLKey(message, "password")) {
		int mlen = strlen(message);
		char type[mlen], login[mlen], password[mlen];
		getXMLData(message, "type", type);
		getXMLData(message, "login", login);
		getXMLData(message, "password", password);
		sprintf(print, "%sType: %s\nLogin: %s\nPassword: %s\n", print, type,
				login, password);
		if (strcmp(type, "auth") == 0) {
			if (haveLoginAndPassword(login, password)) {
				sprintf(result,
						"<response>success</response><version>%d</versions>",
						CLIENT_VERSION);
			} else {
				strcpy(result, "<response>bad login</response>");
			}
		} else if (strcmp(type, "reg") == 0 && hasXMLKey(message, "mail")) {
			bool res = true;

			char mail[mlen], fmail[mlen], flogin[mlen];
			getXMLData(message, "mail", mail);
			sprintf(print, "%sMail: %s\n", print, mail);

			fseek(usersfile, 0, SEEK_SET);
			while (fgets(buf, sizeof(buf), usersfile) != NULL ) {
				getXMLData(buf, "login", flogin);
				getXMLData(buf, "mail", fmail);
				if (strcmp(login, flogin) == 0 || strcmp(mail, fmail) == 0) {
					strcpy(result, "<response>already exists</response>");
					res = false;
					break;
				}
			}
			if (res) {
				sprintf(buf,
						"<login>%s</login><password>%s</password><mail>%s</mail>\n",
						login, password, mail);
				fputs(buf, usersfile);
				strcpy(result, "<response>success</response>");
			}
		} else if (strcmp(type, "gameauth") == 0 && hasXMLKey(message, "md5")) {
			char md5[mlen];
			getXMLData(message, "md5", md5);
			sprintf(print, "%sMD5: %s\n", print, md5);

			if (haveLoginAndPassword(login, password)) {
				if (strcmp(md5, CLIENT_MD5) == 0) {
					addPlayer(login);
					strcpy(result, "<response>success</response>");
				} else
					strcpy(result, "<response>bad checksum</response>");
			} else
				strcpy(result, "<response>bad login</response>");
		} else
			strcpy(result, "<response>bad login</response>");
	} else
		strcpy(result, "<response>bad login</response>");
	sprintf(print,
			"%sResponse: %s\n=============================================================\n",
			print, result);
	puts(print);
}

void * f00(void *data) {
	char buf[BUFSIZE], answer[70];
	int asock = *(int *) data, nread = 0;

	while ((nread = read(asock, buf, BUFSIZE)) > 0) {
		processAnswer(answer, buf);
		write(asock, answer, strlen(answer));
		shutdown(asock, 0);
	}
	return NULL ;
}

void *f01(void *data) {
	char buf[BUFSIZE];

	while (1) {
		read(serverpipe[READ], buf, BUFSIZE - 1);
		printf("[SERVER]%s", buf);
		memset(buf, 0, strlen(buf));
	}
	return NULL ;
}

void *f02(void *data) {
	int x = 0, asock = 0;
	pthread_t threadid[MAXTHREADS], nthreads = 0;

	while (1) {
		if ((asock = accept(lsock, NULL, NULL )) < 0)
			puts("accept error");

		if (pthread_create(&threadid[x++], NULL, (void *) &f00, &asock) != 0)
			puts("thread creating error");

		if (nthreads++ >= MAXTHREADS)
			break;
	}

	for (x = 0; x < nthreads; x++)
		if (pthread_join(threadid[x], NULL ) < 0)
			puts("pthread join error");
	return NULL ;
}

void *f03(void *data) {
	el *tmp;

	while(1) {
		LL_FOREACH(head, tmp){
			tmp->pt.time--;
			if(tmp->pt.time == 0){
				removePlayer(tmp->pt.name);
				removeFromTimeList(tmp);
			}
		}
		sleep(1);
	}
	return NULL;
}

int main(int argc, char **argv) {
	int on = 1;
	struct sockaddr_in sa;
	pthread_t mcthread = 0, sockthread = 0, removethread = 0;
	char line[150] = { '\0' };

	signal(SIGINT, exitListener);

	puts("Starting server...");
	usersfile = fopen("Base.dat", "a+");
	if (usersfile == NULL ) {
		puts("Error opening file");
		return -1;
	}

	puts("Launching minecraft server...");
	if (popen2(LAUNCH_STRING, &serverpipe[WRITE], &serverpipe[READ]) <= 0) {
		puts("Server launch error.");
		stop();
	}
	if (pthread_create(&mcthread, NULL, (void *) &f01, NULL ) != 0) {
		puts("thread creating error");
		stop();
	}

	puts("Creating socket...");
	if ((lsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		puts("Socket creating error");
		stop();
	}

	if (setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
		puts("setsockopt error");
		stop();
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(SERVER_PORT);
	sa.sin_addr.s_addr = htonl(INADDR_ANY );

	if ((bind(lsock, (struct sockaddr *) &sa, sizeof(sa))) < 0) {
		puts("bind error");
		stop();
	}

	if ((listen(lsock, 5)) < 0) {
		puts("listen error");
		stop();
	}

	puts("Done!\nWaiting from connections...");

	if (pthread_create(&sockthread, NULL, (void *) &f02, NULL ) != 0){
		puts("thread creating error");
		stop();
	}

	if (pthread_create(&removethread, NULL, (void *) &f03, NULL ) != 0) {
		puts("thread creating error");
		stop();
	}

	while (fgets(line, sizeof(line), stdin) != NULL ) {
		sendMessage(line);
	}

	stop();
	return 0;
}
