#!/usr/bin/env python3

# Copyright (C) 2016 g10 Code GmbH
#
# This file is part of GPGME.
#
# GPGME is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# GPGME is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General
# Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, see <http://www.gnu.org/licenses/>.

from pyme import core, constants
import support

support.init_gpgme(constants.PROTOCOL_OpenPGP)
c = core.Context()
c.op_trustlist_start("alice", 0)

while True:
    item = c.op_trustlist_next()
    if not item:
        break

    print("l={} k={} t={} o={} v={} u={}".format(
        item.level, item.keyid, item.type, item.owner_trust,
        item.validity, item.name))