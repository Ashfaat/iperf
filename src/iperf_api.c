
/*
 * Copyright (c) 2009, The Regents of the University of California, through
 * Lawrence Berkeley National Laboratory (subject to receipt of any required
 * approvals from the U.S. Dept. of Energy).  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <stdint.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>

#include "iperf.h"
#include "iperf_api.h"
#include "iperf_udp.h"
#include "iperf_tcp.h"
#include "iperf_error.h"
#include "timer.h"
#include "net.h"
#include "units.h"
#include "tcp_window_size.h"
#include "iperf_util.h"
#include "locale.h"

jmp_buf env;			/* to handle longjmp on signal */

/*************************************************************/

void
usage()
{
    fprintf(stderr, usage_short);
}

void
usage_long()
{
    fprintf(stderr, usage_long1);
    fprintf(stderr, usage_long2);
}

int
iperf_parse_arguments(struct iperf_test *test, int argc, char **argv)
{
    static struct option longopts[] =
    {
        {"client", required_argument, NULL, 'c'},
        {"server", no_argument, NULL, 's'},
        {"time", required_argument, NULL, 't'},
        {"port", required_argument, NULL, 'p'},
        {"parallel", required_argument, NULL, 'P'},
        {"udp", no_argument, NULL, 'u'},
        {"tcpInfo", no_argument, NULL, 'T'},
        {"bandwidth", required_argument, NULL, 'b'},
        {"length", required_argument, NULL, 'l'},
        {"window", required_argument, NULL, 'w'},
        {"interval", required_argument, NULL, 'i'},
        {"bytes", required_argument, NULL, 'n'},
        {"NoDelay", no_argument, NULL, 'N'},
        {"Print-mss", no_argument, NULL, 'm'},
        {"Set-mss", required_argument, NULL, 'M'},
        {"version", no_argument, NULL, 'v'},
        {"verbose", no_argument, NULL, 'V'},
        {"debug", no_argument, NULL, 'd'},
        {"help", no_argument, NULL, 'h'},
        {"daemon", no_argument, NULL, 'D'},
        {"format", required_argument, NULL, 'f'},
        {"reverse", no_argument, NULL, 'R'},

    /*  XXX: The following ifdef needs to be split up. linux-congestion is not necessarily supported
     *  by systems that support tos.
     */
#ifdef ADD_WHEN_SUPPORTED
        {"tos",        required_argument, NULL, 'S'},
        {"linux-congestion", required_argument, NULL, 'Z'},
#endif
        {NULL, 0, NULL, 0}
    };
    char ch;

    while ((ch = getopt_long(argc, argv, "c:p:st:uP:b:l:w:i:n:mRNTvhVdM:f:", longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':
                if (test->role == 's') {
                    ierrno = IESERVCLIENT;
                    return (-1);
                } else {
                    test->role = 'c';
                    test->server_hostname = (char *) malloc(strlen(optarg)+1);
                    strncpy(test->server_hostname, optarg, strlen(optarg));
                }
                break;
            case 'p':
                test->server_port = atoi(optarg);
                break;
            case 's':
                if (test->role == 'c') {
                    ierrno = IESERVCLIENT;
                    return (-1);
                } else {
                    test->role = 's';
                }
                break;
            case 't':
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->duration = atoi(optarg);
                if (test->duration > MAX_TIME) {
                    ierrno = IEDURATION;
                    return (-1);
                }
                break;
            case 'u':
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->protocol = Pudp;
                test->default_settings->blksize = DEFAULT_UDP_BLKSIZE;
                test->new_stream = iperf_new_udp_stream;
                break;
            case 'P':
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->num_streams = atoi(optarg);
                if (test->num_streams > MAX_STREAMS) {
                    ierrno = IENUMSTREAMS;
                    return (-1);
                }
                break;
            case 'b':
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->default_settings->rate = unit_atof(optarg);
                break;
            case 'l':
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->default_settings->blksize = unit_atoi(optarg);
                if (test->default_settings->blksize > MAX_BLOCKSIZE) {
                    ierrno = IEBLOCKSIZE;
                    return (-1);
                }
                break;
            case 'w':
                // XXX: This is a socket buffer, not specific to TCP
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->default_settings->socket_bufsize = unit_atof(optarg);
                if (test->default_settings->socket_bufsize > MAX_TCP_BUFFER) {
                    ierrno = IEBUFSIZE;
                    return (-1);
                }
                break;
            case 'i':
                /* XXX: could potentially want separate stat collection and reporting intervals,
                   but just set them to be the same for now */
                test->stats_interval = atoi(optarg);
                test->reporter_interval = atoi(optarg);
                if (test->stats_interval > MAX_INTERVAL) {
                    ierrno = IEINTERVAL;
                    return (-1);
                }
                break;
            case 'n':
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->default_settings->bytes = unit_atoi(optarg);
                break;
            case 'm':
                test->print_mss = 1;
                break;
            case 'N':
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->no_delay = 1;
                break;
            case 'M':
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->default_settings->mss = atoi(optarg);
                if (test->default_settings->mss > MAX_MSS) {
                    ierrno = IEMSS;
                    return (-1);
                }
                break;
            case 'f':
                test->default_settings->unit_format = *optarg;
                break;
            case 'T':
                test->tcp_info = 1;
                break;
            case 'V':
                test->verbose = 1;
                break;
            case 'd':
                test->debug = 1;
                break;
            case 'R':
                if (test->role == 's') {
                    ierrno = IECLIENTONLY;
                    return (-1);
                }
                test->reverse = 1;
                break;
            case 'v':
                printf(version);
                exit(0);
            case 'h':
            default:
                usage_long();
                exit(1);
        }
    }
    /* For subsequent calls to getopt */
#ifdef __APPLE__
    optreset = 1;
#endif
    optind = 0;

    if ((test->role != 'c') && (test->role != 's')) {
        ierrno = IENOROLE;
        return (-1);
    }

    return (0);
}

int
all_data_sent(struct iperf_test * test)
{
    if (test->default_settings->bytes > 0) {
        if (test->bytes_sent >= (test->num_streams * test->default_settings->bytes)) {
            return 1;
        }
    }

    return 0;
}

int
iperf_send(struct iperf_test *test)
{
    int result;
    iperf_size_t bytes_sent;
    fd_set temp_write_set;
    struct timeval tv;
    struct iperf_stream *sp;

    memcpy(&temp_write_set, &test->write_set, sizeof(fd_set));
    tv.tv_sec = 15;
    tv.tv_usec = 0;

    result = select(test->max_fd + 1, NULL, &temp_write_set, NULL, &tv);
    if (result < 0 && errno != EINTR) {
        perror("select iperf_send");
        return -1;
    }
    if (result > 0) {
        for (sp = test->streams; sp != NULL; sp = sp->next) {
            if (FD_ISSET(sp->socket, &temp_write_set)) {
                if ((bytes_sent = sp->snd(sp)) < 0) {
                    // XXX: Do better error handling
                    perror("iperf stream->snd");
                    return -1;
                }
                test->bytes_sent += bytes_sent;
                FD_CLR(sp->socket, &temp_write_set);
            }
        }
    }

    return 0;
}

int
iperf_recv(struct iperf_test *test)
{
    int result;
    iperf_size_t bytes_sent;
    fd_set temp_read_set;
    struct timeval tv;
    struct iperf_stream *sp;

    memcpy(&temp_read_set, &test->read_set, sizeof(fd_set));
    tv.tv_sec = 15;
    tv.tv_usec = 0;

    result = select(test->max_fd + 1, &temp_read_set, NULL, NULL, &tv);
    if (result < 0) {
        perror("select iperf_recv");
        return -1;
    }
    if (result > 0) {
        for (sp = test->streams; sp != NULL; sp = sp->next) {
            if (FD_ISSET(sp->socket, &temp_read_set)) {
                if ((bytes_sent = sp->rcv(sp)) < 0) {
                    // XXX: Do better error handling
                    perror("sp->rcv(sp)");
                    return -1;
                }
                test->bytes_sent += bytes_sent;
                FD_CLR(sp->socket, &temp_read_set);
            }
        }
    }

    return 0;
}

void
iperf_init_test(struct iperf_test *test)
{
    char *prot;
    struct iperf_stream *sp;
    int64_t dtargus;

    if (test->protocol == Pudp) {
        prot = "UDP";
        dtargus = (int64_t) test->default_settings->blksize * SEC_TO_US * 8;
        dtargus /= test->default_settings->rate;

        assert(dtargus != 0);

        for (sp = test->streams; sp; sp = sp->next)
            sp->send_timer = new_timer(dtargus / SEC_TO_US, dtargus % SEC_TO_US);
    } else {
        prot = "TCP";
    }

    /* Set timers */
    if (test->default_settings->bytes == 0) {
        test->timer = new_timer(test->duration, 0);
        printf(test_start_time, prot, test->num_streams, test->default_settings->blksize,
            test->duration);
    } else {
        printf(test_start_bytes, prot, test->num_streams, test->default_settings->blksize,
            test->default_settings->bytes);
    }

    if (test->stats_interval != 0)
        test->stats_timer = new_timer(test->stats_interval, 0);
    if (test->reporter_interval != 0)
        test->reporter_timer = new_timer(test->reporter_interval, 0);

    /* Set start time */
    for (sp = test->streams; sp; sp = sp->next)
        gettimeofday(&sp->result->start_time, NULL);
}


/*********************************************************/

int
package_parameters(struct iperf_test *test)
{
    char pstring[256];
    char optbuf[128];
    memset(pstring, 0, 256*sizeof(char));

    *pstring = ' ';

    if (test->protocol == Ptcp) {
        strncat(pstring, "-p ", sizeof(pstring));
    } else if (test->protocol == Pudp) {
        strncat(pstring, "-u ", sizeof(pstring));
    }

    snprintf(optbuf, sizeof(optbuf), "-P %d ", test->num_streams);
    strncat(pstring, optbuf, sizeof(pstring));
    
    if (test->reverse)
        strncat(pstring, "-R ", sizeof(pstring));
    
    if (test->default_settings->socket_bufsize) {
        snprintf(optbuf, sizeof(optbuf), "-w %d ", test->default_settings->socket_bufsize);
        strncat(pstring, optbuf, sizeof(pstring));
    }

    if (test->default_settings->rate) {
        snprintf(optbuf, sizeof(optbuf), "-b %llu ", test->default_settings->rate);
        strncat(pstring, optbuf, sizeof(pstring));
    }

    if (test->default_settings->mss) {
        snprintf(optbuf, sizeof(optbuf), "-m %d ", test->default_settings->mss);
        strncat(pstring, optbuf, sizeof(pstring));
    }

    if (test->no_delay) {
        snprintf(optbuf, sizeof(optbuf), "-N ");
        strncat(pstring, optbuf, sizeof(pstring));
    }

    if (test->default_settings->bytes) {
        snprintf(optbuf, sizeof(optbuf), "-n %llu ", test->default_settings->bytes);
        strncat(pstring, optbuf, sizeof(pstring));
    }

    if (test->duration) {
        snprintf(optbuf, sizeof(optbuf), "-t %d ", test->duration);
        strncat(pstring, optbuf, sizeof(pstring));
    }

    if (test->default_settings->blksize) {
        snprintf(optbuf, sizeof(optbuf), "-l %d ", test->default_settings->blksize);
        strncat(pstring, optbuf, sizeof(pstring));
    }

    *pstring = (char) (strlen(pstring) - 1);

    if (Nwrite(test->ctrl_sck, pstring, (size_t) strlen(pstring), Ptcp) < 0) {
        perror("Nwrite pstring");
        return -1;
    }

    return 0;
}


int
parse_parameters(struct iperf_test *test)
{
    int n;
    char *param, **params;
    char len, ch;
    char pstring[256];
    char readbuf[256];

    memset(pstring, 0, 256 * sizeof(char));

    if (read(test->ctrl_sck, &len, sizeof(char)) < 0) {
        perror("read len");
        return -1;
    }

    while (len > 0) {
        memset(readbuf, 0, 256 * sizeof(char));
        if ((len -= read(test->ctrl_sck, readbuf, (size_t) len)) < 0) {
            perror("read pstring");
            return -1;
        }
        strcat(pstring, readbuf);
    }

    for (param = strtok(pstring, " "), n = 0, params = NULL; param; param = strtok(NULL, " ")) {
        if ((params = realloc(params, (n+1)*sizeof(char *))) == NULL) {
            perror("realloc");
            return -1;
        }
        params[n] = param;
        n++;
    }

    while ((ch = getopt(n, params, "pt:n:m:uNP:Rw:l:b:")) != -1) {
        switch (ch) {
            case 'p':
                test->protocol = Ptcp;
                break;
            case 't':
                test->duration = atoi(optarg);
                break;
            case 'n':
                test->default_settings->bytes = atoll(optarg);
                break;
            case 'm':
                test->default_settings->mss = atoi(optarg);
                break;
            case 'u':
                test->protocol = Pudp;
                test->new_stream = iperf_new_udp_stream;
                break;
            case 'N':
                test->no_delay = 1;
                break;
            case 'P':
                test->num_streams = atoi(optarg);
                break;
            case 'R':
                test->reverse = 1;
                break;
            case 'w':
                test->default_settings->socket_bufsize = atoi(optarg);
                break;
            case 'l':
                test->default_settings->blksize = atoi(optarg);
                break;
            case 'b':
                test->default_settings->rate = atoll(optarg);
                break;
        }
    }
#ifdef __APPLE__
    optreset = 1;
#endif
    optind = 0;

    free(params);

    return 0;
}

/**
 * iperf_exchange_parameters - handles the param_Exchange part for client
 *
 */

int
iperf_exchange_parameters(struct iperf_test * test)
{
    int s, opt, len;
    struct sockaddr_in sa;

    if (test->role == 'c') {

        package_parameters(test);

    } else {
        parse_parameters(test);

        printf("      cookie: %s\n", test->default_settings->cookie);

        if (test->protocol == Pudp) {
            test->listener_udp = netannounce(test->protocol, NULL, test->server_port);
            FD_SET(test->listener_udp, &test->read_set);
            test->max_fd = (test->listener_udp > test->max_fd) ? test->listener_udp : test->max_fd;
        } else if (test->protocol == Ptcp) {
            if (test->no_delay || test->default_settings->mss) {
                FD_CLR(test->listener_tcp, &test->read_set);
                close(test->listener_tcp);
                if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                    perror("socket tcp listener mss");
                    return (-1);
                }
                if (test->no_delay) {
                    opt = 1;
                    if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
                        perror("setsockopt TCP_NODELAY");
                        return (-1);
                    }
                    printf("      TCP NODELAY: on\n");
                }
                // XXX: Setting MSS is very buggy!
                if (opt = test->default_settings->mss) {
                    if (setsockopt(s, IPPROTO_TCP, TCP_MAXSEG, &opt, sizeof(opt)) < 0) {
                        perror("setsockopt TCP_MAXSEG");
                        return (-1);
                    }
                    printf("      TCP MSS: %d\n", opt);
                }
                opt = 1;
                if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                    perror("setsockopt SO_REUSEADDR");
                    return (-1);
                }
                memset(&sa, 0, sizeof(sa));
                sa.sin_family = AF_INET;
                sa.sin_addr.s_addr = htonl(INADDR_ANY);
                sa.sin_port = htons(test->server_port);
                if (bind(s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
                    close(s);
                    perror("bind tcp mss/nodelay listener");
                    return (-1);
                }

                listen(s, 5);

                test->listener_tcp = s;
                test->max_fd = (s > test->max_fd) ? s : test->max_fd;
                FD_SET(test->listener_tcp, &test->read_set);
            }

        }

        // Send the control message to create streams and start the test
        test->state = CREATE_STREAMS;
        if (Nwrite(test->ctrl_sck, &test->state, sizeof(char), Ptcp) < 0) {
            perror("Nwrite CREATE_STREAMS");
            return -1;
        }

    }

    return 0;
}

