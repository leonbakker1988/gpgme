/* types.h -  Some type definitions
 *	Copyright (C) 2000 Werner Koch (dd9jn)
 *      Copyright (C) 2001 g10 Code GmbH
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

#ifndef TYPES_H
#define TYPES_H

#include "gpgme.h"  /* external objects and prototypes */

#ifndef HAVE_BYTE_TYPEDEF
typedef unsigned char byte;
#endif
#ifndef HAVE_ULONG_TYPEDEF
typedef unsigned long ulong;
#endif


typedef enum {
    GPGME_DATA_MODE_NONE  = 0,
    GPGME_DATA_MODE_IN    = 1,
    GPGME_DATA_MODE_OUT   = 2,
    GPGME_DATA_MODE_INOUT = 3
} GpgmeDataMode;


/*
 * Declaration of internal objects
 */

/*-- engine.c --*/
struct engine_object_s;
typedef struct engine_object_s *EngineObject;

/*-- engine-gpgsm.c --*/
struct gpgsm_object_s;
typedef struct gpgsm_object_s *GpgsmObject;

/*-- rungpg.c --*/
struct gpg_object_s;
typedef struct gpg_object_s *GpgObject;

/*-- verify.c --*/
struct verify_result_s;
typedef struct verify_result_s *VerifyResult;

/*-- decrypt.c --*/
struct decrypt_result_s;
typedef struct decrypt_result_s *DecryptResult;

/*-- sign.c --*/
struct sign_result_s;
typedef struct sign_result_s *SignResult;

/*-- encrypt.c --*/
struct encrypt_result_s;
typedef struct encrypt_result_s *EncryptResult;

/*-- passphrase.c --*/
struct passphrase_result_s;
typedef struct passphrase_result_s *PassphraseResult;

/*-- key.c --*/


#endif /* TYPES_H */




