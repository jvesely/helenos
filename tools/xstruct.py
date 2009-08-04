#
# Copyright (c) 2008 Martin Decky
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
"""
Convert descriptive structure definitions to structure object
"""

import struct

class Struct:
	def size(self):
		return struct.calcsize(self._format_)
	
	def pack(self):
		args = []
		for variable in self._args_:
			if (isinstance(self.__dict__[variable], list)):
				for item in self.__dict__[variable]:
					args.append(item)
			else:
				args.append(self.__dict__[variable])
		
		return struct.pack(self._format_, *args)

def create(definition):
	"Create structure object"
	
	tokens = definition.split(None)
	
	# Initial byte order tag
	format = {
		"little:":  lambda: "<",
		"big:":     lambda: ">",
		"network:": lambda: "!"
	}[tokens[0]]()
	inst = Struct()
	args = []
	
	# Member tags
	comment = False
	variable = None
	for token in tokens[1:]:
		if (comment):
			if (token == "*/"):
				comment = False
			continue
		
		if (token == "/*"):
			comment = True
			continue
		
		if (variable != None):
			subtokens = token.split("[")
			
			if (len(subtokens) > 1):
				format += "%d" % int(subtokens[1].split("]")[0])
			
			format += variable
			
			inst.__dict__[subtokens[0]] = None
			args.append(subtokens[0])
			
			variable = None
			continue
		
		if (token[0:8] == "padding["):
			size = token[8:].split("]")[0]
			format += "%dx" % int(size)
			continue
		
		variable = {
			"char":     lambda: "s",
			"uint8_t":  lambda: "B",
			"uint16_t": lambda: "H",
			"uint32_t": lambda: "L",
			"uint64_t": lambda: "Q",
			
			"int8_t":   lambda: "b",
			"int16_t":  lambda: "h",
			"int32_t":  lambda: "l",
			"int64_t":  lambda: "q"
		}[token]()
	
	inst.__dict__['_format_'] = format
	inst.__dict__['_args_'] = args
	return inst
