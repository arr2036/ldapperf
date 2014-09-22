ldapperf: ldapperf.c
	gcc -Wall -lldap -lpthread -llber -g -o $@ $<

tar: clean
	tar -cvf ldapperf.tar *

clean:
	rm -rf *.dSYM
	rm ldapperf