/*************************************************************/

int
iperf_exchange_results(struct iperf_test *test)
{
    unsigned int size;
    char buf[128];
    char *results;
    struct iperf_stream *sp;
    iperf_size_t bytes_transferred;

    if (test->role == 'c') {
        /* Prepare results string and send to server */
        results = NULL;
        size = 0;
        for (sp = test->streams; sp; sp = sp->next) {
            bytes_transferred = (test->reverse ? sp->result->bytes_received : sp->result->bytes_sent);
            snprintf(buf, 128, "%d:%llu,%lf,%d,%d\n", sp->id, bytes_transferred,sp->jitter,
                sp->cnt_error, sp->packet_count);
            size += strlen(buf);
            if ((results = realloc(results, size+1)) == NULL) {
                perror("realloc results");
                return -1;
            }
            if (sp == test->streams)
                *results = '\0';
            strncat(results, buf, size+1);
        }
        size++;
        size = htonl(size);
        if (Nwrite(test->ctrl_sck, &size, sizeof(size), Ptcp) < 0) {
            perror("Nwrite size");
            return (-1);
        }
        if (Nwrite(test->ctrl_sck, results, ntohl(size), Ptcp) < 0) {
            perror("Nwrite results");
            return (-1);
        }
        free(results);

        /* Get server results string */
        if (Nread(test->ctrl_sck, &size, sizeof(size), Ptcp) < 0) {
            perror("Nread size");
            return (-1);
        }
        size = ntohl(size);
        results = (char *) malloc(size * sizeof(char));
        if (results == NULL) {
            perror("malloc results");
            return (-1);
        }
        if (Nread(test->ctrl_sck, results, size, Ptcp) < 0) {
            perror("Nread results");
            return (-1);
        }

        parse_results(test, results);

        free(results);

    } else {
        /* Get client results string */
        if (Nread(test->ctrl_sck, &size, sizeof(size), Ptcp) < 0) {
            perror("Nread size");
            return (-1);
        }
        size = ntohl(size);
        results = (char *) malloc(size * sizeof(char));
        if (results == NULL) {
            perror("malloc results");
            return (-1);
        }
        if (Nread(test->ctrl_sck, results, size, Ptcp) < 0) {
            perror("Nread results");
            return (-1);
        }

        parse_results(test, results);

        free(results);

        /* Prepare results string and send to client */
        results = NULL;
        size = 0;
        for (sp = test->streams; sp; sp = sp->next) {
            bytes_transferred = (test->reverse ? sp->result->bytes_sent : sp->result->bytes_received);
            snprintf(buf, 128, "%d:%llu,%lf,%d,%d\n", sp->id, bytes_transferred, sp->jitter,
                sp->cnt_error, sp->packet_count);
            size += strlen(buf);
            if ((results = realloc(results, size+1)) == NULL) {
                perror("realloc results");
                return (-1);
            }
            if (sp == test->streams)
                *results = '\0';
            strncat(results, buf, size+1);
        }
        size++;
        size = htonl(size);
        if (Nwrite(test->ctrl_sck, &size, sizeof(size), Ptcp) < 0) {
            perror("Nwrite size");
            return (-1);
        }
        if (Nwrite(test->ctrl_sck, results, ntohl(size), Ptcp) < 0) {
            perror("Nwrite results");
            return (-1);
        }
        free(results);

    }

    return 0;
}

