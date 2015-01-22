/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

 /*
 * $Id: e3752d5f19fb4a46be758c4475ba71291537a28d $
 *
 * @file ldapperf.c
 * @verision 0.1
 *
 * @brief Simple LDAP performance tool.
 * @author Arran Cudbard-Bell <a.cudbardb@freeradius.org>
 *
 * @copyright 2014 Arran Cudbard-Bell <a.cudbardb@freeradius.org>
 * @copyright 2014 Network RADIUS SARL
 *
 * @note This is based on ldapbench 0.2 by Geerd-Dietger Hoffman, which was also released under GPLv2.
 *	 ldapbench is poorly documented and pretty buggy though, so I don't recommend you use it.
 */

/* Standard system headers */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <assert.h>

#include <sys/time.h>

/* OpenLDAP or other RFC compliant library */
#define LDAP_DEPRECATED 0
#include <ldap.h>

/* Every system should have pthreads */
#include <pthread.h>

typedef struct {
	uint64_t	successful;				//!< Successful queries.
	uint64_t	error_session_init;			//!< How many times we failed session init.
	uint64_t	error_bind_fail;			//!< How many times we failed to bind.
	uint64_t	error_search_fail;			//!< How many times we got errors when searching.
	struct timeval	before;					//!< Time the test started.
	struct timeval	after;					//!< Time the test stopped.
} lp_stats_t;

typedef struct lp_thread {
	int		number;					//!< The thread number.
	pthread_t	handle;					//!< pthread handle.

	char		*base_dn;				//!< Dynamic base DN.
	size_t		base_dn_len;				//!< Length of buffer alloced for base DN.

	char		*filter;				//!< Dynamic filter.
	size_t		filter_len;				//!< Length of buffer alloced for filter len.

	lp_stats_t	stats;					//!< Stats generated by the thread.
} lp_thread_t;

typedef struct lp_name {
	size_t		len;					//!< Length of the name field (avoid strlen).
	char 		*name;					//!< Name string.
} lp_name_t;

#define VTC_RED		"\x1b[31m"
#define VTC_BOLD	"\x1b[1m"
#define VTC_RESET	"\x1b[0m"

#define USEC 1000000
#define TIMEVAL_TO_FLOAT(_x) ((_x).tv_sec + ((_x).tv_usec / 1000000.0))

#define DEBUG(fmt, ...) do { if (debug > 0) printf(fmt "\n", ## __VA_ARGS__); fflush(stdout); } while (0)
#define DEBUG2(fmt, ...) do { if (debug > 1) printf(fmt "\n", ## __VA_ARGS__); fflush(stdout); } while (0)
#define INFO(fmt, ...) do { if (debug >= 0) printf(VTC_BOLD fmt "\n" VTC_RESET, ## __VA_ARGS__); fflush(stdout); } while (0)
#define ERROR(fmt, ...) do { fprintf(stderr, VTC_RED "ERROR: " fmt "\n" VTC_RESET, ## __VA_ARGS__); fflush(stderr); } while (0)

#define TDEBUG(fmt, ...) do { if (debug > 0) printf("(%03i) " fmt "\n", thread->number, ## __VA_ARGS__); fflush(stdout); } while (0)
#define TINFO(fmt, ...) do { if (debug >= 0) printf(VTC_BOLD "(%03i) " fmt "\n" VTC_RESET, thread->number, ## __VA_ARGS__); fflush(stdout); } while (0)
#define TERROR(fmt, ...) do { fprintf(stderr, VTC_RED "(%03i) ERROR: " fmt "\n" VTC_RESET, thread->number, ## __VA_ARGS__); fflush(stderr); } while (0)

#ifndef SUBST_CHAR
#  define SUBST_CHAR '@'					//!< The char to substitute in the filter and/or DN.
#endif

