/* rungpg.c 
 *	Copyright (C) 2000 Werner Koch (dd9jn)
 *
 * This file is part of GPGME.
 *
 * GPGME is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GPGME is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include "gpgme.h"
#include "util.h"
#include "ops.h"
#include "wait.h"
#include "rungpg.h"
#include "context.h"  /*temp hack until we have GpmeData methods to do I/O */

#include "status-table.h"

/* This type is used to build a list of gpg arguments and
 * data sources/sinks */
struct arg_and_data_s {
    struct arg_and_data_s *next;
    GpgmeData data;  /* If this is not NULL .. */
    int dup_to;
    char arg[1];     /* .. this is used */
};

struct fd_data_map_s {
    GpgmeData data;
    int inbound;  /* true if this is used for reading from gpg */
    int dup_to;
    int fd;       /* the fd to use */
    int peer_fd;  /* the outher side of the pipe */
};


struct gpg_object_s {
    struct arg_and_data_s *arglist;
    struct arg_and_data_s **argtail;
    int arg_error;  

    struct {
        int fd[2];  
        size_t bufsize;
        char *buffer;
        size_t readpos;
        int eof;
        GpgStatusHandler fnc;
        void *fnc_value;
    } status;

    char **argv;  
    struct fd_data_map_s *fd_data_map;

    pid_t pid; 

    int running;
    int exit_status;
    int exit_signal;
};

static void kill_gpg ( GpgObject gpg );
static void free_argv ( char **argv );
static int gpg_status_handler ( void *opaque, pid_t pid, int fd );
static int gpg_inbound_handler ( void *opaque, pid_t pid, int fd );
static int gpg_outbound_handler ( void *opaque, pid_t pid, int fd );

static GpgmeError read_status ( GpgObject gpg );



GpgmeError
_gpgme_gpg_new_object ( GpgObject *r_gpg )
{
    GpgObject gpg;
    char buf[20];
    int rc = 0;

    gpg = xtrycalloc ( 1, sizeof *gpg );
    if ( !gpg ) {
        rc = mk_error (Out_Of_Core);
        goto leave;
    }
    gpg->argtail = &gpg->arglist;

    gpg->status.fd[0] = -1;
    gpg->status.fd[1] = -1;

    /* The name of the beast will be gpg - so put it into argv[0] */
    _gpgme_gpg_add_arg ( gpg, "gpg" );

    /* allocate the read buffer for the status pipe */
    gpg->status.bufsize = 1024;
    gpg->status.readpos = 0;
    gpg->status.buffer = xtrymalloc (gpg->status.bufsize);
    if (!gpg->status.buffer) {
        rc = mk_error (Out_Of_Core);
        goto leave;
    }
    /* In any case we need a status pipe - create it right here  and
     * don't handle it with our generic GpgmeData mechanism */
    if (pipe (gpg->status.fd) == -1) {
        rc = mk_error (Pipe_Error);
        goto leave;
    }
    gpg->status.eof = 0;
    _gpgme_gpg_add_arg ( gpg, "--status-fd" );
    sprintf ( buf, "%d", gpg->status.fd[1]);
    _gpgme_gpg_add_arg ( gpg, buf );

 leave:
    if (rc) {
        _gpgme_gpg_release_object (gpg);
        *r_gpg = NULL;
    }
    else
        *r_gpg = gpg;
    return rc;
}

void
_gpgme_gpg_release_object ( GpgObject gpg )
{
    if ( !gpg )
        return;
    xfree (gpg->status.buffer);
    if ( gpg->argv )
        free_argv (gpg->argv);
    if (gpg->status.fd[0] != -1 )
        close (gpg->status.fd[0]);
    if (gpg->status.fd[1] != -1 )
        close (gpg->status.fd[1]);
    /* fixme: walk over the data map and close all fds */
    xfree (gpg->fd_data_map);
    kill_gpg (gpg); /* fixme: should be done asyncronously */
    xfree (gpg);
}

static void
kill_gpg ( GpgObject gpg )
{
    if ( gpg->running ) {
        /* still running? Must send a killer */
        kill ( gpg->pid, SIGTERM);
        sleep (2);
        if ( !waitpid (gpg->pid, NULL, WNOHANG) ) {
            /* pay the murderer better and then forget about it */
            kill (gpg->pid, SIGKILL);
        }
        gpg->running = 0;
    }
}


