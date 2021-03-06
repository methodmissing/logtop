/*
 * Copyright (c) 2010 Julien Palard.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#include "main.h"

env_t gl_env;

/**
 * Basic sig handling using sigaction.
 * Reset action to SIG_DFL if act is NULL.
 */
void setup_sighandler(int signum, int flags, void (*act)(int))
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    if (act != NULL)
        sa.sa_handler = act;
    else
        sa.sa_handler = SIG_DFL;
    sa.sa_flags = flags;
    sigaction(signum, &sa, NULL);
}

static void update_display(int sig __attribute__((unused)))
{
    time_t     current_time;

    if (gl_env.quiet)
        return ;
    current_time = time(NULL);
    if (current_time < gl_env.last_update_time + gl_env.interval)
        return ;
    gl_env.last_update_time = current_time;
    if (gl_env.line_by_line)
        stdout_update(gl_env.line_by_line, 1);
    else
        curses_update();
    alarm(1);
}

static void run(struct logtop *logtop)
{
    char    *string;
    size_t  size;
    ssize_t str_length;

    string = NULL;
    size = 0;
    while ((str_length = getline(&string, &size, stdin)) != -1)
    {
        while (str_length > 0 && (string[str_length - 1] == '\n'
                                  || string[str_length - 1] == '\r'))
        {
            string[str_length - 1] = '\0';
            str_length -= 1;
        }
        logtop_feed(logtop, string);
    }
    if (string != NULL)
        free(string);
}

static void usage_and_exit(int exit_code)
{
    fprintf(exit_code == EXIT_SUCCESS ? stdout : stderr,
            "Usage: tail -f something | logtop [OPTIONS]\n"
            "    -s, --size=NUM         Number of log line to keep in memory\n"
            "                           Defaults to : "
            STRINGIFY(DEFAULT_HISTORY_SIZE) "\n"
            "    -q, --quiet            Quiet, only display a top 10 at exit.\n"
            "    -l, --line-by-line=NUM Print result line by line\n"
            "                           in a machine friendly format,\n"
            "                           NUM: quantity of result by line.\n");
    fprintf(exit_code == EXIT_SUCCESS ? stdout : stderr,
            "    -i, --interval=NUM     Interval between graphical updates,\n"
            "                           in seconds. Defaults to 1.\n"
            "\n"
            "  Line by line format is : [%%d %%f %%s\\t]*\\n\n"
            "    %%d : Number of occurences\n"
            "    %%f : Frequency of apparition\n"
            "    %%s : String (Control chars replaced by dots).\n"
            "\n");
    exit(exit_code);
}

static void version_and_exit(void)
{
#define stringify(v) #v
#define concat_version(v) "logtop v" stringify(v) "\n"
    fprintf(stdout, concat_version(VERSION));
    exit(EXIT_SUCCESS);
}

static void parse_args(int ac, char **av)
{
    int c;

    gl_env.history_size = 0;
    gl_env.quiet = 0;
    gl_env.last_update_time = 0;
    gl_env.line_by_line = 0;
    gl_env.interval = 1;
    while (1)
    {
        int option_index = 0;
        static struct option long_options[] = {
            {"size", 1, 0, 's'},
            {"quiet", 0, 0, 'q'},
            {"help", 0, 0, 'h'},
            {"version", 0, 0, 'v'},
            {"line-by-line", 1, 0, 'l'},
            {"interval", 1, 0, 'i'},
            {0, 0, 0, 0}
        };
        c = getopt_long(ac, av, "qhvl:s:i:",
                        long_options, &option_index);
        if (c == -1)
            break;
        switch (c)
        {
            case 'l':
                gl_env.line_by_line = atoi(optarg);
                break;
            case 'q':
                gl_env.quiet = 1;
                break;
            case 's':
                gl_env.history_size = atoi(optarg);
                break;
            case 'i':
                gl_env.interval = atoi(optarg);
                break;
            case 'v':
                version_and_exit();
            case 'h':
                usage_and_exit(EXIT_SUCCESS);
            default:
                usage_and_exit(EXIT_FAILURE);
        }
    }
    if (isatty(fileno(stdin)))
        usage_and_exit(EXIT_FAILURE);
    if (gl_env.history_size == 0)
        gl_env.history_size = DEFAULT_HISTORY_SIZE;
}

static void at_exit(void)
{
    if (!gl_env.quiet && !gl_env.line_by_line)
        curses_release();
    if (gl_env.line_by_line)
        stdout_update(gl_env.line_by_line, 1);
    else
        stdout_update(10, 0);
    delete_logtop(gl_env.logtop);
    fflush(NULL);
}

static void on_sigint(int sig)
{
    setup_sighandler(SIGINT, 0, NULL);
    at_exit();
    kill(getpid(), sig);
}

int main(int ac, char **av)
{
    parse_args(ac, av);
    setup_sighandler(SIGINT, 0, on_sigint);
    setup_sighandler(SIGALRM, SA_RESTART, update_display);
    alarm(1);
    gl_env.last_update_time = time(NULL);
    gl_env.logtop = new_logtop(gl_env.history_size);
    if (!gl_env.quiet && !gl_env.line_by_line)
        curses_setup();
    run(gl_env.logtop);
    at_exit();
    return EXIT_SUCCESS;
}
