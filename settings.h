#include "hash.h"

#define DB_FILE 0x1
#define DB_MYSQL 0x2
#define FALSE 0
#define TRUE 1

/* НАСТРОЙКИ */
#define SERVER_PORT 65533                     //Порт, на котором запущен сервер
#define MAXTHREADS 15                         //Максимальное количество одновременно обрабатываемых игроков
#define CLIENT_VERSION 0                      //Версия клиента
#define CLIENT_HASH "58e8c6b9374e0d4ff71df7ba3ba136cc"    //Хеш клиента
#define JAVA_PATH "/usr/bin/java"   //Путь до Java
static char* const LAUNCH_ARGS[] = {"java", "-Xms512M", "-Xmx512M", "-jar", "craftbukkit.jar"};    //Массив с параметрами запуска
#define PATH_TO_WHITELIST "server/white-list.txt"
#define TIME_TO_ENTER 90                     //Время на вход в игру (в секундах)
#define HASH_ALGO HASH_MD5                   //Алгоритм хеширования
#define DATABASE DB_MYSQL

#define AUTO_RESTART TRUE

#if DATABASE == DB_FILE
#define HWIDS_DIR "PlayersHWIDs/"            //Папка с HWID'ами игроков. Не менять без необходимости.
#endif

#if DATABASE == DB_MYSQL

/* Настройки MYSQL */
#define MYSQL_HOST "127.0.0.1"
#define MYSQL_USER "authserver"
#define MYSQL_PASS "authserverpass"
#define MYSQL_PORT 3306
#define MYSQL_DB "authserver"

#endif