GpgmeError
_gpgme_gpg_add_arg ( GpgObject gpg, const char *arg )
{
    struct arg_and_data_s *a;

    assert (gpg);
    assert (arg);
    a = xtrymalloc ( sizeof *a + strlen (arg) );
    if ( !a ) {
        gpg->arg_error = 1;
        return mk_error(Out_Of_Core);
    }
    a->next = NULL;
    a->data = NULL;
    a->dup_to = -1;
    strcpy ( a->arg, arg );
    *gpg->argtail = a;
    gpg->argtail = &a->next;
    return 0;
}

GpgmeError
_gpgme_gpg_add_data ( GpgObject gpg, GpgmeData data, int dup_to )
{
    struct arg_and_data_s *a;

    assert (gpg);
    assert (data);
    a = xtrymalloc ( sizeof *a - 1 );
    if ( !a ) {
        gpg->arg_error = 1;
        return mk_error(Out_Of_Core);
    }
    a->next = NULL;
    a->data = data;
    a->dup_to = dup_to;
    *gpg->argtail = a;
    gpg->argtail = &a->next;
    return 0;
}

/*
 * Note, that the status_handler is allowed to modifiy the args value
 */
void
_gpgme_gpg_set_status_handler ( GpgObject gpg,
                                GpgStatusHandler fnc, void *fnc_value ) 
{
    assert (gpg);
    gpg->status.fnc = fnc;
    gpg->status.fnc_value = fnc_value;
}

static void
free_argv ( char **argv )
{
    int i;

    for (i=0; argv[i]; i++ )
        xfree (argv[i]);
    xfree (argv);
}


static GpgmeError
build_argv ( GpgObject gpg )
{
    struct arg_and_data_s *a;
    struct fd_data_map_s *fd_data_map;
    size_t datac=0, argc=0;  
    char **argv;
       
    if ( gpg->argv ) {
        free_argv ( gpg->argv );
        gpg->argv = NULL;
    }
    /* fixme: release fd_data_map */

    for ( a=gpg->arglist; a; a = a->next ) {
        argc++;
        if (a->data) {
            fprintf (stderr, "build_argv: data\n" );
            datac++;
        }
        else
            fprintf (stderr, "build_argv: arg=`%s'\n", a->arg );
    }

    argv = xtrycalloc ( argc+1, sizeof *argv );
    if (!argv)
        return mk_error (Out_Of_Core);
    fd_data_map = xtrycalloc ( datac+1, sizeof *fd_data_map );
    if (!fd_data_map) {
        free_argv (argv);
        return mk_error (Out_Of_Core);
    }

    argc = datac = 0;
    for ( a=gpg->arglist; a; a = a->next ) {
        if ( a->data ) {
            switch ( _gpgme_query_data_mode (a->data) ) {
              case GPGME_DATA_MODE_NONE:
              case GPGME_DATA_MODE_INOUT:
                xfree (fd_data_map);
                free_argv (argv);
                return mk_error (Invalid_Mode);
              case GPGME_DATA_MODE_IN:
                /* create a pipe to read from gpg */
                fd_data_map[datac].inbound = 1;
                break;
              case GPGME_DATA_MODE_OUT:
                /* create a pipe to pass it down to gpg */
                fd_data_map[datac].inbound = 0;
                break;
            }

            switch ( gpgme_query_data_type (a->data) ) {
              case GPGME_DATA_TYPE_NONE:
                if ( fd_data_map[datac].inbound )
                    break;  /* allowed */
                xfree (fd_data_map);
                free_argv (argv);
                return mk_error (Invalid_Type);
              case GPGME_DATA_TYPE_MEM:
                break;
              case GPGME_DATA_TYPE_FD:
              case GPGME_DATA_TYPE_FILE:
                xfree (fd_data_map);
                free_argv (argv);
                return mk_error (Not_Implemented);
            }
  
            /* create a pipe */
            {   
                int fds[2];
                
                if (pipe (fds) == -1) {
                    xfree (fd_data_map);
                    free_argv (argv);
                    return mk_error (Pipe_Error);
                }
                /* if the data_type is FD, we have to do a dup2 here */
                if (fd_data_map[datac].inbound) {
                    fd_data_map[datac].fd       = fds[0];
                    fd_data_map[datac].peer_fd  = fds[1];
                }
                else {
                    fd_data_map[datac].fd       = fds[1];
                    fd_data_map[datac].peer_fd  = fds[0];
                }
            }
            fd_data_map[datac].data = a->data;
            fd_data_map[datac].dup_to = a->dup_to;
            datac++;
        }
        else {
            argv[argc] = xtrystrdup ( a->arg );
            if (!argv[argc]) {
                xfree (fd_data_map);
                free_argv (argv);
                return mk_error (Out_Of_Core);
            }
        }
        argc++;
    }

    gpg->argv = argv;
    gpg->fd_data_map = fd_data_map;
    return 0;
}