/*************************************************************/

int
parse_results(struct iperf_test *test, char *results)
{
    int sid, cerror, pcount;
    double jitter;
    char *strp;
    iperf_size_t bytes_transferred;
    struct iperf_stream *sp;

    for (strp = results; *strp; strp = strchr(strp, '\n')+1) {
        sscanf(strp, "%d:%llu,%lf,%d,%d\n", &sid, &bytes_transferred, &jitter,
            &cerror, &pcount);
        for (sp = test->streams; sp; sp = sp->next)
            if (sp->id == sid) break;
        if (sp == NULL) {
            fprintf(stderr, "error: No stream with id %d\n", sid);
            return (-1);
        }
        if ((test->role == 'c' && !test->reverse) || (test->role == 's' && test->reverse)) {
            sp->jitter = jitter;
            sp->cnt_error = cerror;
            sp->packet_count = pcount;
            sp->result->bytes_received = bytes_transferred;
        } else
            sp->result->bytes_sent = bytes_transferred;
    }

    return 0;
}


/*************************************************************/
/**
 * add_to_interval_list -- adds new interval to the interval_list
 *
 */

void
add_to_interval_list(struct iperf_stream_result * rp, struct iperf_interval_results * new)
{
    struct iperf_interval_results *ip = NULL;

    ip = (struct iperf_interval_results *) malloc(sizeof(struct iperf_interval_results));
    memcpy(ip, new, sizeof(struct iperf_interval_results));
    ip->next = NULL;

    if (rp->interval_results == NULL) {	/* if 1st interval */
        rp->interval_results = ip;
        rp->last_interval_results = ip; /* pointer to last element in list */
    } else { /* add to end of list */
        rp->last_interval_results->next = ip;
        rp->last_interval_results = ip;
    }
}

 /*************************************************************/
 /* for debugging only */
