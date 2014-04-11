/* C server part of NTLauncher
 * Written by serega6531
 * From github.com/serega6531/NTLauncher-authserver
 * Thanks HoShiMin and Asmodai for helping */

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

   /* НАСТРОЙКИ */
#define SERVER_PORT 65533                     //Порт, на котором запущен сервер
#define MAXTHREADS 15                         //Максимальное количество одновременно обрабатываемых игроков
#define CLIENT_VERSION 0                      //Версия клиента
#define CLIENT_MD5 "58e8c6b9374e0d4ff71df7ba3ba136cc"    //MD5 хеш клиента
#define LAUNCH_STRING "cd server && java -Xms512M -Xmx512M -jar craftbukkit.jar"    //Строка, запускающая сервер. Обязателен переход в папку сервера.
#define TIME_TO_ENTER 90                     //Время на вход в игру (в секундах)
#define HWIDS_DIR "PlayersHWIDs/"            //Папка с HWID'ами игроков. Не менять без необходимости.

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

/* Запускает команду command в pipe, дает доступ на чтение и запись */

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

/* Отправляет сообщение во входной поток сервера, запущенного через popen2().
 * Сообщение должно оканчиваться \n. */

void sendMessage(char *message) {
	write(serverpipe[WRITE], message, strlen(message));
}

/* Вырезает строку из подстроки
 * strcut("Hello world", 3, 2) == llo */

void strcut(char *str, int begin, int len) {
	int l = strlen(str);

	if (len < 0)
		len = l - begin;
	if (begin + len > l)
		len = l - begin;
	memmove(str, str + begin, len);
	str[len] = '\0';
}

/* Получает позицию substr в строке str
 * strpos("Hello world", "world") == 7 */

int strpos(char *str, char *substr) {
	char *pos = strstr(str, substr);
	if (!pos)
		return 0;
	return pos - str;
}

/* Проверяет, является ли строка синтаксически верным XML */

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

/* Проверяет, имеет ли XML строка string ключ key (т.е. содержит ли она <key> и </key>) */

bool hasXMLKey(char *string, char *key) {
	char buf1[strlen(key) + 3], buf2[strlen(key) + 4];

	sprintf(buf1, "<%s>", key);
	sprintf(buf2, "</%s>", key);
	return (strstr(string, buf1) != NULL && strstr(string, buf2) != NULL );
}

/* Получает из строки string ключ key и записывает в буфер result.
 * maxlen - размер result - 1.
 * Пример:
 * char buf[10];
 * getXMLData("<val1>test1</val1><val2>test2</val2>", "val1", buf, 9);
 * buf содержит test1 */

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

/* Закрывает все файлы, pipes и сокет */

void stop() {
	puts("Stopping server...");
	fclose(usersfile);
	fclose(hwidfile);
	close(serverpipe[0]);
	close(serverpipe[1]);
	close(lsock);

	exit(0);
}

/* Функция, слушающая Ctrl + C */

void exitListener(int sig) {
	signal(sig, SIG_IGN );
	sendMessage("stop\n");
	sleep(1);
	stop();
}

/* Ищет аккаут в файле игроков */

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

/* Проверяет, забанен ли HWID игрока */

bool isHWIDBanned(char *hwid) {
	char buf[100];
	char fhwid[50];

	fseek(hwidfile, 0, SEEK_SET);
	while (fgets(buf, sizeof(buf), hwidfile) != NULL ) {
		getXMLData(buf, "hwid", fhwid, 49);
		if (strcmp(fhwid, hwid) == 0)
			return true;
	}
	return false;
}

/* Проверяет, находится ли HWID в списке HWID'ов игрока player */

bool hasHWIDInBase(char *player, char *hwid) {
	char buf[40];

	snprintf(buf, sizeof(buf), "%s%s_HWID.dat", HWIDS_DIR, player);
	FILE * file = fopen(buf, "r");
	while (fgets(buf, sizeof(buf), file) != NULL ) {
		if (strcmp(buf, hwid) == 0) {
			fclose(file);
			return true;
		}
	}
	fclose(file);
	return false;
}

/* Добавляет HWID в файл HWID'ов игрока player */

void addHWIDToList(char *player, char *hwid) {
	char buf[75];

	snprintf(buf, sizeof(buf), "%s%s_HWID.dat", HWIDS_DIR, player);
	FILE * file = fopen(buf, "a+");
	sprintf(buf, "%s\n", hwid);
	fputs(hwid, file);
	fclose(file);
}

/* Добавляет игрока в список на удаление из вайтлиста. Удаляет через TIME_TO_ENTER секунд. */

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

/* Удаляет игрока из списка на удаление */

void removeFromTimeList(el *elem) {
	LL_DELETE(head, elem);
}

/* Добавляет игрока в вайтлист */

