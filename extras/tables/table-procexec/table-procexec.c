/*	$OpenBSD$	*/

/*
 * Copyright (c) 2024 Omar Polo <op@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <sys/time.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <smtpd-api.h>

#include "log.h"

#define PROTOCOL_VERSION	"0.1"

int	 services;
pid_t	 pid;
FILE	*backend;
char	*line;
size_t	 linesize;
ssize_t	 linelen;

char id[64];

static char *
nextid(void)
{
	int r;

	r = snprintf(id, sizeof(id), "%llx", (unsigned long long)arc4random());
	if (r < 0 || (size_t)r >= sizeof(id))
		fatal("snprintf");

	return (id);
}

static const char *
service_name(int service)
{
	switch (service) {
	case K_ALIAS:		return ("alias");
	case K_DOMAIN:		return ("domain");
	case K_CREDENTIALS:	return ("credentials");
	case K_NETADDR:		return ("netaddr");
	case K_USERINFO:	return ("userinfo");
	case K_SOURCE:		return ("source");
	case K_MAILADDR:	return ("mailaddr");
	case K_ADDRNAME:	return ("addrname");
	case K_MAILADDRMAP:	return ("mailaddrmap");
	}

	fatalx("unknown service %d", service);
}

static int
send_request(const char *type, int service, const char *param)
{
	struct timeval	 tv;

	gettimeofday(&tv, NULL);

	fprintf(backend, "table|%s|%lld.%06ld|%s|%s",
	    PROTOCOL_VERSION, (long long)tv.tv_sec, (long)tv.tv_usec,
	    table_api_get_name(), type);

	if (service != -1) {
		fprintf(backend, "|%s|%s", service_name(service), nextid());
		if (param)
			fprintf(backend, "|%s", param);
		fputc('\n', backend);
	} else
		fprintf(backend, "|%s\n", nextid());

	if (fflush(backend) == EOF)
		return (-1);
	return (0);
}

static const char *
parse_reply(const char *type)
{
	const char	*l;
	size_t		 len;

	if ((linelen = getline(&line, &linesize, backend)) == -1)
		fatal("getline");
	if (line[linelen-1] == '\n')
		line[--linelen] = '\0';
	l = line;

	len = strlen(type);
	if (strncmp(l, type, len) != 0)
		return (NULL);
	l += len;

	if (*l != '|')
		return (NULL);
	l++;

	len = strlen(id);
	if (strncmp(l, id, len) != 0)
		return (NULL);
	l += len;

	if (*l != '|')
		return (NULL);
	return (++l);
}

static int
table_procexec_update(void)
{
	const char	*r;

	if (send_request("update", -1, NULL) == -1)
		fatal("send_request");

	if ((r = parse_reply("update-result")) == NULL)
		fatalx("malformed line: %s", line);

	if (!strcmp(r, "ok"))
		return (1);

	if (strcmp(r, "error") != 0)
		log_warnx("update-result: unexpected value: %s", r);

	return (0);
}

static int
table_procexec_check(int service, struct dict *params, const char *key)
{
	const char	*r;

	if (!(services & service))
		return (-1);

	if (send_request("check", service, key) == -1)
		fatal("fflush");

	if ((r = parse_reply("check-result")) == NULL)
		fatalx("malformed line: %s", line);

	if (!strcmp(r, "found"))
		return (1);

	if (strcmp(r, "error") != 0)
		log_warnx("invalid response: %s", r);

	return (-1);
}

static int
table_procexec_lookup(int service, struct dict *params, const char *key,
    char *dst, size_t sz)
{
	const char	*r;

	if (!(services & service))
		return (-1);

	if (send_request("lookup", service, key) == -1)
		fatal("fflush");

	if ((r = parse_reply("lookup-result")) == NULL)
		fatalx("malformed line: %s", line);

	if (!strncmp(r, "found|", 6)) {
		r += 6;
		if (strlcpy(dst, r, sz) >= sz) {
			log_warnx("warn: result too large");
			return (-1);
		}
		return (1);
	}

	if (!strcmp(r, "not-found"))
		return (0);

	if (strcmp(r, "error") != 0)
		log_warnx("invalid response: %s", r);

	return (-1);
}

static int
table_procexec_fetch(int service, struct dict *params, char *dst, size_t sz)
{
	const char	*r, *k;

	if (!(services & service))
		return (-1);

	if (send_request("fetch", service, NULL))
		fatal("fflush");

	if ((r = parse_reply("fetch-result")) == NULL)
		fatalx("malformed line: %s", line);
	if (!strcmp(r, "not-found"))
		return (0);
	if (!strcmp(r, "error"))
		return (-1);

	if (strncmp(r, "found|", 6) != 0)
		fatalx("malformed line: %s", line);
	r += 6;

	if (strlcpy(dst, k, sz) >= sz)
		return (-1);

	return (1);
}

static int
spawn_backend(int argc, char **argv)
{
	int p[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, p) == -1)
		fatal("socketpair");

	if ((pid = fork()) == -1)
		fatal("fork");

	if (pid == 0) {		/* child */
		close(p[0]);
		dup2(p[1], 0);
		dup2(p[1], 1);
		/* stderr is inherited */

		execvp(argv[0], argv);
		log_warn("execvp %s", argv[0]);
		_exit(1);
	}

	close(p[1]);
	return (p[0]);
}