void
display_interval_list(struct iperf_stream_result * rp, int tflag)
{
    struct iperf_interval_results *n;
    float gb = 0.;

    n = rp->interval_results;

    printf("----------------------------------------\n");
    while (n != NULL) {
        gb = (float) n->bytes_transferred / (1024. * 1024. * 1024.);
        printf("Interval = %f\tGBytes transferred = %.3f\n", n->interval_duration, gb);
        if (tflag)
            print_tcpinfo(n);
        n = n->next;
    }
}

/************************************************************/

/**
 * connect_msg -- displays connection message
 * denoting sender/receiver details
 *
 */

void
connect_msg(struct iperf_stream * sp)
{
    char ipl[512], ipr[512];

    inet_ntop(AF_INET, (void *) (&((struct sockaddr_in *) & sp->local_addr)->sin_addr), (void *) ipl, sizeof(ipl));
    inet_ntop(AF_INET, (void *) (&((struct sockaddr_in *) & sp->remote_addr)->sin_addr), (void *) ipr, sizeof(ipr));

    printf("[%3d] local %s port %d connected to %s port %d\n",
        sp->socket,
        ipl, ntohs(((struct sockaddr_in *) & sp->local_addr)->sin_port),
        ipr, ntohs(((struct sockaddr_in *) & sp->remote_addr)->sin_port));
}

