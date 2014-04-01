#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#define bool	_Bool
#define true	1
#define false	0

#define BUFSIZE 8192

    /* SETTINGS */
#define SERVER_PORT 65533
#define MAXTHREADS 15
#define CLIENT_VERSION 1
#define CLIENT_MD5 "098f6bcd4621d373cade4e832627b4f6"
#define LAUNCH_STRING "java -Xms1G -Xmx1G -jar /home/serega6531/mcserver/lobby/craftbukkit.jar"

FILE * usersfile, *serverpipe;
int lsock = 0;

void strcut(char *str, int begin, int len)
{
    int l = strlen(str);

    if (len < 0) len = l - begin;
    if (begin + len > l) len = l - begin;
    strncpy(str, str + begin, len);
    str[len] = '\0';
}

int strpos(char *str, char *substr){
	int pos =  strstr(str, substr) - str;
	if(!pos) return 0;
	return pos;
}

bool isXML(char *string){
	int a, b, c, i;

	a = 0;
	b = 0;
	c = 0;
	i = 0;

	for(;i < strlen(string); i++){
		if(string[i] == '<') a++;
		if(string[i] == '>') b++;
		if(string[i] == '/') c++;
	}
	return (a == b && a == (c * 2) && a != 0);
}

bool hasXMLKey(char *string, char *key){
	char buf1[strlen(key) + 3], buf2[strlen(key) + 4];

	sprintf(buf1, "<%s>", key);
	sprintf(buf2, "</%s>", key);
	return (strstr(string, buf1) != NULL && strstr(string, buf2) != NULL);
}

void getXMLData(char *string, char *key, char *result){
	char buf[50], buf2[strlen(string)];
	int p, s;

	strcpy(buf2, string);
	sprintf(buf, "<%s>", key);
	p = strpos(buf2, buf) + strlen(buf);
	sprintf(buf, "</%s>", key);
	s = strpos(buf2, buf);
	strcut(buf2, p, s - p);
	strcpy(result, buf2);
}

void stop(){
	puts("Stopping server...");
	fclose(usersfile);
	pclose(serverpipe);
	close(lsock);
	exit(0);
}

void ExitListener(int sig){
    signal(sig, SIG_IGN);
    stop();
}

bool haveLoginAndPassword(char *login, char *password){
	char buf[128], buf1[64], buf2[64];

	while(fgets(buf, sizeof(buf), usersfile) != NULL){
		getXMLData(buf, "login", buf1);
		getXMLData(buf, "password", buf2);
		if(strcmp(buf1, login) == 0 &&  strcmp(buf2, password) == 0) {fseek(usersfile, 0, SEEK_SET);return true;}
	}
	fseek(usersfile, 0, SEEK_SET);
	return false;
}

void addPlayer(char *player){

}

void removePlayer(char *player){

}

void processAnswer(char *result, char *message){
	char buf[100];

	if(isXML(message) && hasXMLKey(message, "type") && hasXMLKey(message, "login") && hasXMLKey(message, "password")){
		char type[strlen(message)], login[strlen(message)], password[strlen(message)];
		getXMLData(message, "type", type);
		getXMLData(message, "login", login);
		getXMLData(message, "password", password);
		if(strcmp(type, "auth") == 0){
			if (haveLoginAndPassword(login, password)){
				sprintf(result, "<response>success</response><version>%d</versions>", CLIENT_VERSION);
			} else {
				strcpy(result, "<response>bad login</response>");
			}
		} else if(strcmp(type, "reg") == 0 && hasXMLKey(message, "mail")){
			char mail[50], fmail[50], flogin[50];
			getXMLData(message, "mail", mail);

			while(fgets(buf, sizeof(buf), usersfile) != NULL){
				getXMLData(buf, "login", flogin);
				getXMLData(buf, "mail", fmail);
				if(strcmp(login, flogin) == 0 || strcmp(mail, fmail) == 0){
					strcpy(result, "<response>already exists</response>");return;
				}
			}
			sprintf(buf, "<login>%s</login><password>%s</password><mail>%s</mail>\n", login, password, mail);
			fputs(buf, usersfile);
			fseek(usersfile, 0, SEEK_SET);
			strcpy(result, "<response>success</response>");
		} else if(strcmp(type, "gameauth") == 0 && hasXMLKey(message, "md5")){
			char md5[50];
			getXMLData(message, "md5", md5);

			if(haveLoginAndPassword(login, password)){
				if(strcmp(md5, CLIENT_MD5) == 0){
					addPlayer(login);
					strcpy(result, "<response>success</response>");
				} else strcpy(result, "<response>bad checksum</response>");
			} else strcpy(result, "<response>bad login</response>");
		} else strcpy(result, "<response>bad login</response>");
	} else strcpy(result, "<response>bad login</response>");
}

void * f00(void *data)
{
        char buf[BUFSIZE], answer[70];
        int asock = *(int *) data, nread = 0;

        while((nread = read(asock, buf, BUFSIZE)) > 0){
        	printf("Catch: %s", buf);
        	processAnswer(answer, buf);
        	write(asock, answer, strlen(answer));
        	shutdown(asock, 0);
        	return NULL;
        }
        return NULL;
}

int main(int argc, char **argv)
{
	int on = 1, asock = 0;
	struct  sockaddr_in sa;
	pthread_t threadid[MAXTHREADS], nthreads = 0, x = 0;

	signal(SIGINT, stop);

	puts("Starting server...");
	usersfile = fopen("Base.dat", "a+");
	//serverpipe = popen(LAUNCH_STRING, "a+");

	puts("Creating socket...");
	if ((lsock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		puts("Socket creating error");

	if(setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		puts("setsockopt error");

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port   = htons(SERVER_PORT);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);

	if((bind(lsock, (struct sockaddr *) &sa, sizeof(sa))) < 0)
		puts("bind error");

	if((listen(lsock, 5)) < 0)
		puts("listen error");

	puts("Done!\nWaiting from connections...");

	while(1){
		if((asock = accept(lsock, NULL, NULL)) < 0)
			puts("accept error");

		if(pthread_create(&threadid[x++], NULL, (void *)&f00, &asock) != 0)
			puts("thread creating error");

		if(nthreads++ >= MAXTHREADS)
			break;
	}

	for(x = 0; x < nthreads; x++)
		if(pthread_join(threadid[x], NULL) < 0)
			puts("pthread join error");

	return 0;
}
