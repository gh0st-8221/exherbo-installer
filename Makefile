all: exherbo-installer

build: exherbo-installer

exherbo-installer: exherbo-installer.c
	$(CC) $(CFLAGS) exherbo-installer.c -o exherbo-installer $(LDFLAGS)

clean:
	rm -f exherbo-installer