static void __dead
usage(void)
{
	fprintf(stderr, "usage: %s table-backend [args...]\n", getprogname());
	exit(1);
}

int
main(int argc, char **argv)
{
	int		 ch, fd;
	const char	*service;

#if 0
	static int attached;
	while (!attached) {
		fprintf(stderr, "my pid is %d\n", getpid());
		sleep(1);
	}
#endif

	log_init(1);
	log_verbose(1);

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	if ((fd = spawn_backend(argc, argv)) == -1)
		fatal("spawn_backend");
	if ((backend = fdopen(fd, "r+")) == NULL) {
		log_warn("fdopen");
		kill(pid, SIGTERM);
		exit(1);
	}

	fprintf(backend, "config|smtpd-version|7.4.0\n");
	fprintf(backend, "config|protocol|"PROTOCOL_VERSION"\n");
	fprintf(backend, "config|ready\n");
	fflush(backend);

	while ((linelen = getline(&line, &linesize, backend)) != -1) {
		if (line[linelen-1] == '\n')
			line[--linelen] = '\0';

		if (strncmp(line, "register|", 9) != 0)
			fatalx("invalid line: %s", line);

		service = line + 9;
		if (!strcmp(service, "ready"))
			break;
		else if (!strcmp(service, "alias"))
			services |= K_ALIAS;
		else if (!strcmp(service, "domain"))
			services |= K_DOMAIN;
		else if (!strcmp(service, "credentials"))
			services |= K_CREDENTIALS;
		else if (!strcmp(service, "netaddr"))
			services |= K_NETADDR;
		else if (!strcmp(service, "userinfo"))
			services |= K_USERINFO;
		else if (!strcmp(service, "source"))
			services |= K_SOURCE;
		else if (!strcmp(service, "mailaddr"))
			services |= K_MAILADDR;
		else if (!strcmp(service, "addrname"))
			services |= K_ADDRNAME;
		else if (!strcmp(service, "mailaddrmap"))
			services |= K_MAILADDRMAP;
		else
			fatalx("unknown service %s", service);
	}

	if (ferror(backend))
		fatal("getline");

	if (services == 0)
		fatalx("no services registered");

	table_api_on_update(table_procexec_update);
	table_api_on_check(table_procexec_check);
	table_api_on_lookup(table_procexec_lookup);
	table_api_on_fetch(table_procexec_fetch);
	table_api_dispatch();

#if notyet
	if (!died)
		kill(pid, SIGTERM);
#endif

	return (0);
}