int debug		= 0;					//!< Default configuration options.
bool decode_entry	= false;				//!< Do a scan of the results return through the ldap server. This is client side !
bool rebind		= false;				//!< Rebind after every search.
bool do_stats		= false;				//!< Print statistics at the end of the test.
bool ordered		= false;				//!< Perform searches for names from -r <file> in order to prime the cache.

char const *ldap_uri	= "ldap://127.0.0.1";			//!< The host where the ldap server resides.
char const *bind_dn	= NULL;					//!< Manager bind DN.
char const *password	= NULL;					//!< And the password.

char const *base_dn	= NULL;					//!< Where to search.
size_t base_dn_len	= 0;					//!< Length of base DN.

char const *filter	= NULL;					//!< What to search for.
size_t filter_len	= 0;					//!< Length of filter.

bool do_subst		= false;				//!< Whether we're going to perform substitution on the
								//!< base DN or filter.

int scope		= LDAP_SCOPE_ONE;			//!< Scope of the search,
int num_loops		= 10;					//!< How many searches to execute.
int num_pthreads	= 5;					//!< How many threads there should be.
char *names_file        = NULL;					//!< File containing names to substitute.
struct timeval timeout = {
	.tv_sec = 10 						//!< 10 second connection/search time_out,
};
int version		= LDAP_VERSION3;

lp_name_t *names	= NULL;
int names_cnt		= 0;

void usage(char const *path, int code)
{
	char const *prog;

	prog = strrchr(path, '/');
	prog = prog ? prog + 1: path;

	printf("%s -b <base_dn> [options]\n", prog);
	printf("  -v             Output more debugging information. Use multiple times to increase verbosity\n");
	printf("  -s             Search scope, one of (one, sub, "
	/* Children is specific to OpenLDAP */
#ifdef LDAP_SCOPE_CHILDREN
	       "children, "
#endif
	       "base)\n");
	printf("  -S             Print statistics after all queries have completed\n");
	printf("  -H <uri>       Host to connect to (default ldap://127.0.0.1)\n");
	printf("  -o <ordered>   Search for each of the names in the -r <file> in order, using a single thread\n");
	printf("  -d             Decode received entry (default no)\n");
	printf("  -D <dn>        Bind DN\n");
	printf("  -w <pasword>   Bind password\n");
	printf("\nSearch options:\n");
	printf("  -b <base_dn>   DN to start the search from ('%c' will be replaced with "
	       "a name from -r <file>)\n", SUBST_CHAR);
	printf("  -f <filter>    Filter to use when searching ('%c' will be replaced with "
	       "a name from -r <file>)\n", SUBST_CHAR);
	printf("  -l <loops>     How many searches a thread should perform\n");
	printf("  -t <threads>   How many threads we should spawn\n");
	printf("  -q             Produce less verbose output\n");
	printf("  -r <file>      List of names to use when searching\n");
	printf("  -R             Rebind after every search operation (default no)\n");
	printf("\nExample:\n");
	printf("  %s -H ldap://127.0.0.1 -D \"cn=manager,dc=example,dc=org\" -w \"letmein\" "
	       "-b \"dc=example,dc=org\" -s\n", prog);
	printf("\nlibldap vendor: %s, version: %i.%i.%i\n", LDAP_VENDOR_NAME,
	       LDAP_VENDOR_VERSION_MAJOR, LDAP_VENDOR_VERSION_MINOR, LDAP_VENDOR_VERSION_PATCH);

	exit(code);
}

/** Subtract one timeval struct from another
 *
 * @param[in] after Timestamp to subtract from.
 * @param[in] before Timestamp to subtract.
 * @param[out] time subtracting before from after.
 */
static void lp_timeval_sub(struct timeval const *after, struct timeval const *before, struct timeval *elapsed)
{
	elapsed->tv_sec = after->tv_sec - before->tv_sec;
	if (elapsed->tv_sec > 0) {
		elapsed->tv_sec--;
		elapsed->tv_usec = USEC;
	} else {
		elapsed->tv_usec = 0;
	}
	elapsed->tv_usec += after->tv_usec;
	elapsed->tv_usec -= before->tv_usec;

	if (elapsed->tv_usec >= USEC) {
		elapsed->tv_usec -= USEC;
		elapsed->tv_sec++;
	}
}

