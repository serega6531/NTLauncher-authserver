authserver:
	@echo 'Building auth server...'
	$(CC) -O3 -o "NTLauncher-authserver" -w -L/usr/lib/mysql -I/usr/include/mysql -lpthread -pthread -lm -lmysqlclient -lcrypto main.c hash.c
	@echo 'Done!'