void addPlayer(char *player) {
	char message[strlen(player) + 16];

	sprintf(message, "whitelist add %s\n", player);
	sendMessage(message);
	addToTimeList(player);
}

/* Удаляет игрока из вайтлиста */

void removePlayer(char *player) {
	char message[strlen(player) + 21];

	sprintf(message, "whitelist remove %s\n", player);
	sendMessage(message);
}

/* Главная функция обвязки. Работает с сообщением клиента message и записывает результат в result */

void processAnswer(char *result, char *message) {
	char print[BUFSIZE];

	sprintf(print,
			"=============================================================\nClient send message.\nRaw message: %s\n",
			message);
	if (isXML(message) && hasXMLKey(message, "type")
			&& hasXMLKey(message, "login") && hasXMLKey(message, "password")) {  // Если есть необходимые поля...
		int mlen = strlen(message) - 1;
		char type[mlen], login[mlen], password[mlen];
		getXMLData(message, "type", type, mlen - 1);      // ...Получаем их
		getXMLData(message, "login", login, mlen - 1);
		getXMLData(message, "password", password, mlen - 1);
		sprintf(print, "%sType: %s\nLogin: %s\nPassword: %s\n", print, type,
				login, password);
		if (strcmp(type, "auth") == 0) {             // Если тип - авторизация
			if (haveLoginAndPassword(login, password)) {     // Если игрок есть есть в базе
				sprintf(result,
						"<response>success</response><version>%d</versions>",
						CLIENT_VERSION);
			} else {
				strcpy(result, "<response>bad login</response>");
			}
		} else if (strcmp(type, "reg") == 0 && hasXMLKey(message, "mail")   // Если тип - регистрация
				&& hasXMLKey(message, "hwid")) {                            // И есть необходимые поля для регистрации
			bool res = true;
			char mail[mlen], fmail[mlen], flogin[mlen], hwid[mlen], buf[75];

			getXMLData(message, "mail", mail, mlen - 1);              // Получаем данные
			getXMLData(message, "hwid", hwid, mlen - 1);

			sprintf(print, "%sMail: %s\nHWID: %s\n", print, mail, hwid);

			if (isHWIDBanned(hwid)) {                                   // Если HWID забанен...
				strcpy(result, "<response>banned</response>");          // ...отправляем сообщение об этом
				res = false;
			} else {
				addHWIDToList(login, hwid);                            // Иначе добавляем HWID в список
			}
			if (res) {
				fseek(usersfile, 0, SEEK_SET);
				while (fgets(buf, sizeof(buf), usersfile) != NULL ) {          // Ищем игрока с таким логином или почтой в базе
					getXMLData(buf, "login", flogin, mlen - 1);
					getXMLData(buf, "mail", fmail, mlen - 1);
					if (strcmp(login, flogin) == 0
							|| strcmp(mail, fmail) == 0) {
						strcpy(result, "<response>already exists</response>"); // Если есть - отправляем сообщение
						res = false;
						break;
					}
				}
			}
			if (res) {                     // Если нет ошибок
				sprintf(buf,
						"<login>%s</login><password>%s</password><mail>%s</mail>\n",
						login, password, mail);
				fputs(buf, usersfile);
				strcpy(result, "<response>success</response>");
			}
		} else if (strcmp(type, "gameauth") == 0 && hasXMLKey(message, "md5")  // Если тип - игровая авторизация
				&& hasXMLKey(message, "hwid")) {                               // И есть нужные поля
			char md5[mlen], hwid[mlen];
			bool res = true;

			getXMLData(message, "md5", md5, mlen - 1);                         // Получаем данные
			getXMLData(message, "hwid", hwid, mlen - 1);
			sprintf(print, "%sMD5: %s\nHWID: %s\n", print, md5, hwid);

			if (!isHWIDBanned(hwid)) {                                        // Если HWID не забанен
				if (!hasHWIDInBase(login, hwid))                              // И его нет в базе HWID'ов игрока
					addHWIDToList(login, hwid);                               // Добавляем
			} else {
				strcpy(result, "<response>banned</response>");                // Если HWID забанен
				res = false;
			}
			if (!res || haveLoginAndPassword(login, password)) {              // Если логин и пароль верные
				if (strcmp(md5, CLIENT_MD5) == 0) {                           // И md5 совпадает
					addPlayer(login);                                         // Добавляем игрока в вайтлист
				} else {
					strcpy(result, "<response>bad checksum</response>");      // Иначе отправляем сообщение об ошибке
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

/* Функция, обрабатывающая консольное сообщение пользователя.
 * Возвращаемое значение - нужно ли отправлять сообщение серверу. */

bool processConsoleMessage(char *message) {
	char command[strlen(message)], arg[strlen(message)], buf[50], buf2[100];

	if (strcmp(message, "stop\n") == 0) {     // Если сообщение - stop
		sendMessage("stop\n");
		puts("Waiting for server stopping...");
		sleep(3);
		stop();
		return true;
	}
	if (sscanf(message, "%s %s", command, arg) < 2)   // Если команда составлена неверно
		return false;                                 // Выходим

	if (strcmp(command, "banuser") == 0) {            // Если команда - banuser
		snprintf(buf, sizeof(buf), "%s%s_HWID.dat", HWIDS_DIR, arg);
		FILE * file = fopen(buf, "r");
		if (file != NULL )
			while (fgets(buf, sizeof(buf), file) != NULL ) {    // Баним все HWID'ы из файла пользователя
				printf("Banned HWID %s\n", buf);
				snprintf(buf2, sizeof(buf2),
						"<hwid>%s</hwid><player>%s</player>", buf, arg);
				fputs(buf2, hwidfile);
				fclose(file);
			}
		return true;
	} else if (strcmp(command, "banhwid") == 0) {        // Если команда - banhwid
		snprintf(buf, sizeof(buf), "<hwid>%s</hwid>", arg);
		fputs(buf, hwidfile);                            // Записываем его в список забаненных
		puts("Banned!");
		return true;
	}
	return false;
}

/* Функция потока, обрабатывающая подключённого игрока */

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

/* Функция потока, слушающая вывод сервера */

void *f01(void *data) {
	char buf[BUFSIZE];

	while (1) {
		read(serverpipe[READ], buf, BUFSIZE - 1);
		printf("[SERVER]%s", buf);
		memset(buf, 0, strlen(buf));
	}
	return NULL ;
}

/* Функция потока, управляющая подключениями клиентов */

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

/* Функция потока, удаляющая игроков из вайтлиста */

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

/* Инициализация ВСЕГО */

int main(int argc, char **argv) {
	int on = 1;
	struct sockaddr_in sa;
	pthread_t mcthread = 0, sockthread = 0, removethread = 0;
	char line[150] = { '\0' };
	struct stat st = { 0 };

	signal(SIGINT, exitListener); // Ставим слушатель Ctrl + C на функцию

	puts("Starting server...");

	if (stat(HWIDS_DIR, &st) == -1) {   // если нет папки с HWID'ами...
		mkdir(HWIDS_DIR, 0700);         // ...создаем её
	}
	usersfile = fopen("Base.dat", "a+");           // Открываем
	hwidfile = fopen("BannedHWIDs.dat", "a+");     // файлы
	if (usersfile == NULL || hwidfile == NULL ) {  // Если ошибка -
		puts("Error opening file");                // Выводим её
		return -1;                                 // и закрываем программу
	}

	puts("Launching minecraft server...");
	if (popen2(LAUNCH_STRING, &serverpipe[WRITE], &serverpipe[READ]) <= 0) {   // Запускаем сервер, устанавливаем ввод и вывод на ячейки массива
		puts("Server launch error.");                                          // Не получилось? Выключаем обвязку.
		stop();
	}
	if (pthread_create(&mcthread, NULL, (void *) &f01, NULL ) != 0) {          // Запускаем поток, слушающий сообщения с сервера
		puts("thread creating error");                                         // или выключаем обвязку
		stop();
	}

	puts("Creating socket...");
	if ((lsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {            // Запускаем сокет
		puts("Socket creating error");                              // Думаю, вы уже поняли, что тут
		stop();
	}

	if (setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {   // Устанавливаем параметры сокета
		puts("setsockopt error");
		stop();
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(SERVER_PORT);          // Устанавливаем порт
	sa.sin_addr.s_addr = htonl(INADDR_ANY );   // Адрес - любой

	if ((bind(lsock, (struct sockaddr *) &sa, sizeof(sa))) < 0) {   // Биндим
		puts("bind error");
		stop();
	}

	if ((listen(lsock, 5)) < 0) {         // Слушаем
		puts("listen error");
		stop();
	}

	puts("Done!\nWaiting from connections...");

	if (pthread_create(&sockthread, NULL, (void *) &f02, NULL ) != 0) {    // Запускаем поток, управляющий подключениями клиентов
		puts("thread creating error");
		stop();
	}

	if (pthread_create(&removethread, NULL, (void *) &f03, NULL ) != 0) {   // Запускаем поток, удаляющий игроков из вайтлиста
		puts("thread creating error");
		stop();
	}

	while (fgets(line, sizeof(line), stdin) != NULL ) {     // Слушаем пользовательский ввод
		if (!processConsoleMessage(line))                   // Если сообщение - не системное
			sendMessage(line);                              // Отправляем его
	}

	stop();
	return 0;
}

/* ТЕСТОВЫЕ СООБЩЕНИЯ
 <type>auth</type><login>test</login><password>test</password>
 <type>reg</type><login>test</login><password>test</password><mail>test@test.com</mail><hwid>test</hwid>
 <type>gameauth</type><login>test</login><password>test</password><md5>test</md5><hwid>test</hwid>
 */