/** Convert an LDAP_SCOPE_* macro back to a string.
 *
 * @param[in] scope to convert.
 * @return scope string.
 */
static char const *lp_scope_str(int scope)
{
	switch (scope) {
	case LDAP_SCOPE_ONE:
		return "one";

	case LDAP_SCOPE_SUB:
		return "sub";

#ifdef LDAP_SCOPE_CHILDREN
	case LDAP_SCOPE_CHILDREN:
		return "children";
#endif
	case LDAP_SCOPE_BASE:
		return "base";

	default:
		assert(0);
	}
}

/** Read lines from a file into an array of strings and lengths
 *
 * @param[out] out array of lp_name_t structs, must be freed after use.
 * @param[in] path of file to read.
 * @return the number of entries read, or < 0 on error.
 */
static int lp_names_file(lp_name_t **out, char const *path)
{
	FILE *file;
	char buffer[1024];
	char *p;

	size_t max = 0;
	int idx = 0, cnt = 0;

	lp_name_t *names = NULL;

	file = fopen(path, "r");
	if (!file) {
		ERROR("Failed opening name file \"%s\": %s", path, strerror(errno));
		return -1;
	}

	DEBUG("Reading names from \"%s\"", path);

	while (fgets(buffer, sizeof(buffer), file)) {
		size_t len = strlen(buffer);

		if ((len == 0) || (--len == 0)) continue;
		p = malloc(len + 1);
		memcpy(p, buffer, len);
		p[len] = '\0';

		if (idx == cnt) {
			cnt += 1000;
			names = realloc(names, sizeof(lp_name_t) * (cnt+1));
		}

		DEBUG2("[%i] %s", idx, p);
		names[idx].len = len;
		names[idx].name = p;

		if (len > max) max = len;

		idx++;
	}

	names[idx].name = NULL;
	names[idx].len = max;

	*out = names;

	fclose(file);

	return idx;
}

/** Frees an array of lp_name_t structs.
 *
 * @param[in] names to free.
 */
static void lp_names_free(lp_name_t *names)
{
	lp_name_t *p = names;

	if (!names) return;

	while (p->name) {
		free(p->name);
		p++;
	};

	free(names);
}

/** Replaces to_find in in with subst.
 *
 * @param[in] out Buffer to write a copy of in with to_find substituted to subst.
 * @param[in] outlen length of output buffer.
 * @param[in] in String to perform substitutions on.
 * @param[in] subst String to substitute.
 * @
 * @param[in] names to free.
 */
static char const *lp_strpst(char *out, size_t outlen, char const *in, size_t inlen,
			     char const *subst, size_t subst_len, char to_find)
{
	char *p, *out_p;
	size_t start_len, end_len;

	assert((inlen + subst_len) <= outlen);

	p = strchr(in, to_find);
	if (!p) return in;

	start_len = p - in;

	out_p = out;
	memcpy(out_p, in, start_len);
	out_p += start_len;

	memcpy(out_p, subst, subst_len);
	out_p += subst_len;

	end_len = inlen - (start_len + 1);
	memcpy(out_p, p + 1, end_len);

	out_p += end_len;
	*out_p = '\0';

	return out;
}

