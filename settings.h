#include "hash.h"

/* НАСТРОЙКИ */
#define SERVER_PORT 65533                     //Порт, на котором запущен сервер
#define MAXTHREADS 15                         //Максимальное количество одновременно обрабатываемых игроков
#define CLIENT_VERSION 0                      //Версия клиента
#define CLIENT_HASH "58e8c6b9374e0d4ff71df7ba3ba136cc"    //Хеш клиента
#define LAUNCH_STRING "cd server && java -Xms512M -Xmx512M -jar craftbukkit.jar"    //Строка, запускающая сервер. Обязателен переход в папку сервера.
#define PATH_TO_WHITELIST "server/white-list.txt"
#define TIME_TO_ENTER 90                     //Время на вход в игру (в секундах)
#define HWIDS_DIR "PlayersHWIDs/"            //Папка с HWID'ами игроков. Не менять без необходимости.
#define HASH_ALGO HASH_MD5                   //Алгоритм хеширования
