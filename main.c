/* C server part of NTLauncher
 * Written by serega6531
 * Page on GitHub - github.com/serega6531/NTLauncher-authserver
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
#include <errno.h>
#include "utlist.h"
#include "settings.h"

#if DATABASE == DB_MYSQL
#include <mysql.h>
#endif

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

#if DATABASE == DB_FILE
FILE * usersfile, *hwidfile;
#elif DATABASE == DB_MYSQL
MYSQL * mysql;
#endif
#if AUTO_RESTART == TRUE
pid_t serverpid = 0;
pthread_t restartthread = 0;
#endif
pthread_t mcthread = 0;
int lsock = 0, serverpipe[2];
el *head = NULL;
bool stopping = false;

void *f01(void *data);

/* Запускает команду в pipe, дает доступ на чтение и запись */

pid_t popen2(int *infp, int *outfp) {
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

		execv(JAVA_PATH, LAUNCH_ARGS);
		perror("exec");
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

#if AUTO_RESTART == TRUE
/* Проверяет, запущена ли программа с данным pid */bool PIDExists(pid_t pid) {
	if (kill(pid, 0) == 0) {
		return true;
	} else if (errno == ESRCH) {
		return false;
	} else {
		perror("Kill error");
		return false;
	}
}
#endif

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

/* Преобразует число в строку */
void itoa(int integer, char *result) {
	sprintf(result, "%d", integer);
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
#if DATABASE == DB_FILE
	fclose(usersfile);     // Закрываем
	fclose(hwidfile);// файлы
#elif DATABASE == DB_MYSQL
	mysql_close(mysql);   // соединения
#endif
	close(serverpipe[0]); // трубы
	close(serverpipe[1]);
	close(lsock);         // сокеты

	exit(0);
}

/* Функция, слушающая Ctrl + C */

void exitListener(int sig) {
	signal(sig, SIG_IGN );
	sendMessage("stop\n");
	sleep(2);    // Ждём остановки сервера. Замените число на своё (в секундах).
	stop();
}

/* Запускает minecraft сервер */
void startMCServer() {
#if AUTO_RESTART == FALSE
	if (popen2(&serverpipe[WRITE], &serverpipe[READ]) <= 0) { // Запускаем сервер, устанавливаем ввод и вывод на ячейки массива
#elif AUTO_RESTART == TRUE
	if ((serverpid = popen2(&serverpipe[WRITE], &serverpipe[READ])) > 0) {
		if (restartthread != 0) {
			pthread_kill(mcthread, SIGSTOP);
			mcthread = 0;
		}
		if (pthread_create(&mcthread, NULL, (void *) &f01, NULL ) != 0) { // Запускаем поток, слушающий сообщения с сервера
			puts("thread creating error");              // или выключаем обвязку
			stop();
		}
		printf("Server started, pid %d\n", serverpid);
	} else {
#endif
		puts("Server launch error.");       // Не получилось? Выключаем обвязку.
		stop();
	}
}

/* Ищет аккаут в файле игроков */

bool haveLoginAndPassword(char *login, char *password) {
#if DATABASE == DB_FILE
	char buf[128], buf1[64], buf2[64];

	fseek(usersfile, 0, SEEK_SET);
	while (fgets(buf, sizeof(buf), usersfile) != NULL ) { // Проходимся циклом по строкам
		getXMLData(buf, "login", buf1, 63);
		getXMLData(buf, "password", buf2, 63);
		if (strcmp(buf1, login) == 0 && strcmp(buf2, password) == 0)
		return true;
	}
#elif DATABASE == DB_MYSQL
	char buf[150];
	MYSQL_RES * result;
	bool ret;

	sprintf(buf, "SELECT * FROM `users` WHERE `login`='%s' AND `password`='%s'",
			login, password);
	mysql_query(mysql, buf);            // Отправляем запрос
	result = mysql_store_result(mysql);
	if (mysql_num_rows(result) > 0)      // Если есть такие пользователи
		ret = true;
	mysql_free_result(result);
	return ret;
#endif
	return false;
}

/* Проверяет, забанен ли HWID игрока */

bool isHWIDBanned(char *hwid) {
#if DATABASE == DB_FILE
	char buf[100];
	char fhwid[50];

	fseek(hwidfile, 0, SEEK_SET);
	while (fgets(buf, sizeof(buf), hwidfile) != NULL ) { // Проходимся циклом по строкам
		getXMLData(buf, "hwid", fhwid, 49);
		if (strcmp(fhwid, hwid) == 0)
		return true;
	}
#elif DATABASE == DB_MYSQL
	char buf[150];
	MYSQL_RES * result;
	bool ret = false;

	sprintf(buf, "SELECT * FROM `bannedhwids` WHERE `hwid`='%s'", hwid);
	mysql_query(mysql, buf);
	result = mysql_store_result(mysql);
	if (mysql_num_rows(result) > 0)           // Если HWID в списке забаненных
		ret = true;
	mysql_free_result(result);
	return ret;
#endif
	return false;
}

/* Проверяет, находится ли HWID в списке HWID'ов игрока player */

bool hasHWIDInBase(char *player, char *hwid) {
#if DATABASE == DB_FILE
	char buf[40];

	snprintf(buf, sizeof(buf), "%s%s_HWID.dat", HWIDS_DIR, player);
	FILE * file = fopen(buf, "r");
	if (file == NULL ) {         // Если вдруг файл удалён
		perror("Opening hwid file");
		return false;
	}
	while (fgets(buf, sizeof(buf), file) != NULL ) {  // Цикл по строкам
		if (strcmp(buf, hwid) == 0) {
			fclose(file);
			return true;
		}
	}
	fclose(file);
#elif DATABASE == DB_MYSQL
	char buf[150];
	MYSQL_RES * result;
	bool ret = false;

	sprintf(buf, "SELECT * FROM `hwids` WHERE `hwid`='%s' AND `login`='%s'",
			hwid, player);
	mysql_query(mysql, buf);
	result = mysql_store_result(mysql);
	if (mysql_num_rows(result) > 0)      // Если HWID есть в базе
		ret = true;
	mysql_free_result(result);
	return ret;
#endif
	return false;
}

/* Добавляет HWID в файл HWID'ов игрока player */

void addHWIDToList(char *player, char *hwid) {
#if DATABASE == DB_FILE
	char buf[75];

	snprintf(buf, sizeof(buf), "%s%s_HWID.dat", HWIDS_DIR, player);
	FILE * file = fopen(buf, "a+");
	sprintf(buf, "%s\n", hwid);
	fputs(hwid, file);
	fclose(file);
#elif DATABASE == DB_MYSQL
	char buf[150];

	sprintf(buf, "INSERT INTO `hwids` VALUES ('%s','%s')", hwid, player);
	mysql_query(mysql, buf);
#endif
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

/* Сравнивает хеш игрока с правильным хешем */

bool cmpHash(char *str) {
	char server[strlen(CLIENT_HASH) + 2], salt[2], nserver[strlen(CLIENT_HASH)];

	strcpy(server, CLIENT_HASH);
	memmove(salt, str, 2);    // Получаем соль из первых двух символов сообщения
	strcut(str, 2, strlen(str) - 2);   // Оставляем в сообщении оставшееся
	strcat(server, salt);              // Добавляем соль к хешу сервера
	hash(server, nserver);             // Хешируем её
	if (strcmp(nserver, str) == 0)     // Сравниваем с сообщением без соли
		return true;
	else
		return false;
}

/* Главная функция обвязки. Работает с сообщением клиента message и записывает результат в result */

void processAnswer(char *result, char *message) {
	char print[4096];

	sprintf(print,
			"=============================================================\nClient send message.\nRaw message: %s\n",
			message);
	if (isXML(message) && hasXMLKey(message, "type")
			&& hasXMLKey(message, "login") && hasXMLKey(message, "password")) { // Если есть необходимые поля...
		int mlen = strlen(message) - 1;
		char type[mlen], login[mlen], password[mlen];
		getXMLData(message, "type", type, mlen - 1);      // ...Получаем их
		getXMLData(message, "login", login, mlen - 1);
		getXMLData(message, "password", password, mlen - 1);
		sprintf(print, "%sType: %s\nLogin: %s\nPassword: %s\n", print, type,
				login, password);
		if (strcmp(type, "auth") == 0) {             // Если тип - авторизация
			if (haveLoginAndPassword(login, password)) { // Если игрок есть есть в базе
				sprintf(result,
						"<response>success</response><version>%d</versions>",
						CLIENT_VERSION);
			} else {
				strcpy(result, "<response>bad login</response>");
			}
		} else if (strcmp(type, "reg") == 0 && hasXMLKey(message, "mail") // Если тип - регистрация
				&& hasXMLKey(message, "hwid")) { // И есть необходимые поля для регистрации
			bool res = true;
			char hwid[mlen], mail[mlen], buf[75];
#if DATABASE == DB_FILE
			char fmail[mlen], flogin[mlen];
#endif

			getXMLData(message, "mail", mail, mlen - 1);      // Получаем данные
			getXMLData(message, "hwid", hwid, mlen - 1);

			sprintf(print, "%sMail: %s\nHWID: %s\n", print, mail, hwid);

			if (isHWIDBanned(hwid)) {                    // Если HWID забанен...
				strcpy(result, "<response>banned</response>"); // ...отправляем сообщение об этом
				res = false;
			} else {
				addHWIDToList(login, hwid);     // Иначе добавляем HWID в список
			}
			if (res) {
#if DATABASE == DB_FILE
				fseek(usersfile, 0, SEEK_SET);
				while (fgets(buf, sizeof(buf), usersfile) != NULL ) { // Ищем игрока с таким логином или почтой в базе
					getXMLData(buf, "login", flogin, mlen - 1);
					getXMLData(buf, "mail", fmail, mlen - 1);
					if (strcmp(login, flogin) == 0
							|| strcmp(mail, fmail) == 0) {
						strcpy(result, "<response>already exists</response>"); // Если есть - отправляем сообщение
						res = false;
						break;
					}
				}
#elif DATABASE == DB_MYSQL
				char buf[150];
				MYSQL_RES * msresult;

				sprintf(buf,
						"SELECT * FROM `users` WHERE `login`='%s' OR `mail`='%s'",
						login, mail);
				mysql_query(mysql, buf);
				msresult = mysql_store_result(mysql);
				if (mysql_num_rows(msresult) > 0) { // Если такой игрок уже есть
					strcpy(result, "<response>already exists</response>");
					res = false;
				}
				mysql_free_result(msresult);
#endif
			}
			if (res) {                     // Если нет ошибок
#if DATABASE == DB_FILE
			sprintf(buf,
					"<login>%s</login><password>%s</password><mail>%s</mail>\n",
					login, password, mail);
			fputs(buf, usersfile);         // Записываем в файл с игроками
#elif DATABASE == DB_MYSQL
				sprintf(buf, "INSERT INTO `users` VALUES ('%s','%s','%s')",
						login, password, mail);
				mysql_query(mysql, buf);      // Отправляем запрос на добавление
#endif
				strcpy(result, "<response>success</response>");
			}
		} else if (strcmp(type, "gameauth") == 0 && hasXMLKey(message, "md5") // Если тип - игровая авторизация
				&& hasXMLKey(message, "hwid")) {           // И есть нужные поля
			char hash[mlen], hwid[mlen];
			bool res = true;

			getXMLData(message, "md5", hash, mlen - 1);       // Получаем данные
			getXMLData(message, "hwid", hwid, mlen - 1);
			sprintf(print, "%sHash: %s\nHWID: %s\n", print, hash, hwid);

			if (!isHWIDBanned(hwid)) {                   // Если HWID не забанен
				if (!hasHWIDInBase(login, hwid)) // И его нет в базе HWID'ов игрока
					addHWIDToList(login, hwid);                     // Добавляем
			} else {
				strcpy(result, "<response>banned</response>"); // Если HWID забанен
				res = false;
			}
			if (!res || haveLoginAndPassword(login, password)) { // Если логин и пароль верные
				if (cmpHash(hash)) {                          // И хеш совпадает
					addPlayer(login);             // Добавляем игрока в вайтлист
				} else {
					strcpy(result, "<response>bad checksum</response>"); // Иначе отправляем сообщение об ошибке
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
	printf(
			"%sResponse: %s\n=============================================================\n",
			print, result);
}

/* Функция, обрабатывающая консольное сообщение пользователя.
 * Возвращаемое значение - нужно ли отправлять сообщение серверу. */

bool processConsoleMessage(char *message) {
	char command[strlen(message)], arg[strlen(message)], buf[150];
#if DATABASE == DB_FILE
	char buf2[100];
#endif

	if (strcmp(message, "stop\n") == 0) {     // Если сообщение - stop
		stopping = true;
		sendMessage("stop\n");
		puts("Waiting for server stopping...");
		sleep(3);
		stop();
		return true;
	}
	if (sscanf(message, "%s %s", command, arg) < 2) // Если команда составлена неверно
		return false;                                 // Выходим

	if (strcmp(command, "banuser") == 0) {            // Если команда - banuser
#if DATABASE == DB_FILE
			snprintf(buf, sizeof(buf), "%s%s_HWID.dat", HWIDS_DIR, arg);
			FILE * file = fopen(buf, "r");
			if (file != NULL ) {
				while (fgets(buf, sizeof(buf), file) != NULL ) { // Баним все HWID'ы из файла пользователя
					printf("Banned HWID %s\n", buf);
					snprintf(buf2, sizeof(buf2),
							"<hwid>%s</hwid><player>%s</player>\n", buf, arg);
					fputs(buf2, hwidfile);
				}
				fclose(file);
			} else {
				puts("No user's HWIDs in database!");
			}
#elif DATABASE == DB_MYSQL
		MYSQL_RES * msres;
		MYSQL_ROW msrow;

		sprintf(buf, "SELECT `hwid` FROM `hwids` WHERE `login`='%s'", arg);
		mysql_query(mysql, buf);
		msres = mysql_store_result(mysql);      // Получаем HWIDы игрока

		while (!mysql_eof(msres)) {            // Проходимся по ним циклом
			msrow = mysql_fetch_row(msres);
			sprintf(buf, "INSERT INTO `bannedhwids` VALUES ('%s','%s')", arg, // Баним HWID
					msrow[0]);
			mysql_query(mysql, buf);
			printf("Banned hwid %s", msrow[0]);
		}
#endif
		return true;
	} else if (strcmp(command, "banhwid") == 0) {      // Если команда - banhwid
#if DATABASE == DB_FILE
			snprintf(buf, sizeof(buf), "<hwid>%s</hwid>\n", arg);
			fputs(buf, hwidfile);        // Записываем его в список забаненных
#elif DATABASE == DB_MYSQL
		sprintf(buf, "INSERT INTO `bannedhwids` VALUES ('__BANNEDUSER__','%s')", //Отправляем запрос на добавление HWID'а с фейковым пользователем
				arg);
		mysql_query(mysql, buf);
#endif
		puts("Banned!");
		return true;
	}
#if DATABASE == DB_MYSQL          // Эти команды работают только если БД - MySQL
	else if (strcmp(command, "unbanuser") == 0) {    // Если команда - unbanuser
		sprintf(buf, "DELETE FROM `bannedhwids` WHERE `login`='%s'", arg);
		mysql_query(mysql, buf);
		puts("Unbanned!");
		return true;
	} else if (strcmp(command, "unbanhwid") == 0) {  // Если команда - unbanhwid
		sprintf(buf, "DELETE FROM `bannedhwids` WHERE `hwid`='%s'", arg);
		mysql_query(mysql, buf);
		puts("Unbanned!");
		return true;
	}
#endif
	return false;
}

/* Функция потока, обрабатывающая подключённого игрока */

void * f00(void *data) {
	char buf[BUFSIZE], answer[70];
	int asock = *(int *) data, nread = 0;

	while ((nread = read(asock, buf, BUFSIZE)) > 0) {     // Читаем сообщение
		processAnswer(answer, buf);                       // Обрабатываем
		write(asock, answer, strlen(answer));             // Посылаем ответ
		shutdown(asock, 0);                              // Закрываем соединение
	}
	return NULL ;
}

/* Функция потока, слушающая вывод сервера */

void *f01(void *data) {
	char buf[BUFSIZE];

	while (1) {
		if(read(serverpipe[READ], buf, BUFSIZE - 1) <= 1) // Получаем сообщения сервера
			continue;
		printf("[SERVER]%s", buf);                   // Выводим
		memset(buf, 0, strlen(buf));                 // Очищаем буфер
	}
	return NULL ;
}

/* Функция потока, управляющая подключениями клиентов */

void *f02(void *data) {
	int x = 0, asock = 0;
	pthread_t threadid[MAXTHREADS], nthreads = 0;

	while (1) {
		if ((asock = accept(lsock, NULL, NULL )) < 0)    // Принимаем подклчение
			puts("accept error");

		if (pthread_create(&threadid[x++], NULL, (void *) &f00, &asock) != 0) // Передаём в поток
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
		// Цикл, уменьшающий время игроков до удаления из вайтлиста
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

#if AUTO_RESTART == TRUE

/* Функция потока, перезапускающего сервер при надобности */

void *f04(void *data) {
	while (1) {
		waitpid(serverpid, NULL, 0);
		if (stopping)
			return NULL ;
		startMCServer();
	}
	return NULL ;
}

#endif

/* Инициализация ВСЕГО */

int main(int argc, char **argv) {
	int on = 1;
	struct sockaddr_in sa;
	pthread_t sockthread = 0, removethread = 0;
	char line[150] = { '\0' };

	signal(SIGINT, exitListener); // Ставим слушатель Ctrl + C на функцию

	puts("Starting server...");

	if (unlink(PATH_TO_WHITELIST) == -1 && errno != ENOENT) {
		puts("[WARNING]Can't remove whitelist file");
	}

#if DATABASE == DB_FILE
	struct stat st = {0};

	if (stat(HWIDS_DIR, &st) == -1) {   // если нет папки с HWID'ами...
		mkdir(HWIDS_DIR, 0700);// ...создаем её
	}

	usersfile = fopen("Base.dat", "a+");           // Открываем
	hwidfile = fopen("BannedHWIDs.dat", "a+");// файлы
	if (usersfile == NULL || hwidfile == NULL ) {  // Если ошибка -
		puts("Error opening file");// Выводим её
		return -1;// и закрываем программу
	}
#elif DATABASE == DB_MYSQL
	mysql = mysql_init(NULL );
	if (mysql == NULL
			|| !mysql_real_connect(mysql, MYSQL_HOST, MYSQL_USER, MYSQL_PASS,
					MYSQL_DB, MYSQL_PORT, NULL, 0)) {
		fprintf(stderr, "Error connecting MySQL: %s\n", mysql_error(mysql));
		return -1;
	}
	mysql_query(mysql,
			"CREATE TABLE IF NOT EXISTS bannedhwids (login TEXT(20), hwid TEXT(20))");
	mysql_query(mysql,
			"CREATE TABLE IF NOT EXISTS hwids (login TEXT(20), hwid TEXT(20))");
	mysql_query(mysql,
			"CREATE TABLE IF NOT EXISTS users (login TEXT(20), password TEXT(40), mail TEXT(80))");
#endif

	puts("Launching minecraft server...");
	startMCServer();

	puts("Creating socket...");
	if ((lsock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {      // Запускаем сокет
		puts("Socket creating error");
		stop();
	}

	if (setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) { // Устанавливаем параметры сокета
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

	puts("Done!\nWaiting for connections...");

	if (pthread_create(&sockthread, NULL, (void *) &f02, NULL ) != 0) { // Запускаем поток, управляющий подключениями клиентов
		puts("thread creating error");
		stop();
	}

	if (pthread_create(&removethread, NULL, (void *) &f03, NULL ) != 0) { // Запускаем поток, удаляющий игроков из вайтлиста
		puts("thread creating error");
		stop();
	}

#if AUTO_RESTART == TRUE
	if (pthread_create(&restartthread, NULL, (void *) &f04, NULL ) != 0) { // Запускаем поток, проверяющий состояние сервера
		puts("thread creating error");
		stop();
	}
#endif

	while (fgets(line, sizeof(line), stdin) != NULL ) { // Слушаем пользовательский ввод
		if (!processConsoleMessage(line))       // Если сообщение - не системное
			sendMessage(line);                              // Отправляем его
	}

	stop();
	return 0;
}

/* ТЕСТОВЫЕ СООБЩЕНИЯ
 <type>auth</type><login>test</login><password>A94A8FE5CCB19BA61C4C0873D391E987982FBBD3</password>
 <type>reg</type><login>test1</login><password>A94A8FE5CCB19BA61C4C0873D391E987982FBBD3</password><mail>test1@test.com</mail><hwid>test1</hwid>
 <type>gameauth</type><login>test</login><password>A94A8FE5CCB19BA61C4C0873D391E987982FBBD3</password><md5>a036b3be41c6becd1c859c7e58e4da3d85</md5><hwid>test</hwid>
 */