/*************************************************************/
/**
 * Display -- Displays results for test
 * Mainly for DEBUG purpose
 *
 */

void
Display(struct iperf_test * test)
{
    int count = 1;
    struct iperf_stream *n;

    n = test->streams;

    printf("===============DISPLAY==================\n");

    while (n != NULL) {
        if (test->role == 'c') {
            printf("position-%d\tsp=%llu\tsocket=%d\tMbytes sent=%u\n",
                count++, (uint64_t) n, n->socket, (unsigned int) (n->result->bytes_sent / (float) MB));
        } else {
            printf("position-%d\tsp=%llu\tsocket=%d\tMbytes received=%u\n",
                count++, (uint64_t) n, n->socket, (unsigned int) (n->result->bytes_received / (float) MB));
        }
        n = n->next;
    }
    printf("=================END====================\n");
    fflush(stdout);
}

/**************************************************************************/

struct iperf_test *
iperf_new_test()
{
    struct iperf_test *testp;

    testp = (struct iperf_test *) malloc(sizeof(struct iperf_test));
    if (!testp) {
        perror("malloc");
        return (NULL);
    }
    /* initialize everything to zero */
    memset(testp, 0, sizeof(struct iperf_test));

    testp->default_settings = (struct iperf_settings *) malloc(sizeof(struct iperf_settings));
    memset(testp->default_settings, 0, sizeof(struct iperf_settings));

    return testp;
}

/**************************************************************************/
void
iperf_defaults(struct iperf_test * testp)
{
    testp->protocol = Ptcp;
    testp->duration = DURATION;
    testp->server_port = PORT;
    testp->ctrl_sck = -1;

    testp->new_stream = iperf_new_tcp_stream;
    testp->stats_callback = iperf_stats_callback;
    testp->reporter_callback = iperf_reporter_callback;

    testp->stats_interval = 0;
    testp->reporter_interval = 0;
    testp->num_streams = 1;

    testp->default_settings->unit_format = 'a';
    testp->default_settings->socket_bufsize = 0;	/* use autotuning */
    testp->default_settings->blksize = DEFAULT_TCP_BLKSIZE;
    testp->default_settings->rate = RATE;	/* UDP only */
    testp->default_settings->state = TEST_START;
    testp->default_settings->mss = 0;
    testp->default_settings->bytes = 0;
    memset(testp->default_settings->cookie, 0, COOKIE_SIZE);
}

/**************************************************************************/

int
iperf_create_streams(struct iperf_test *test)
{
    struct iperf_stream *sp;
    struct sockaddr_in sa;
    struct hostent *hent;
    int i, s, buf, opt;

    for (i = 0; i < test->num_streams; ++i) {
        if (test->protocol == Ptcp && (test->no_delay || test->default_settings->mss)) {
            if ((hent = gethostbyname(test->server_hostname)) == 0) {
                perror("gethostbyname");    
                return (-1);
            }
            if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                perror("socket");
                return (-1);
            }
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            memcpy(&sa.sin_addr.s_addr, hent->h_addr, sizeof(sa.sin_addr.s_addr));
            sa.sin_port = htons(test->server_port);

            if (test->no_delay) {
                opt = 1;
                if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
                    perror("setsockopt");
                    return (-1);
                }
            }
            if (opt = test->default_settings->mss) {
                if (setsockopt(s, IPPROTO_TCP, TCP_MAXSEG, &opt, sizeof(opt)) < 0) {
                    perror("setsockopt");
                    return (-1);
                }
            }
            if (connect(s, (struct sockaddr *) &sa, sizeof(sa)) < 0 && errno != EINPROGRESS) {
                perror("connect tcp stream");
                return (-1);
            }
        } else {
            s = netdial(test->protocol, test->server_hostname, test->server_port);
            if (s < 0) {
                perror("netdial stream");
                return -1;
            }
        }

        if (test->protocol == Ptcp) {
            if (Nwrite(s, test->default_settings->cookie, COOKIE_SIZE, Ptcp) < 0) {
                perror("Nwrite COOKIE\n");
                return -1;
            }
        } else {
            if (write(s, &buf, sizeof(i)) < 0) {
                perror("write data");
                return -1;
            }
            // XXX: Should this read be TCP instead?
            if (read(s, &buf, sizeof(i)) < 0) {
                perror("read data");
                return -1;
            }
        }

        FD_SET(s, &test->read_set);
        FD_SET(s, &test->write_set);
        test->max_fd = (test->max_fd < s) ? s : test->max_fd;

        // XXX: This doesn't fit our API model!
        sp = test->new_stream(test);
        sp->socket = s;
        iperf_init_stream(sp, test);
        iperf_add_stream(test, sp);

        connect_msg(sp);
    }

    return 0;
}

int
iperf_handle_message_client(struct iperf_test *test)
{
    int rval;

    if ((rval = read(test->ctrl_sck, &test->state, sizeof(char))) <= 0) {
        if (rval == 0) {
            fprintf(stderr, "The server has unexpectedly closed the connection. Exiting...\n");
            exit(1);
        } else {
            perror("read ctrl_sck");
            return -1;
        }
    }

    switch (test->state) {
        case PARAM_EXCHANGE:
            iperf_exchange_parameters(test);
            break;
        case CREATE_STREAMS:
            iperf_create_streams(test);
            break;
        case TEST_START:
            iperf_init_test(test);
            break;
        case TEST_RUNNING:
            break;
        case EXCHANGE_RESULTS:
            iperf_exchange_results(test);
            break;
        case DISPLAY_RESULTS:
            iperf_client_end(test);
            break;
        case IPERF_DONE:
            break;
        case SERVER_TERMINATE:
            fprintf(stderr, "The server has terminated. Exiting...\n");
            exit(1);
        case ACCESS_DENIED:
            fprintf(stderr, "The server is busy running a test. Try again later.\n");
            exit(0);
        default:
            // XXX: This needs to be removed from the production version
            printf("How did you get here? test->state = %d\n", test->state);
            break;
    }

    return 0;
}

