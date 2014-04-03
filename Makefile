authserver:
	@echo 'Building auth server...'
	$(CC) -O3 -o "NTLauncher-authserver" -w -lpthread -pthread main.c
	@echo 'Done!'