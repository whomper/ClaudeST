/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * replay.c - runs a fuzz target on the files it is given, without
 * libFuzzer: linked with any C compiler's AddressSanitizer and UBSan, it
 * puts the seeds and the inputs that once broke a parser (tests/fuzz/found)
 * in an app's make test. A directory stands for every file in it; an
 * input that takes longer than libFuzzer's -timeout=10 stops the run, as
 * one that never ends would.
 *
 *     build/replay_imap tests/fuzz/seeds/imap tests/fuzz/found/imap
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n);

static long files;
static const char *now;

static void too_long(int sig)
{
	static const char msg[] = "replay: still at it after 10 s: ";
	(void)sig;
	(void)!write(2, msg, sizeof(msg) - 1);
	(void)!write(2, now, strlen(now));
	(void)!write(2, "\n", 1);
	_exit(2);
}

static int run(const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned char *d;
	long n;
	if (!f) {
		perror(path);
		return 0;
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	/* a copy of its own, so that reading past the end is caught */
	d = malloc(n ? n : 1);
	if (!d || fread(d, 1, n, f) != (size_t)n) {
		fprintf(stderr, "%s: can't read it\n", path);
		fclose(f);
		free(d);
		return 0;
	}
	fclose(f);
	now = path;
	alarm(10);
	LLVMFuzzerTestOneInput(d, (size_t)n);
	alarm(0);
	free(d);
	files++;
	return 1;
}

static int walk(const char *path)
{
	struct stat st;
	DIR *dir;
	struct dirent *e;
	char sub[1024];
	int ok = 1;
	if (stat(path, &st) != 0) {
		perror(path);
		return 0;
	}
	if (!S_ISDIR(st.st_mode))
		return run(path);
	dir = opendir(path);
	if (!dir) {
		perror(path);
		return 0;
	}
	while ((e = readdir(dir)) != 0) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
		ok &= walk(sub);
	}
	closedir(dir);
	return ok;
}

int main(int argc, char **argv)
{
	int i, ok = 1;
	signal(SIGALRM, too_long);
	for (i = 1; i < argc; i++)
		ok &= walk(argv[i]);
	printf("%s: %ld inputs%s\n", argc > 0 ? argv[0] : "replay", files, ok ? "" : ", and some not read");
	return ok ? 0 : 1;
}
