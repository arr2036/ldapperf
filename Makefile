all:
	gcc -Wall -lldap -lpthread -g -o ldapperf ldapperf.c

tar: clean
	tar -cvf ldapperf.tar *

clean:
	rm ldapperf
