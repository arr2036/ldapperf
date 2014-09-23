#!/bin/sh

CONNS_MIN=1             # How many connections we start with
CONNS_MAX=100           # Maximum number of connections to test with
CONNS_INCRM=5           # How much we increase the number of connections each time
AVERAGE=3               # How many runs to average the results over
RETRY=3                 # Number of times we retry on error
OUTPUT='term'           # gnuplot output file
TERM_TYPE='dumb'

if which gnuplot 2>&1 > /dev/null; then
    HAVE_GNUPLOT=true
else
    HAVE_GNUPLOT=false
fi

export PATH="./:$PATH"

ERROR() {
    >&2 echo "ERROR: "$@
}

INFO() {
    echo $@
}

# Prints help
usage() {
    echo "Run with ldapperf increasing numbers of connections and output/plot the results"
    echo $(basename $0)" [options] -- <ldapperf options>"
    echo "Options:"
    echo "  -r <runs>        How many runs to average the results over"
    echo "  -R <retry>       How many times to retry on error before giving up (default 3)"
    echo "  -s <conns>       Connections start (default 1)"
    echo "  -S <conns>       Connections max (default 100)"
    echo "  -i <conns>       How much to increase number of connections by each time (default 5)"
    echo "  -o <file>        Output graph to .svg file"
    echo "  -h               This help text"
    exit $1
}

is_positive_int() {
    if [ "$OPTARG" -lt 0 ]; then
        ERROR "$1 must be a positive integer"
        usage 64
    fi
}

while getopts "+s:S:i:r:R:o:" OPT; do
    case "$OPT" in
        o)
            if ! $HAVE_GNUPLOT; then
                ERROR "gnuplot not found in path. Needed to produce graphs"
                exit 64
            fi

            OUTPUT="$OPTARG"
            TERM_TYPE='png'
            ;;

        r)
            is_positive_int "-r <runs>"
            AVERAGE="$OPTARG"
            ;;

        R)
            is_positive_int "-R <retry>"
            RETRY="$OPTARG"
            ;;

        s)
            is_positive_int "-s <conns>"
            CONNS_MIN="$OPTARG"

            ;;

        S)
            is_positive_int "-S <conns>"
            CONNS_MAX="$OPTARG"

            ;;

        i)
            is_positive_int "-i <conns>"
            CONNS_INCRM="$OPTARG"
            ;;

        h)
            usage 0
            ;;

        --)
            break;
            ;;

        *)
            usage 64
            ;;
    esac
done
shift $((OPTIND-1))

if ! eval "ldapperf $@ -t 1 -l 1 -q"; then
    ERROR "Invalid arguments provided for ldapperf"
    exit $?
fi

if $HAVE_GNUPLOT; then
    tmp=$(mktemp /tmp/$0_XXX);
    if [ $? -ne 0 ]; then
        ERROR "Failed creating temporary file for gnuplot"
        exit 1;
    fi
fi

for t in $(seq $CONNS_MIN $CONNS_INCRM $CONNS_MAX); do
    avg=0
    min=0
    max=0

    INFO "Testing with ${t} connection(s):"
    for p in $(seq 1 $AVERAGE); do
        printf "  Run ${p}/${AVERAGE}: "
        for r in $(seq 0 $RETRY); do
            stats=$(eval "ldapperf $@ -t $t -q -S")
            if [ $? -ne 0 ]; then
                if [ $r -lt $RETRY ]; then
                    ERROR "Run failed, retrying ($(expr ${r} + 1)/${RETRY})"
                    continue
                else
                    ERROR "Hit maximum number of retries, exiting..."
                    if [ ! -z "$OUTPUT" ]; then "$tmp"; fi
                    exit 1
                fi
            fi
            tps=$(echo "$stats"  | grep '^[0-9,]*$' | cut -d ',' -f 3)
            avg=$(expr $avg + $tps)

            if [ $min -eq 0 ]; then min=$tps; fi
            if [ $tps -lt $min ]; then min=$tps; fi

            if [ $tps -gt $max ]; then max=$tps; fi
            break;
        done
        printf "$tps\n"
    done
    avg=$(expr $avg / ${AVERAGE})
    INFO "Min(tps): ${min}, Max(tps): ${max}, Avg(tps): ${avg}"
    INFO
    if $HAVE_GNUPLOT; then echo "${t} ${avg} ${min} ${max}" >> "$tmp"; fi
done

if [ ! -z "$OUTPUT" ]; then output="set output \"${OUTPUT}\""; fi

if $HAVE_GNUPLOT; then
gnuplot <<-EOF
    set xlabel "Connection count"
    set ylabel "TPS (Transactions Per Second)"
    set term "${TERM_TYPE}"
    ${output}
    plot "${tmp}" using 1:2:3:4 with errorbars title "LDAP TPS: ${CONNS_MIN}-${CONNS_MAX} connections"
EOF
    rm "$tmp"
fi