/* iperf_connect -- client to server connection function */
int
iperf_connect(struct iperf_test *test)
{
    printf("Connecting to host %s, port %d\n", test->server_hostname, test->server_port);

    FD_ZERO(&test->read_set);
    FD_ZERO(&test->write_set);

    get_uuid(test->default_settings->cookie);

    /* Create and connect the control channel */
    test->ctrl_sck = netdial(Ptcp, test->server_hostname, test->server_port);
    if (test->ctrl_sck < 0) {
        return -1;
    }

    if (Nwrite(test->ctrl_sck, test->default_settings->cookie, COOKIE_SIZE, Ptcp) < 0) {
        perror("Nwrite COOKIE\n");
        return -1;
    }

    FD_SET(test->ctrl_sck, &test->read_set);
    FD_SET(test->ctrl_sck, &test->write_set);
    test->max_fd = (test->ctrl_sck > test->max_fd) ? test->ctrl_sck : test->max_fd;

    return 0;
}

/**************************************************************************/
void
iperf_free_test(struct iperf_test * test)
{
    free(test->default_settings);
    free_timer(test->timer);
    free_timer(test->stats_timer);
    free_timer(test->reporter_timer);

    test->streams = NULL;
    test->accept = NULL;
    test->stats_callback = NULL;
    test->reporter_callback = NULL;
    test->new_stream = NULL;
    free(test);
}

/**************************************************************************/

/**
 * iperf_stats_callback -- handles the statistic gathering for both the client and server
 *
 * XXX: This function needs to be updated to reflect the new code
 */


void
iperf_stats_callback(struct iperf_test * test)
{
    struct iperf_stream *sp;
    struct iperf_stream_result *rp = NULL;
    struct iperf_interval_results *ip = NULL, temp;

    for (sp = test->streams; sp != NULL; sp = sp->next) {
        rp = sp->result;

        if (test->role == 'c')
            temp.bytes_transferred = rp->bytes_sent_this_interval;
        else
            temp.bytes_transferred = rp->bytes_received_this_interval;
     
        ip = sp->result->interval_results;
        /* result->end_time contains timestamp of previous interval */
        if ( ip != NULL ) /* not the 1st interval */
            memcpy(&temp.interval_start_time, &sp->result->end_time, sizeof(struct timeval));
        else /* or use timestamp from beginning */
            memcpy(&temp.interval_start_time, &sp->result->start_time, sizeof(struct timeval));
        /* now save time of end of this interval */
        gettimeofday(&sp->result->end_time, NULL);
        memcpy(&temp.interval_end_time, &sp->result->end_time, sizeof(struct timeval));
        temp.interval_duration = timeval_diff(&temp.interval_start_time, &temp.interval_end_time);
        //temp.interval_duration = timeval_diff(&temp.interval_start_time, &temp.interval_end_time);
        if (test->tcp_info)
            get_tcpinfo(test, &temp);
        //printf(" iperf_stats_callback: adding to interval list: \n");
        add_to_interval_list(rp, &temp);
        rp->bytes_sent_this_interval = rp->bytes_received_this_interval = 0;

    }

}

/**************************************************************************/

/**
 * iperf_reporter_callback -- handles the report printing
 *
 */

