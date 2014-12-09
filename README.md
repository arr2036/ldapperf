ldapperf
========

Very simple, threaded, LDAP directory performance checker/benchmark.

This should compile on any POSIX-2001 system, which has pthreads and OpenLDAP libldap.

```text
ldapperf -b <base_dn> [options]
  -v             Output more debugging information. Use multiple times to increase verbosity
  -s             Search scope, one of (one, sub, children, base)
  -S             Print statistics after all queries have completed
  -H <host>      Host to connect to (default 127.0.0.1)
  -p <port>      Port to connect on (default 389)
  -o <ordered>   Search for each of the names in the -r <file> in order, using a single thread
  -d             Decode received entry (default no)
  -D <dn>        Bind DN
  -w <pasword>   Bind password

Search options:
  -b <base_dn>   DN to start the search from ('@' will be replaced with a name from -r <file>)
  -f <filter>    Filter to use when searching ('@' will be replaced with a name from -r <file>)
  -l <loops>     How many searches a thread should perform
  -t <threads>   How many threads we should spawn
  -q             Produce less verbose output
  -r <file>      List of names to use when searching
  -R             Rebind after every search operation (default no)

Example:
  ldapperf -H 127.0.0.1 -p 389 -D "cn=manager,dc=example,dc=org" -w "letmein" -b "dc=example,dc=org" -s
```

Modes
-----

### Substitution mode


If an ``-r <file>`` is passed, all names from this file will be read into memory. 
For each search a different name will be selected from from the list.

The ``-b <base_dn>`` and ``-f <filter>`` (if set) strings are scanned for a 
``REPLACE_CHAR`` by default ``@``, and this is replaced with the randomly picked
name.

This is useful for checking random access to objects within the directory.

### Ordered substitution mode

Some LDAP backends or directories have multiple levels of caching between the on
disk representation of the objects and the LDAP encoder. If you want to test the
server will all caches partially or fully filled you can pass the ``-o`` flag to
set ordered mode.

``-o`` must be used with ``-r <file>``.

In ordered mode, searches will be performed for each name in the file
sequentially. ``-l <loops>`` and ``-t <threads>`` will be overidden, with loops 
being set to the number ofnames in the file and threads being set to one.

### No substitution mode


If you want to check the performance of the query cache don't pass ``-r <file>``
and set ``-b <base_dn>`` to the DN of an object, and pass ``-s base``.

The object will then be retrieved ``-l <loops>`` * ``-t <threads>`` times.

Rebinding
---------

If you'd like to check the performance of things such as server side scripts 
which may open and bind on every search, the rebind ``-R`` flag may be passed.

For each search the server will then open the connection, bind (optionally) and 
close the connection.

Rebinding may introduce significant latency because of the addition of the TCP
connection setup, and the bind operation.

Decoding
--------

By default the responses will not be decoded. If you'd like to do this 
(to check for validity, or the presence of attributes), the ``-d`` flag may
be passed.

In debugging mode (``-v``) the DN of the object and its attributes will be 
output.

Stats
-----

Once all the threads have completed their searches, the statistics are gathered
by the main program, and may optionally be displayed using the ``-S`` flag.

At normal verbosity level the output produced will look something like:
```text
Statistics:
  Total time (seconds)  : 1.413752
  Successful searches   : 10
  Successful searches/s : 7.073376
  Search failures       : 0
  Session init errors   : 0
  Bind failures         : 0
```

At quiet (``-q``) verbosity level a more compact, script friendly (floats cast 
to integers, easy to parse), output will be produced:
```text
time,success,success_s,search_fail,init_fail,bind_fail
1,10,7,0,0,0
```

Contributing
------------

Pull requests welcome, so long as they're pretty and useful.

Author
------

Written by Arran Cudbard-Bell of the FreeRADIUS project.

Based on ldapbench 0.2 by Geerd-Dietger Hoffman, though they now share little
or no code.