GpgmeError
_gpgme_gpg_spawn( GpgObject gpg, void *opaque )
{
    int rc;
    int i;
    pid_t pid;

    if ( !gpg )
        return mk_error (Invalid_Value);

    /* Kludge, so that we don't need to check the return code of
     * all the gpgme_gpg_add_arg().  we bail out here instead */
    if ( gpg->arg_error )
        return mk_error (Out_Of_Core);

    rc = build_argv ( gpg );
    if ( rc )
        return rc;

    fflush (stderr);
    pid = fork ();
    if (pid == -1) {
        return mk_error (Exec_Error);
    }
        
    if ( !pid ) { /* child */
        for (i=0; gpg->fd_data_map[i].data; i++ ) {
            close (gpg->fd_data_map[i].fd);
            gpg->fd_data_map[i].fd = -1;
            if ( gpg->fd_data_map[i].dup_to != -1 ) {
                if ( dup2 (gpg->fd_data_map[i].peer_fd,
                           gpg->fd_data_map[i].dup_to ) == -1 ) {
                    fprintf (stderr, "dup2 failed in child: %s\n",
                             strerror (errno));
                    _exit (8);
                }
                close ( gpg->fd_data_map[i].peer_fd );
            }
        }
        
        if ( gpg->status.fd[0] != -1 )
            close (gpg->status.fd[0]);

        /* FIXME: dup /dev/null to stdin if nothing is connected to stdin */
        execv ("/usr/local/bin/gpg", gpg->argv );
        fprintf (stderr,"exec of gpg failed\n"); fflush (stderr);
        _exit (8);
    }
    gpg->pid = pid;

    if ( gpg->status.fd[1] != -1 )
        close (gpg->status.fd[1]);
    if ( _gpgme_register_pipe_handler ( opaque, gpg_status_handler,
                                        gpg, pid, gpg->status.fd[0], 1 ) ) {
        /* FIXME: kill the child */
        return mk_error (General_Error);

    }
    for (i=0; gpg->fd_data_map[i].data; i++ ) {
        close (gpg->fd_data_map[i].peer_fd);
        gpg->fd_data_map[i].peer_fd = -1;
        if ( _gpgme_register_pipe_handler (
                 opaque, 
                 gpg->fd_data_map[i].inbound?
                       gpg_inbound_handler:gpg_outbound_handler,
                 gpg->fd_data_map[i].data,
                 pid, gpg->fd_data_map[i].fd,
                 gpg->fd_data_map[i].inbound )
           ) {
            /* FIXME: kill the child */
            return mk_error (General_Error);
        }
    }

    /* fixme: check what data we can release here */

    gpg->running = 1;
    return 0;

}


static int
gpg_inbound_handler ( void *opaque, pid_t pid, int fd )
{
    GpgmeData dh = opaque;
    int nread;
    char buf[200];

    assert ( _gpgme_query_data_mode (dh) == GPGME_DATA_MODE_IN );

    do {
        nread = read (fd, buf, 200 );
    } while ( nread == -1 && errno == EINTR);
    fprintf(stderr, "inbound on fd %d: nread=%d\n", fd, nread );
    if ( nread < 0 ) {
        fprintf (stderr, "read_mem_data: read failed on fd %d (n=%d): %s\n",
                 fd, nread, strerror (errno) );
        return 1;
    }
    else if (!nread)
        return 1; /* eof */
    

    return 0;
}


static int
write_mem_data ( GpgmeData dh, int fd )
{
    size_t nbytes;
    int  nwritten; 

    nbytes = dh->len - dh->readpos;
    if ( !nbytes ) {
        close (fd);
        return 1;
    }
    
    do {
        nwritten = write ( fd, dh->data+dh->readpos, nbytes );
    } while ( nwritten == -1 && errno == EINTR );
    if ( nwritten < 1 ) {
        fprintf (stderr, "write_mem_data: write failed on fd %d (n=%d): %s\n",
                 fd, nwritten, strerror (errno) );
        close (fd);
        return 1;
    }

    dh->readpos += nwritten;
    return 0;
}