void
iperf_reporter_callback(struct iperf_test * test)
{
    int total_packets = 0, lost_packets = 0;
    char ubuf[UNIT_LEN];
    char nbuf[UNIT_LEN];
    struct iperf_stream *sp = NULL;
    iperf_size_t bytes = 0, bytes_sent = 0, bytes_received = 0;
    iperf_size_t total_sent = 0, total_received = 0;
    double start_time, end_time, avg_jitter;
    struct iperf_interval_results *ip = NULL;

    switch (test->state) {
        case TEST_RUNNING:
        case STREAM_RUNNING:
            /* print interval results for each stream */
            for (sp = test->streams; sp != NULL; sp = sp->next) {
                print_interval_results(test, sp);
                bytes += sp->result->interval_results->bytes_transferred; /* sum up all streams */
            }
            if (bytes <=0 ) { /* this can happen if timer goes off just when client exits */
                fprintf(stderr, "error: bytes <= 0!\n");
                break;
            }
            /* next build string with sum of all streams */
            if (test->num_streams > 1) {
                sp = test->streams; /* reset back to 1st stream */
                ip = test->streams->result->last_interval_results;	/* use 1st stream for timing info */

                unit_snprintf(ubuf, UNIT_LEN, (double) (bytes), 'A');
                unit_snprintf(nbuf, UNIT_LEN, (double) (bytes / ip->interval_duration),
                        test->default_settings->unit_format);

                start_time = timeval_diff(&sp->result->start_time,&ip->interval_start_time);
                end_time = timeval_diff(&sp->result->start_time,&ip->interval_end_time);
                printf(report_sum_bw_format, start_time, end_time, ubuf, nbuf);

#if defined(linux) || defined(__FreeBSD__)			/* is it usful to figure out a way so sum * TCP_info acrross multiple streams? */
                if (test->tcp_info)
                    print_tcpinfo(ip);
#endif
            }
            break;
        case DISPLAY_RESULTS:
            /* print final summary for all intervals */

            printf(report_bw_header);

            start_time = 0.;
            sp = test->streams;
            end_time = timeval_diff(&sp->result->start_time, &sp->result->end_time);
            for (sp = test->streams; sp != NULL; sp = sp->next) {
                bytes_sent = sp->result->bytes_sent;
                bytes_received = sp->result->bytes_received;
                total_sent += bytes_sent;
                total_received += bytes_received;

                if (test->protocol == Pudp) {
                    total_packets += sp->packet_count;
                    lost_packets += sp->cnt_error;
                    avg_jitter += sp->jitter;
                }

                if (bytes_sent > 0) {
                    unit_snprintf(ubuf, UNIT_LEN, (double) (bytes_sent), 'A');
                    unit_snprintf(nbuf, UNIT_LEN, (double) (bytes_sent / end_time), test->default_settings->unit_format);
                    if (test->protocol == Ptcp) {
                        printf("      Sent\n");
                        printf(report_bw_format, sp->socket, start_time, end_time, ubuf, nbuf);

#if defined(linux) || defined(__FreeBSD__)
                        if (test->tcp_info) {
                            ip = sp->result->last_interval_results;	
                            print_tcpinfo(ip);
                        }
#endif
                    } else {
                        printf(report_bw_jitter_loss_format, sp->socket, start_time,
                                end_time, ubuf, nbuf, sp->jitter * 1000, sp->cnt_error, 
                                sp->packet_count, (double) (100.0 * sp->cnt_error / sp->packet_count));
                        if (test->role == 'c') {
                            printf(report_datagrams, sp->socket, sp->packet_count);
                        }
                        if (sp->outoforder_packets > 0)
                            printf(report_sum_outoforder, start_time, end_time, sp->cnt_error);
                    }
                }
                if (bytes_received > 0) {
                    unit_snprintf(ubuf, UNIT_LEN, (double) bytes_received, 'A');
                    unit_snprintf(nbuf, UNIT_LEN, (double) (bytes_received / end_time), test->default_settings->unit_format);
                    if (test->protocol == Ptcp) {
                        printf("      Received\n");
                        printf(report_bw_format, sp->socket, start_time, end_time, ubuf, nbuf);
                    }
                }
            }

            if (test->num_streams > 1) {
                unit_snprintf(ubuf, UNIT_LEN, (double) total_sent, 'A');
                unit_snprintf(nbuf, UNIT_LEN, (double) total_sent / end_time, test->default_settings->unit_format);
                if (test->protocol == Ptcp) {
                    printf("      Total sent\n");
                    printf(report_sum_bw_format, start_time, end_time, ubuf, nbuf);
                    unit_snprintf(ubuf, UNIT_LEN, (double) total_received, 'A');
                    unit_snprintf(nbuf, UNIT_LEN, (double) (total_received / end_time), test->default_settings->unit_format);
                    printf("      Total received\n");
                    printf(report_sum_bw_format, start_time, end_time, ubuf, nbuf);
                } else {
                    avg_jitter /= test->num_streams;
                    printf(report_sum_bw_jitter_loss_format, start_time, end_time, ubuf, nbuf, avg_jitter,
                        lost_packets, total_packets, (double) (100.0 * lost_packets / total_packets));
                }

                // XXX: Why is this here?
                if ((test->print_mss != 0) && (test->role == 'c')) {
                    printf("The TCP maximum segment size mss = %d\n", getsock_tcp_mss(sp->socket));
                }
            }
            break;
    } 

}

/**************************************************************************/
void
print_interval_results(struct iperf_test * test, struct iperf_stream * sp)
{
    char ubuf[UNIT_LEN];
    char nbuf[UNIT_LEN];
    double st = 0., et = 0.;
    struct iperf_interval_results *ir = NULL;

    ir = sp->result->last_interval_results; /* get last entry in linked list */
    if (ir == NULL) {
        printf("print_interval_results Error: interval_results = NULL \n");
        return;
    }
    if (sp == test->streams) {
        printf(report_bw_header);
    }

    unit_snprintf(ubuf, UNIT_LEN, (double) (ir->bytes_transferred), 'A');
    unit_snprintf(nbuf, UNIT_LEN, (double) (ir->bytes_transferred / ir->interval_duration),
            test->default_settings->unit_format);
    
    st = timeval_diff(&sp->result->start_time,&ir->interval_start_time);
    et = timeval_diff(&sp->result->start_time,&ir->interval_end_time);
    
    printf(report_bw_format, sp->socket, st, et, ubuf, nbuf);

#if defined(linux) || defined(__FreeBSD__)
    if (test->tcp_info)
        print_tcpinfo(ir);
#endif
}

/**************************************************************************/
void
iperf_free_stream(struct iperf_stream * sp)
{
    struct iperf_interval_results *ip, *np;

    /* XXX: need to free interval list too! */
    free(sp->buffer);
    free(sp->settings);
    for (ip = sp->result->interval_results; ip; ip = np) {
        np = ip->next;
        free(ip);
    }
    free(sp->result);
    free(sp->send_timer);
    free(sp);
}

