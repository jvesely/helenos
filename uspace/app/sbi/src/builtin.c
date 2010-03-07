/*
 * Copyright (c) 2010 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
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

/** @file Builtin functions. */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "list.h"
#include "mytypes.h"
#include "os/os.h"
#include "run.h"
#include "stree.h"
#include "strtab.h"
#include "symbol.h"

#include "builtin.h"

static stree_symbol_t *builtin_declare_fun(stree_csi_t *csi, char *name);
static void builtin_fun_add_arg(stree_symbol_t *fun_sym, char *name);
static void builtin_fun_add_vararg(stree_symbol_t *fun_sym, char *name);

static void builtin_write_line(run_t *run);
static void builtin_exec(run_t *run);

static stree_symbol_t *bi_write_line;
static stree_symbol_t *bi_exec;

/** Declare builtin symbols in the program.
 *
 * Declares symbols that will be hooked to builtin interpreter functions.
 */
void builtin_declare(stree_program_t *program)
{
	stree_modm_t *modm;
	stree_csi_t *csi;
	stree_ident_t *ident;
	stree_symbol_t *symbol;

	ident = stree_ident_new();
	ident->sid = strtab_get_sid("Builtin");

	csi = stree_csi_new(csi_class);
	csi->name = ident;
	list_init(&csi->members);

	modm = stree_modm_new(mc_csi);
	modm->u.csi = csi;

	symbol = stree_symbol_new(sc_csi);
	symbol->u.csi = csi;
	symbol->outer_csi = NULL;
	csi->symbol = symbol;

	list_append(&program->module->members, modm);

	/* Declare builtin functions. */

	bi_write_line = builtin_declare_fun(csi, "WriteLine");
	builtin_fun_add_arg(bi_write_line, "arg");

	bi_exec = builtin_declare_fun(csi, "Exec");
	builtin_fun_add_vararg(bi_exec, "args");
}

void builtin_run_fun(run_t *run, stree_symbol_t *fun_sym)
{
#ifdef DEBUG_RUN_TRACE
	printf("Run builtin function.\n");
#endif
	if (fun_sym == bi_write_line) {
		builtin_write_line(run);
	} else if (fun_sym == bi_exec) {
		builtin_exec(run);
	} else {
		assert(b_false);
	}
}

/** Declare a builtin function in @a csi. */
static stree_symbol_t *builtin_declare_fun(stree_csi_t *csi, char *name)
{
	stree_ident_t *ident;
	stree_fun_t *fun;
	stree_csimbr_t *csimbr;
	stree_symbol_t *symbol;

	ident = stree_ident_new();
	ident->sid = strtab_get_sid(name);

	fun = stree_fun_new();
	fun->name = ident;
	fun->body = NULL;
	list_init(&fun->args);

	csimbr = stree_csimbr_new(csimbr_fun);
	csimbr->u.fun = fun;

	symbol = stree_symbol_new(sc_fun);
	symbol->u.fun = fun;
	symbol->outer_csi = csi;
	fun->symbol = symbol;

	list_append(&csi->members, csimbr);

	return symbol;
}

/** Add one formal parameter to function. */
static void builtin_fun_add_arg(stree_symbol_t *fun_sym, char *name)
{
	stree_fun_arg_t *fun_arg;
	stree_fun_t *fun;

	fun = symbol_to_fun(fun_sym);
	assert(fun != NULL);

	fun_arg = stree_fun_arg_new();
	fun_arg->name = stree_ident_new();
	fun_arg->name->sid = strtab_get_sid(name);
	fun_arg->type = NULL; /* XXX */

	list_append(&fun->args, fun_arg);
}

/** Add variadic formal parameter to function. */
static void builtin_fun_add_vararg(stree_symbol_t *fun_sym, char *name)
{
	stree_fun_arg_t *fun_arg;
	stree_fun_t *fun;

	fun = symbol_to_fun(fun_sym);
	assert(fun != NULL);

	fun_arg = stree_fun_arg_new();
	fun_arg->name = stree_ident_new();
	fun_arg->name->sid = strtab_get_sid(name);
	fun_arg->type = NULL; /* XXX */

	fun->varg = fun_arg;
}

static void builtin_write_line(run_t *run)
{
	rdata_var_t *var;

#ifdef DEBUG_RUN_TRACE
	printf("Called Builtin.WriteLine()\n");
#endif
	var = run_local_vars_lookup(run, strtab_get_sid("arg"));
	assert(var);

	switch (var->vc) {
	case vc_int:
		printf("%d\n", var->u.int_v->value);
		break;
	case vc_string:
		printf("%s\n", var->u.string_v->value);
		break;
	default:
		printf("Unimplemented: writeLine() with unsupported type.\n");
		exit(1);
	}
}

/** Start an executable and wait for it to finish. */
static void builtin_exec(run_t *run)
{
	rdata_var_t *args;
	rdata_var_t *var;
	rdata_array_t *array;
	rdata_var_t *arg;
	int idx, dim;
	char **cmd;

#ifdef DEBUG_RUN_TRACE
	printf("Called Builtin.Exec()\n");
#endif
	args = run_local_vars_lookup(run, strtab_get_sid("args"));

	assert(args);
	assert(args->vc == vc_ref);

	var = args->u.ref_v->vref;
	assert(var->vc == vc_array);

	array = var->u.array_v;
	assert(array->rank == 1);
	dim = array->extent[0];

	if (dim == 0) {
		printf("Error: Builtin.Exec() expects at least one argument.\n");
		exit(1);
	}

	cmd = calloc(dim + 1, sizeof(char *));
	if (cmd == NULL) {
		printf("Memory allocation failed.\n");
		exit(1);
	}

	for (idx = 0; idx < dim; ++idx) {
		arg = array->element[idx];
		if (arg->vc != vc_string) {
			printf("Error: Argument to Builtin.Exec() must be "
			    "string (found %d).\n", arg->vc);
			exit(1);
		}

		cmd[idx] = arg->u.string_v->value;
	}

	cmd[dim] = '\0';

	if (os_exec(cmd) != EOK) {
		printf("Error: Exec failed.\n");
		exit(1);
	}
}
