/* sign.c -  signing functions
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

#include "util.h"
#include "context.h"
#include "ops.h"


struct  sign_result_s {
    int no_passphrase;
    int okay;
};


void
_gpgme_release_sign_result ( SignResult res )
{
    xfree (res);
}



static void
sign_status_handler ( GpgmeCtx ctx, GpgStatusCode code, char *args )
{
    if ( ctx->out_of_core )
        return;
    if ( ctx->result_type == RESULT_TYPE_NONE ) {
        assert ( !ctx->result.sign );
        ctx->result.sign = xtrycalloc ( 1, sizeof *ctx->result.sign );
        if ( !ctx->result.sign ) {
            ctx->out_of_core = 1;
            return;
        }
        ctx->result_type = RESULT_TYPE_SIGN;
    }
    assert ( ctx->result_type == RESULT_TYPE_SIGN );

    switch (code) {
      case STATUS_EOF:
        break;

      case STATUS_NEED_PASSPHRASE:
      case STATUS_NEED_PASSPHRASE_SYM:
        fprintf (stderr, "Ooops: Need a passphrase -  use the agent\n");
        break;

      case STATUS_MISSING_PASSPHRASE:
        fprintf (stderr, "Missing passphrase - stop\n");;
        ctx->result.sign->no_passphrase = 1;
        break;

      case STATUS_SIG_CREATED:
        /* fixme: we have no error return for multible signatures */
        ctx->result.sign->okay =1;
        break;

      default:
        fprintf (stderr, "sign_status: code=%d not handled\n", code );
        break;
    }
}



GpgmeError
gpgme_op_sign_start ( GpgmeCtx c, GpgmeData in, GpgmeData out )
{
    int rc = 0;
    int i;

    fail_on_pending_request( c );
    c->pending = 1;

    _gpgme_release_result (c);
    c->out_of_core = 0;

    /* do some checks */
    assert ( !c->gpg );
        
    /* create a process object */
    rc = _gpgme_gpg_new ( &c->gpg );
    if (rc)
        goto leave;

    _gpgme_gpg_set_status_handler ( c->gpg, sign_status_handler, c );

    /* build the commandline */
    _gpgme_gpg_add_arg ( c->gpg, "--sign" );
    _gpgme_gpg_add_arg ( c->gpg, "--detach" );
    if ( c->use_armor )
        _gpgme_gpg_add_arg ( c->gpg, "--armor" );
    if ( c->use_textmode )
        _gpgme_gpg_add_arg ( c->gpg, "--textmode" );
    for ( i=0; i < c->verbosity; i++ )
        _gpgme_gpg_add_arg ( c->gpg, "--verbose" );
    
    /* Check the supplied data */
    if ( gpgme_data_get_type (in) == GPGME_DATA_TYPE_NONE ) {
        rc = mk_error (No_Data);
        goto leave;
    }
    _gpgme_data_set_mode (in, GPGME_DATA_MODE_OUT );
    if ( !out || gpgme_data_get_type (out) != GPGME_DATA_TYPE_NONE ) {
        rc = mk_error (Invalid_Value);
        goto leave;
    }
    _gpgme_data_set_mode (out, GPGME_DATA_MODE_IN );

    /* Tell the gpg object about the data */
    _gpgme_gpg_add_data ( c->gpg, in, 0 );
    _gpgme_gpg_add_data ( c->gpg, out, 1 );

    /* and kick off the process */
    rc = _gpgme_gpg_spawn ( c->gpg, c );

 leave:
    if (rc) {
        c->pending = 0; 
        _gpgme_gpg_release ( c->gpg ); c->gpg = NULL;
    }
    return rc;
}


/**
 * gpgme_op_sign:
 * @c: The context
 * @in: Data to be signed
 * @out: Detached signature
 * 
 * Create a detached signature for @in and write it to @out.
 * The data will be signed using either the default key or the ones
 * defined through @c.
 * 
 * Return value: 0 on success or an error code.
 **/
GpgmeError
gpgme_op_sign ( GpgmeCtx c, GpgmeData in, GpgmeData out )
{
    GpgmeError err = gpgme_op_sign_start ( c, in, out );
    if ( !err ) {
        gpgme_wait (c, 1);
        if ( c->result_type != RESULT_TYPE_SIGN )
            err = mk_error (General_Error);
        else if ( c->out_of_core )
            err = mk_error (Out_Of_Core);
        else {
            assert ( c->result.sign );
            if ( c->result.sign->no_passphrase ) 
                err = mk_error (No_Passphrase);
            else if (!c->result.sign->okay)
                err = mk_error (No_Data); /* Hmmm: choose a better error? */
        }
        c->pending = 0;
    }
    return err;
}