static int
gpg_outbound_handler ( void *opaque, pid_t pid, int fd )
{
    GpgmeData dh = opaque;

    assert ( _gpgme_query_data_mode (dh) == GPGME_DATA_MODE_OUT );
    switch ( gpgme_query_data_type (dh) ) {
      case GPGME_DATA_TYPE_MEM:
        if ( write_mem_data ( dh, fd ) )
            return 1; /* ready */
        break;
      default:
        assert (0);
    }


    return 0;
}



static int
gpg_status_handler ( void *opaque, pid_t pid, int fd )
{
    GpgObject gpg = opaque;
    int rc = 0;

    assert ( fd == gpg->status.fd[0] );
    rc = read_status ( gpg );
    if ( rc ) {
        fprintf (stderr, "gpg_handler: read_status problem %d\n - stop", rc);
        return 1;
    }

    return gpg->status.eof;
}


static int
status_cmp (const struct status_table_s *a, const struct status_table_s *b)
{
    return strcmp (a->name, b->name);
}



/*
 * Handle the status output of GnuPG.  This function does read entire
 * lines and passes them as C strings to the callback function (we can
 * use C Strings because the status output is always UTF-8 encoded).
 * Of course we have to buffer the lines to cope with long lines
 * e.g. with a large user ID.  Note: We can optimize this to only cope
 * with status line code we know about and skip all other stuff
 * without buffering (i.e. without extending the buffer).  */
static GpgmeError
read_status ( GpgObject gpg )
{
    char *p;
    int nread;
    size_t bufsize = gpg->status.bufsize; 
    char *buffer = gpg->status.buffer;
    size_t readpos = gpg->status.readpos; 

    assert (buffer);
    if (bufsize - readpos < 256) { 
        /* need more room for the read */
        bufsize += 1024;
        buffer = xtryrealloc (buffer, bufsize);
        if ( !buffer ) 
            return mk_error (Out_Of_Core);
    }
    

    do { 
        nread = read ( gpg->status.fd[0], buffer+readpos, bufsize-readpos );
    } while (nread == -1 && errno == EINTR);

    if (nread == -1)
        return mk_error(Read_Error);

    if (!nread) {
        gpg->status.eof = 1;
        if (gpg->status.fnc)
            gpg->status.fnc ( gpg->status.fnc_value, STATUS_EOF, "" );
        return 0;
    }

    while (nread > 0) {
        for (p = buffer + readpos; nread; nread--, p++) {
            if ( *p == '\n' ) {
                /* (we require that the last line is terminated by a LF) */
                *p = 0;
                fprintf (stderr, "read_status: `%s'\n", buffer);
                if (!strncmp (buffer, "[GNUPG:] ", 9 )
                    && buffer[9] >= 'A' && buffer[9] <= 'Z'
                    && gpg->status.fnc ) {
                    struct status_table_s t, *r;
                    char *rest;

                    rest = strchr (buffer+9, ' ');
                    if ( !rest )
                        rest = p; /* set to an empty string */
                    else
                        *rest++ = 0;
                    
                    t.name = buffer+9;
                    /* (the status table as one extra element) */
                    r = bsearch ( &t, status_table, DIM(status_table)-1,
                                  sizeof t, status_cmp );
                    if ( r ) {
                        gpg->status.fnc ( gpg->status.fnc_value, 
                                          r->code, rest);
                    }
                }
                /* To reuse the buffer for the next line we have to
                 * shift the remaining data to the buffer start and
                 * restart the loop Hmmm: We can optimize this
                 * function by looking forward in the buffer to see
                 * whether a second complete line is available and in
                 * this case avoid the memmove for this line.  */
                nread--; p++;
                if (nread)
                    memmove (buffer, p, nread);
                readpos = 0;
                break; /* the for loop */
            }
            else
                readpos++;
        }
    } 

    /* Update the gpg object.  */
    gpg->status.bufsize = bufsize;
    gpg->status.buffer = buffer;
    gpg->status.readpos = readpos;
    return 0;
}