/**************************************************************************/
struct iperf_stream *
iperf_new_stream(struct iperf_test *testp)
{
    int i;
    struct iperf_stream *sp;

    sp = (struct iperf_stream *) malloc(sizeof(struct iperf_stream));
    if (!sp) {
        perror("malloc");
        return (NULL);
    }
    memset(sp, 0, sizeof(struct iperf_stream));

    sp->buffer = (char *) malloc(testp->default_settings->blksize);
    sp->settings = (struct iperf_settings *) malloc(sizeof(struct iperf_settings));
    sp->result = (struct iperf_stream_result *) malloc(sizeof(struct iperf_stream_result));

    if (!sp->buffer) {
        perror("Malloc sp->buffer");
        return NULL;
    }
    if (!sp->settings) {
        perror("Malloc sp->settings");
        return NULL;
    }
    if (!sp->result) {
        perror("Malloc sp->result");
        return NULL;
    }
    
    /* Make a per stream copy of default_settings in each stream structure */
    // XXX: These settings need to be moved to the test struct
    memcpy(sp->settings, testp->default_settings, sizeof(struct iperf_settings));

    /* Randomize the buffer */
    srandom(time(0));
    for (i = 0; i < testp->default_settings->blksize; ++i)
        sp->buffer[i] = random();

    sp->socket = -1;

    //  XXX: Some of this code is needed, even though everything is already zero.
    sp->packet_count = 0;
    sp->jitter = 0.0;
    sp->prev_transit = 0.0;
    sp->outoforder_packets = 0;
    sp->cnt_error = 0;

    sp->send_timer = NULL;
    sp->next = NULL;

    sp->result->interval_results = NULL;
    sp->result->last_interval_results = NULL;
    sp->result->bytes_received = 0;
    sp->result->bytes_sent = 0;
    sp->result->bytes_received_this_interval = 0;
    sp->result->bytes_sent_this_interval = 0;

    sp->settings->state = STREAM_BEGIN;
    return sp;
}

/**************************************************************************/
void
iperf_init_stream(struct iperf_stream * sp, struct iperf_test * testp)
{
    socklen_t len;

    len = sizeof(struct sockaddr_in);

    if (getsockname(sp->socket, (struct sockaddr *) &sp->local_addr, &len) < 0) {
        perror("getsockname");
    }
    if (getpeername(sp->socket, (struct sockaddr *) &sp->remote_addr, &len) < 0) {
        perror("getpeername");
    }
    if (testp->protocol == Ptcp) {
        if (set_tcp_windowsize(sp->socket, testp->default_settings->socket_bufsize,
                testp->role == 's' ? SO_RCVBUF : SO_SNDBUF) < 0)
            fprintf(stderr, "unable to set window size\n");

        /* set TCP_NODELAY and TCP_MAXSEG if requested */
        set_tcp_options(sp->socket, testp->no_delay, testp->default_settings->mss);
    }

}

/**************************************************************************/
int
iperf_add_stream(struct iperf_test * test, struct iperf_stream * sp)
{
    int i;
    struct iperf_stream *n;

    if (!test->streams) {
        test->streams = sp;
        sp->id = 1;
        return 1;
    } else {
        for (n = test->streams, i = 2; n->next; n = n->next, ++i);
        n->next = sp;
        sp->id = i;
        return 1;
    }

    return 0;
}


/**************************************************************************/

int
iperf_client_end(struct iperf_test *test)
{
    struct iperf_stream *sp, *np;
    printf("Test Complete. Summary Results:\n");
 
    /* show final summary */
    test->reporter_callback(test);

    /* Deleting all streams - CAN CHANGE FREE_STREAM FN */
    for (sp = test->streams; sp; sp = np) {
        close(sp->socket);
        np = sp->next;
        iperf_free_stream(sp);
    }

    test->state = IPERF_DONE;
    if (Nwrite(test->ctrl_sck, &test->state, sizeof(char), Ptcp) < 0) {
        perror("Nwrite IPERF_DONE");
        return -1;
    }

    return 0;
}

void
sig_handler(int sig)
{
   longjmp(env, 1); 
}

int
iperf_run_client(struct iperf_test * test)
{
    int result;
    fd_set temp_read_set, temp_write_set;
    struct timeval tv;

    /* Start the client and connect to the server */
    if (iperf_connect(test) < 0) {
        // set error and return
        return -1;
    }

    signal(SIGINT, sig_handler);
    if (setjmp(env)) {
        fprintf(stderr, "Exiting...\n");
        test->state = CLIENT_TERMINATE;
        if (Nwrite(test->ctrl_sck, &test->state, sizeof(char), Ptcp) < 0) {
            fprintf(stderr, "Unable to send CLIENT_TERMINATE message to server\n");
        }
        exit(1);
    }

    while (test->state != IPERF_DONE) {

        memcpy(&temp_read_set, &test->read_set, sizeof(fd_set));
        memcpy(&temp_write_set, &test->write_set, sizeof(fd_set));
        tv.tv_sec = 15;
        tv.tv_usec = 0;

        result = select(test->max_fd + 1, &temp_read_set, &temp_write_set, NULL, &tv);
        if (result < 0 && errno != EINTR) {
            perror("select");
            exit(1);
        } else if (result > 0) {
            if (FD_ISSET(test->ctrl_sck, &temp_read_set)) {
                iperf_handle_message_client(test);
                FD_CLR(test->ctrl_sck, &temp_read_set);
            }

            if (test->state == TEST_RUNNING) {
                if (test->reverse) {
                    // Reverse mode. Client receives.
                    iperf_recv(test);
                } else {
                    // Regular mode. Client sends.
                    iperf_send(test);
                }

                /* Perform callbacks */
                if (timer_expired(test->stats_timer)) {
                    test->stats_callback(test);
                    update_timer(test->stats_timer, test->stats_interval, 0);
                }
                if (timer_expired(test->reporter_timer)) {
                    test->reporter_callback(test);
                    update_timer(test->reporter_timer, test->reporter_interval, 0);
                }
               
                /* Send TEST_END if all data has been sent or timer expired */ 
                if (all_data_sent(test) || timer_expired(test->timer)) {
                    test->stats_callback(test);
                    test->state = TEST_END;
                    if (Nwrite(test->ctrl_sck, &test->state, sizeof(char), Ptcp) < 0) {
                        perror("Nwrite TEST_END");
                        return -1;
                    }
                }
            }
        }
    }

    return 0;
}