static LDAP *lp_conn_init(lp_thread_t *thread)
{
	LDAP *ld;
	int rc;

	/* Initialize the LDAP session */
	rc = ldap_initialize(&ld, ldap_uri);
	if (rc != LDAP_SUCCESS) {
		TERROR("LDAP session initialization failed: %s", ldap_err2string(rc));
		thread->stats.error_session_init++;
		return NULL;
	}

	/* Set LDAP version to 3 and set connection time_out (in sec).*/
	ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
	ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &timeout.tv_sec);

	TDEBUG("LDAP session initialised");

	/* Bind to the server */
	if (bind_dn && password) {
		struct berval cred;
		cred.bv_val = (char *)password;
		cred.bv_len = strlen(password);

		rc = ldap_sasl_bind_s(ld, bind_dn, LDAP_SASL_SIMPLE, &cred, NULL, NULL, NULL);
		if (rc != LDAP_SUCCESS){
			TERROR("ldap_sasl_bind_s: %s", ldap_err2string(rc));
			thread->stats.error_bind_fail++;
			ldap_unbind_ext(ld, NULL, NULL);
			return NULL;
		}

		TDEBUG("Bind successful");
	}

	return ld;
}

static void lp_conn_close(LDAP **ld)
{
	if (!*ld) return;

	ldap_unbind_ext_s(*ld, NULL, NULL);
	*ld = NULL;
}

static int lp_query_perform(lp_thread_t *thread, LDAP *ld, lp_name_t *subst)
{
	int		i = 0, rc = 0, entry_count = 0;
	char		*attribute, *dn;

	LDAPMessage	*search_result = NULL, *entry;
	char const	*filter_p = filter;
	char const	*base_dn_p = base_dn;

	assert(thread && ld);

	/* Perform substitutions */
	if (subst) {
		if (filter) {
			filter_p = lp_strpst(thread->filter, thread->filter_len,
					     filter, filter_len,
					     subst->name, subst->len, SUBST_CHAR);
		}

		base_dn_p = lp_strpst(thread->base_dn, thread->base_dn_len,
				      base_dn, base_dn_len,
				      subst->name, subst->len, SUBST_CHAR);
	}

	TDEBUG("Searching in \"%s\" filter \"%s\" scope \"%s\"",
	       base_dn_p, filter_p ? filter_p : "none", lp_scope_str(scope));

	/* Search the directory */
	rc = ldap_search_ext_s(ld, base_dn_p, scope, filter_p, NULL, 0, NULL, NULL, &timeout, 0, &search_result);
	if (rc != LDAP_SUCCESS){
		TERROR("ldap_search_ext_s: %s", ldap_err2string(rc));
		thread->stats.error_search_fail++;

		ldap_msgfree(search_result);
		return -1;
	}

	entry_count = ldap_count_entries(ld, search_result);
	TDEBUG("Search completed successfully. Got %d entries", entry_count);

	/* Save client CPU time by not decoding the result */
	if (entry_count && decode_entry) {
		/* Go through the search results by checking entries */
		for (entry = ldap_first_entry(ld, search_result);
		     entry;
		     entry = ldap_next_entry(ld, entry)) {
		     	BerElement *ber = NULL;

			if ((dn = ldap_get_dn(ld, entry)) != NULL ){
				TDEBUG("Decoding object with dn: %s", dn);
				ldap_memfree(dn);
			}

			for (attribute = ldap_first_attribute(ld, entry, &ber);
			     attribute;
			     attribute = ldap_next_attribute(ld, entry, ber)) {
				int		count;
				struct berval	**values;

				values = ldap_get_values_len(ld, entry, attribute);
				if (!values) goto next;

				count = ldap_count_values_len(values);
				if (!count) goto next;

				/* Get values and print.  Assumes all values are strings. */
				for (i = 0; i < count; i++) {
					TDEBUG("\t%s: %.*s", attribute, (int)values[i]->bv_len,
					       values[i]->bv_val);
				}

			next:
				ldap_value_free_len(values);
				ldap_memfree(attribute);
			}
			ber_free(ber, 0);
		}
	}

	ldap_msgfree(search_result);

	thread->stats.successful++;

	return 0;
}

