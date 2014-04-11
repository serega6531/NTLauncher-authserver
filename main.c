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
#include <sys/stat.h>
#include "utlist.h"

/* SETTINGS */
#define SERVER_PORT 65533
#define MAXTHREADS 15
#define CLIENT_VERSION 0
#define CLIENT_MD5 "098f6bcd4621d373cade4e832627b4f6"
#define LAUNCH_STRING "cd server && java -Xms512M -Xmx512M -jar craftbukkit.jar"
#define TIME_TO_ENTER 90
#define HWIDS_DIR "PlayersHWIDs/"

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

FILE * usersfile, *hwidfile;
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
	char *pos = strstr(str, substr);
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

void getXMLData(char *string, char *key, char *result, int maxlen) {
	char buf[64];
	char *start, *end;
	int len;

	len = snprintf(buf, sizeof buf - 1, "<%s>", key);
	start = strstr(string, buf);
	snprintf(buf, sizeof(buf) - 1, "</%s>", key);
	end = strstr(string, buf);

	if (!start || !end || start > end) {
		result[0] = '\0';
		return;
	}

	start += len;
	len = end - start;

	if (len > maxlen)
		len = maxlen;

	memcpy(result, start, len);
	result[len] = '\0';
}

void stop() {
	puts("Stopping server...");
	fclose(usersfile);
	fclose(hwidfile);
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
		getXMLData(buf, "login", buf1, 63);
		getXMLData(buf, "password", buf2, 63);
		if (strcmp(buf1, login) == 0 && strcmp(buf2, password) == 0)
			return true;
	}
	return false;
}

bool isHWIDBanned(char *hwid) {
	char buf[50];
	char fhwid[33];

	fseek(hwidfile, 0, SEEK_SET);
	while (fgets(buf, sizeof(buf), hwidfile) != NULL ) {
		getXMLData(buf, "hwid", fhwid, 32);
		if (strcmp(fhwid, hwid) == 0)
			return true;
	}
	return false;
}

bool hasHWIDInBase(char *player, char *hwid){
	char buf[75];

	snprintf(buf, sizeof(buf), "%s%s_HWID.dat", HWIDS_DIR, player);
	FILE * file = fopen(buf, "r");
	while (fgets(buf, sizeof(buf), file) != NULL ) {
			if (strcmp(buf, hwid) == 0){
				fclose(file);
				return true;
			}
	}
	fclose(file);
	return false;
}

void addHWIDToList(char *player, char *hwid){
	char buf[75];

	snprintf(buf, sizeof(buf), "%s%s_HWID.dat", HWIDS_DIR, player);
	FILE * file = fopen(buf, "a+");
	sprintf(buf, "%s\n", hwid);
	fputs(hwid, file);
	fclose(file);
}

void sendMessage(char *message) {
	write(serverpipe[WRITE], message, strlen(message));
}

void addToTimeList(char *name) {
	el *element;
	struct playertime ptime;

	if ((element = (el*) malloc(sizeof(el))) == NULL )
		stop();
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
	char print[BUFSIZE];

	sprintf(print,
			"=============================================================\nClient send message.\nRaw message: %s\n",
			message);
	if (isXML(message) && hasXMLKey(message, "type")
			&& hasXMLKey(message, "login") && hasXMLKey(message, "password")) {
		int mlen = strlen(message) - 1;
		char type[mlen], login[mlen], password[mlen];
		getXMLData(message, "type", type, mlen - 1);
		getXMLData(message, "login", login, mlen - 1);
		getXMLData(message, "password", password, mlen - 1);
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
		} else if (strcmp(type, "reg") == 0 && hasXMLKey(message, "mail")
				&& hasXMLKey(message, "hwid")) {
			bool res = true;
			char mail[mlen], fmail[mlen], flogin[mlen], hwid[mlen], buf[75];

			getXMLData(message, "mail", mail, mlen - 1);
			getXMLData(message, "hwid", hwid, mlen - 1);

			sprintf(print, "%sMail: %s\nHWID: %s\n", print, mail, hwid);

			fseek(usersfile, 0, SEEK_SET);
			while (fgets(buf, sizeof(buf), usersfile) != NULL ) {
				getXMLData(buf, "login", flogin, mlen - 1);
				getXMLData(buf, "mail", fmail, mlen - 1);
				if (strcmp(login, flogin) == 0 || strcmp(mail, fmail) == 0) {
					strcpy(result, "<response>already exists</response>");
					res = false;
					break;
				}
			}
			if (isHWIDBanned(hwid)) {
				strcpy(result, "<response>banned</response>");
				res = false;
			} else {
				addHWIDToList(login, hwid);
			}
			if (res) {
				sprintf(buf,
						"<login>%s</login><password>%s</password><mail>%s</mail>\n",
						login, password, mail);
				fputs(buf, usersfile);
				strcpy(result, "<response>success</response>");
			}
		} else if (strcmp(type, "gameauth") == 0 && hasXMLKey(message, "md5")
				&& hasXMLKey(message, "hwid")) {
			char md5[mlen], hwid[mlen];
			bool res = true;

			getXMLData(message, "md5", md5, mlen - 1);
			getXMLData(message, "hwid", hwid, mlen - 1);
			sprintf(print, "%sMD5: %s\nHWID: %s\n", print, md5, hwid);

			if (haveLoginAndPassword(login, password)) {
				if (strcmp(md5, CLIENT_MD5) == 0) {
					addPlayer(login);
				} else {
					strcpy(result, "<response>bad checksum</response>");
					res = false;
				}
				if (!isHWIDBanned(hwid)){
					if(!hasHWIDInBase(login, hwid)){
						addHWIDToList(login, hwid);
					}
				} else {
					strcpy(result, "<response>banned</response>");
					res = false;
				}
				if (res)
					strcpy(result, "<response>success</response>");
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

	while (1) {
		LL_FOREACH(head, tmp)
		{
			tmp->pt.time--;
			if (tmp->pt.time == 0) {
				removePlayer(tmp->pt.name);
				removeFromTimeList(tmp);
			}
		}
		sleep(1);
	}
	return NULL ;
}

int main(int argc, char **argv) {
	int on = 1;
	struct sockaddr_in sa;
	pthread_t mcthread = 0, sockthread = 0, removethread = 0;
	char line[150] = { '\0' };
	struct stat st = {0};

	signal(SIGINT, exitListener);

	puts("Starting server...");

	if (stat(HWIDS_DIR, &st) == -1) {
	    mkdir(HWIDS_DIR, 0700);
	}
	usersfile = fopen("Base.dat", "a+");
	hwidfile = fopen("BannedHWIDs.dat", "a+");
	if (usersfile == NULL || hwidfile == NULL ) {
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

	if (pthread_create(&sockthread, NULL, (void *) &f02, NULL ) != 0) {
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

/* TEST MESSAGES 
 <type>auth</type><login>test</login><password>test</password>
 <type>reg</type><login>test</login><password>test</password><mail>test@test.com</mail><hwid>test</hwid>
 <type>gameauth</type><login>test</login><password>test</password><md5>test</md5><hwid>test</hwid>
 */
