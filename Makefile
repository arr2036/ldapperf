ldapperf: ldapperf.c
	cc -Wall -o $@ $< -lldap -lpthread -llber 

tar: clean
	tar -cvf ldapperf.tar *

clean:
	rm -rf *.dSYM
	rm ldapperf