static void *enter_thread(void *arg)
{
	int i = 0;
	LDAP *ld = NULL;

	lp_thread_t *thread = arg;
	struct timeval elapsed;

	TDEBUG("Starting new thread with %d searches", num_loops);

	gettimeofday(&thread->stats.before, NULL);

	for (i = 0; i < num_loops; i++) {
		lp_name_t *subst = NULL;

		if (!ld) ld = lp_conn_init(thread);
		if (!ld) continue;

		/* Replace the SUBST_CHAR in filter with random string out of names_file */
		if (do_subst) subst = ordered ? &names[i] :
						&names[rand() % names_cnt];

		if (lp_query_perform(thread, ld, subst) < 0) {
			lp_conn_close(&ld);
			continue;
		}

		/* Rebind after every search */
		if (rebind) lp_conn_close(&ld);
	}

	/* Ensure we don't leak connections */
	lp_conn_close(&ld);

	gettimeofday(&thread->stats.after, NULL);
	lp_timeval_sub(&thread->stats.after, &thread->stats.before, &elapsed);
	TDEBUG("Thread exiting after: %lfs", TIMEVAL_TO_FLOAT(elapsed));

	return NULL;
}

static void lp_print_stats(lp_stats_t *stats)
{
	struct timeval elapsed;
	double seconds;

	lp_timeval_sub(&stats->after, &stats->before, &elapsed);
	seconds = TIMEVAL_TO_FLOAT(elapsed);

	/* Condensed stats output for scripting */
	if (debug < 0) {
		printf("time,success,success_s,search_fail,init_fail,bind_fail\n");
		printf("%i,"
		       "%" PRIu64 ","
		       "%i,"
		       "%" PRIu64 ","
		       "%" PRIu64 ","
		       "%" PRIu64 "\n",
		       (int) seconds,
		       stats->successful,
		       (int) (stats->successful / seconds),
		       stats->error_search_fail,
		       stats->error_session_init,
		       stats->error_bind_fail);
		return;
	}

	/* Pretty stats output */
	INFO("Statistics:");
	INFO("\tTotal time (seconds)  : %lf", seconds);
	INFO("\tSuccessful searches   : %" PRIu64, stats->successful);
	INFO("\tSuccessful searches/s : %lf", stats->successful / seconds);
	INFO("\tSearch failures       : %" PRIu64, stats->error_search_fail);
	INFO("\tSession init errors   : %" PRIu64, stats->error_session_init);
	INFO("\tBind failures         : %" PRIu64, stats->error_bind_fail);

	return;
}

