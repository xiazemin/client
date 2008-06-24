/*
 * libcsync -- a library to sync a replica with another
 *
 * Copyright (c) 2006-2007 by Andreas Schneider <mail@cynapses.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * vim: ts=2 sw=2 et cindent
 */

#include <argp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <csync.h>

#include "csync_auth.h"

enum {
  KEY_DUMMY = 129,
  KEY_EXCLUDE_FILE,
  KEY_CREATE_JOURNAL,
};

const char *argp_program_version = "csync commandline client 0.42";
const char *argp_program_bug_address = "<csync-devel@csync.org>";

/* Program documentation. */
static char doc[] = "csync -- a user level file synchronizer";

/* A description of the arguments we accept. */
static char args_doc[] = "SOURCE DESTINATION";

/* The options we understand. */
static struct argp_option options[] = {
  {
    .name  = "disable-journal",
    .key   = 'd',
    .arg   = NULL,
    .flags = 0,
    .doc   = "Disable the usage and creation of a journal.",
    .group = 0
  },
  {
    .name  = "update",
    .key   = 'u',
    .arg   = NULL,
    .flags = 0,
    .doc   = "Run only the update detection",
    .group = 0
  },
  {
    .name  = "reconcile",
    .key   = 'r',
    .arg   = NULL,
    .flags = 0,
    .doc   = "Run update detection and reconcilation",
    .group = 0
  },
  {
    .name  = "create-journal",
    .key   = KEY_CREATE_JOURNAL,
    .arg   = NULL,
    .flags = 0,
    .doc   = "Run update detection and write the journal (TESTING ONLY!)",
    .group = 0
  },
  {
    .name  = "exclude-file",
    .key   = KEY_EXCLUDE_FILE,
    .arg   = "<file>",
    .flags = 0,
    .doc   = "Add an additional exclude file",
    .group = 0
  },
  {NULL, 0, 0, 0, NULL, 0}
};

/* Used by main to communicate with parse_opt. */
struct argument_s {
  char *args[2]; /* SOURCE and DESTINATION */
  char *exclude_file;
  int disable_journal;
  int create_journal;
  int update;
  int reconcile;
  int propagate;
};

/* Parse a single option. */
static error_t parse_opt (int key, char *arg, struct argp_state *state) {
  /* Get the input argument from argp_parse, which we
   * know is a pointer to our arguments structure.
   */
  struct argument_s *arguments = state->input;

  switch (key) {
    case 'u':
      arguments->create_journal = 0;
      arguments->update = 1;
      arguments->reconcile = 0;
      arguments->propagate = 0;
      break;
    case 'r':
      arguments->create_journal = 0;
      arguments->update = 1;
      arguments->reconcile = 1;
      arguments->propagate = 0;
      break;
    case KEY_EXCLUDE_FILE:
      arguments->exclude_file = strdup(arg);
      break;
    case 'd':
      arguments->disable_journal = 1;
      break;
    case KEY_CREATE_JOURNAL:
      arguments->create_journal = 1;
      arguments->update = 1;
      arguments->reconcile = 0;
      arguments->propagate = 0;
      break;
    case ARGP_KEY_ARG:
      if (state->arg_num >= 2) {
        /* Too many arguments. */
        argp_usage (state);
      }
      arguments->args[state->arg_num] = arg;
      break;
    case ARGP_KEY_END:
      if (state->arg_num < 2) {
        /* Not enough arguments. */
        argp_usage (state);
      }
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }

  return 0;
}

static void csync_auth_fn(char *usr, size_t usrlen, char *pwd, size_t pwdlen) {
  char tmp[256] = {0};

  /* get username */
  snprintf(tmp, 255, "Username: [%s] ", usr);
  csync_text_prompt(tmp, tmp, 255);

  if (tmp[strlen(tmp) - 1] == '\n') {
    tmp[strlen(tmp) - 1] = '\0';
  }

  if (tmp[0] != '\0') {
    strncpy(usr, tmp, usrlen - 1);
  }

  /* get password */
  csync_password_prompt("Password: ", pwd, pwdlen, 0);
}

/* Our argp parser. */
static struct argp argp = {options, parse_opt, args_doc, doc, NULL, NULL, NULL};

int main(int argc, char **argv) {
  int rc = 0;
  CSYNC *csync;

  struct argument_s arguments;

  /* Default values. */
  arguments.exclude_file = NULL;
  arguments.disable_journal = 0;
  arguments.create_journal = 0;
  arguments.update = 1;
  arguments.reconcile = 1;
  arguments.propagate = 1;

  /*
   * Parse our arguments; every option seen by parse_opt will
   * be reflected in arguments.
   */
  argp_parse (&argp, argc, argv, 0, 0, &arguments);

  if (csync_create(&csync, arguments.args[0], arguments.args[1]) < 0) {
    fprintf(stderr, "csync_create: failed\n");
    exit(1);
  }

  csync_set_auth_callback(csync, csync_auth_fn);
  csync_disable_journal(csync);

  if (csync_init(csync) < 0) {
    perror("csync_init");
    rc = 1;
    goto out;
  }

  if (arguments.exclude_file != NULL) {
    if (csync_add_exclude_list(csync, arguments.exclude_file) < 0) {
      fprintf(stderr, "csync_add_exclude_list - %s: %s\n", arguments.exclude_file,
          strerror(errno));
      rc = 1;
      goto out;
    }
  }

  if (arguments.update) {
    if (csync_update(csync) < 0) {
      perror("csync_update");
      rc = 1;
      goto out;
    }
  }

  if (arguments.reconcile) {
    if (csync_reconcile(csync) < 0) {
      perror("csync_reconcile");
      rc = 1;
      goto out;
    }
  }

  if (arguments.propagate) {
    if (csync_propagate(csync) < 0) {
      perror("csync_propagate");
      rc = 1;
      goto out;
    }
  }

  if (arguments.create_journal) {
    csync_set_status(csync, 0xFFFF);
  }

out:
  csync_destroy(csync);

  return rc;
}

