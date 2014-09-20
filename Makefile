ldapperf: ldapperf.c
	gcc -Wall -lldap -lpthread -g -o $@ $<

tar: clean
	tar -cvf ldapperf.tar *

clean:
	rm ldapperf