int main(int argc, char **argv)
{
	int			i = 0;
	lp_thread_t		*threads;
	int			c = 0;
	extern char		*optarg;
	extern int		optind, optopt, opterr;

	static lp_stats_t	stats;

	while ((c = getopt(argc, argv, "H:ovs:SdD:w:b:f:l:t:hqr:R")) != -1) switch(c) {
	case 'H':
		if (!((strncmp(optarg, "ldap://", 7) == 0) || (strncmp(optarg, "ldaps://", 8) == 0))) {
			ERROR("Host must be specified with an LDAP URI e.g. ldap://127.0.0.1:384");
			exit(1);
		}
		ldap_uri = optarg;
		break;

	case 'o':
		ordered = true;
		break;

	case 'v':
		debug++;
		break;

	case 's':
		if (strcmp(optarg, "one") == 0) {
			scope = LDAP_SCOPE_ONE;
		}
		else if (strcmp(optarg, "sub") == 0) {
			scope = LDAP_SCOPE_SUB;
		}
#ifdef LDAP_SCOPE_CHILDREN
		else if (strcmp(optarg, "children") == 0) {
			scope = LDAP_SCOPE_CHILDREN;
		}
#endif
		else if (strcmp(optarg, "base") == 0) {
			scope = LDAP_SCOPE_BASE;
		}
		else {
			ERROR("Invalid scope \"%s\", must be one of 'one', 'sub', 'base' or 'children'", optarg);
			exit(1);
		}
		break;

	case 'S':
		do_stats = true;
		break;

	case 'd':
		decode_entry = true;
		break;

	case 'D':
		bind_dn = optarg;
		break;

	case 'w':
		password = optarg;
		break;

	case 'b':
		base_dn = optarg;
		break;

	case 'f':
		filter = optarg;
		break;

	case 'l':
		num_loops = atoi(optarg);
		break;

	case 't':
		num_pthreads = atoi(optarg);
		break;

	case 'q':
		debug--;
		break;

	case 'r':
		names_file = optarg;
		do_subst = true;
		break;

	case 'R':
		rebind = true;
		break;

	case 'h':
	case '?':
		usage(argv[0], 0);

	default:
		assert(0);
	}

	if (!base_dn) {
		ERROR("No Base DN provided, use -b <base_dn>");
		usage(argv[0], 64);
	}

	if (ordered && !do_subst) {
		ERROR("List of names needed to perform ordered search");
		usage(argv[0], 64);
	}

	if (do_subst && !strchr(base_dn, SUBST_CHAR) && (!filter || !strchr(filter, SUBST_CHAR))) {
		ERROR("No substitution chars (%c) found in filter or base DN", SUBST_CHAR);
		usage(argv[0], 64);
	}

	/* Alloc memory for each of the threads */
	threads = malloc(sizeof(lp_thread_t) * num_pthreads);
	memset(threads, 0, sizeof(lp_thread_t) * num_pthreads);

	/* Allocate buffers so we don't have to do it during the test */
	if (do_subst) {
		if (names_file) {
			names_cnt = lp_names_file(&names, names_file);
			if (names_cnt < 0) exit(1);
		}

		if (base_dn) base_dn_len = strlen(base_dn);
		if (filter) filter_len = strlen(filter);

		for (i = 0; i < num_pthreads; i++) {
			size_t len;

			/* Last element of names contains length of longest element */
			len = base_dn_len + names[names_cnt].len;
			threads[i].base_dn_len = len;
			threads[i].base_dn = malloc(len);

			/* Last element of names contains length of longest element */
			len = filter_len + names[names_cnt].len;
			threads[i].filter_len = len;
			threads[i].filter = malloc(len);
		}
	}

	/* Override the defaults for threads and loops if were doing an ordered search */
	if (ordered) {
		num_pthreads = 1;
		num_loops = names_cnt;
	}

	INFO("Performing %i search(es) total, with %i threads, %s",
	     (num_loops * num_pthreads), num_pthreads,
	     rebind ? "rebinding after each search" : "with persistent connections");

	/* Work around initialisation race in libldap */
	{
		LDAP *ld;
		ldap_initialize(&ld, "");
	}

	gettimeofday(&stats.before, NULL);
	for (i = 0; i < num_pthreads; i++) {
		threads[i].number = i;
		if (pthread_create(&threads[i].handle, NULL, &enter_thread, &threads[i])) {
			ERROR("Error creating a new thread");
			exit(1);
		}
	}

	DEBUG("Waiting for threads to finish...");
	for (i = 0; i < num_pthreads; i++) {
		pthread_join(threads[i].handle, NULL);

		/* Add up stats from all the threads */
		stats.successful		+= threads[i].stats.successful;
		stats.error_session_init	+= threads[i].stats.error_session_init;
		stats.error_bind_fail		+= threads[i].stats.error_bind_fail;
		stats.error_search_fail		+= threads[i].stats.error_search_fail;

		free(threads[i].base_dn);
		free(threads[i].filter);
	}
	gettimeofday(&stats.after, NULL);
	DEBUG("... All threads done");

	if (do_stats) lp_print_stats(&stats);

	lp_names_free(names);

	/* Any errors means we exit with a none zero code */
	if ((stats.error_session_init > 0) ||
	    (stats.error_bind_fail > 0) ||
	    (stats.error_search_fail > 0)) {
		exit(1);
	}

	exit(0);
}
