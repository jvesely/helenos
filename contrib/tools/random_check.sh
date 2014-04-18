#!/bin/sh

#
# Copyright (c) 2014 Vojtech Horky
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# - Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# - The name of the author may not be used to endorse or promote products
#   derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

LOOPS="$1"
PARALLELISM="$2"
[ -z "$LOOPS" ] && LOOPS=1
[ -z "$PARALLELISM" ] && PARALLELISM=1


run_clean() {
	echo "  Cleaning after previous build." >&2
	make distclean -j$PARALLELISM 2>&1 || exit 1
}

run_random_config() {
	echo "  Preparing random configuration." >&2
	make random-config 2>&1 || exit 1
}

run_make() {
	echo -n "  Building... " >&2
	make -j$PARALLELISM 2>&1
	if [ $? -eq 0 ]; then
		echo "okay." >&2
		return 0
	else
		(
			echo -n "failed ("
			sed -n \
				-e 's#PLATFORM = \(.*\)#\1#p' \
				-e 's#MACHINE = \(.*\)#\1#p' \
				-e 's#COMPILER = \(.*\)#\1#p' \
				Makefile.config \
				| paste '-sd,' | sed -e 's#,#, #g' -e 's#.*#&).#'
		) >&2
		return 1
	fi
}



COUNTER=0
FAILED=0
while [ $COUNTER -lt $LOOPS ]; do
	COUNTER=$(( $COUNTER + 1 ))
	echo "Try #$COUNTER ($FAILED failures):" >&2
	(
		run_clean
		run_random_config
		run_make
		exit $?
	) >random_run_$COUNTER.log
	if [ $? -ne 0 ]; then
		tail -n 10 random_run_$COUNTER.log | sed 's#.*#    | &#'
		FAILED=$(( FAILED + 1 ))
	fi
	cp Makefile.config random_run_$COUNTER.Makefile.config
	cp config.h random_run_$COUNTER.config.h	
done

echo "Out of $LOOPS tries, $FAILED configurations failed to compile." >&2
