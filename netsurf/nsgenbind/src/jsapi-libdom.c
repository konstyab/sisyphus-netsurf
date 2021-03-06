/* binding output generator for jsapi(spidermonkey) to libdom
 *
 * This file is part of nsgenbind.
 * Published under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 */

#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "options.h"
#include "nsgenbind-ast.h"
#include "webidl-ast.h"
#include "jsapi-libdom.h"

#define HDR_COMMENT_SEP "\n * \n * "
#define HDR_COMMENT_PREAMBLE "/* Generated by nsgenbind from %s\n"	\
	" *\n"								\
	" * nsgenbind is published under the MIT Licence.\n"		\
	" * nsgenbind is similar to a compiler is a purely transformative tool which\n" \
	" * explicitly makes no copyright claim on this generated output"



static int webidl_file_cb(struct genbind_node *node, void *ctx)
{
	struct webidl_node **webidl_ast = ctx;
	char *filename;

	filename = genbind_node_gettext(node);

	return webidl_parsefile(filename, webidl_ast);
}

static int
read_webidl(struct genbind_node *genbind_ast, struct webidl_node **webidl_ast)
{
	int res;

	res = genbind_node_foreach_type(genbind_ast,
					 GENBIND_NODE_TYPE_WEBIDLFILE,
					 webidl_file_cb,
					 webidl_ast);

	if (res == 0) {
		res = webidl_intercalate_implements(*webidl_ast);
	}

	/* debug dump of web idl AST */
	if (options->verbose) {
		webidl_ast_dump(*webidl_ast, 0);
	}
	return res;
}


static int webidl_preamble_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;

	fprintf(binding->outfile, "%s", genbind_node_gettext(node));

	return 0;
}

static int webidl_prologue_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;

	fprintf(binding->outfile, "%s", genbind_node_gettext(node));

	return 0;
}

static int webidl_epilogue_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;

	fprintf(binding->outfile, "%s", genbind_node_gettext(node));

	return 0;
}


static int webidl_hdrcomments_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;

	fprintf(binding->outfile,
		HDR_COMMENT_SEP"%s",
		genbind_node_gettext(node));

	return 0;
}

static int webidl_hdrcomment_cb(struct genbind_node *node, void *ctx)
{
	genbind_node_foreach_type(genbind_node_getnode(node),
				   GENBIND_NODE_TYPE_STRING,
				   webidl_hdrcomments_cb,
				   ctx);
	return 0;
}

static int webidl_private_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;
	struct genbind_node *ident_node;
	struct genbind_node *type_node;


	ident_node = genbind_node_find_type(genbind_node_getnode(node),
					    NULL,
					    GENBIND_NODE_TYPE_IDENT);
	if (ident_node == NULL)
		return -1; /* bad AST */

	type_node = genbind_node_find_type(genbind_node_getnode(node),
					    NULL,
					    GENBIND_NODE_TYPE_STRING);
	if (type_node == NULL)
		return -1; /* bad AST */

	fprintf(binding->outfile,
		"        %s%s;\n",
		genbind_node_gettext(type_node),
		genbind_node_gettext(ident_node));

	return 0;
}



/* section output generators */

/** Output the epilogue right at the end of the generated program */
static int
output_epilogue(struct binding *binding)
{
	genbind_node_foreach_type(binding->gb_ast,
				   GENBIND_NODE_TYPE_EPILOGUE,
				   webidl_epilogue_cb,
				   binding);

	fprintf(binding->outfile,"\n\n");

	if (binding->hdrfile) {
		binding->outfile = binding->hdrfile;

		fprintf(binding->outfile,
			"\n\n#endif /* _%s_ */\n",
			binding->hdrguard);

		binding->outfile = binding->srcfile;
	}

	return 0;
}

/** Output the prologue right before the generated function bodies */
static int
output_prologue(struct binding *binding)
{
	/* forward declare property list */
	fprintf(binding->outfile,
		"static JSPropertySpec jsclass_properties[];\n\n");

	genbind_node_foreach_type(binding->gb_ast,
				   GENBIND_NODE_TYPE_PROLOGUE,
				   webidl_prologue_cb,
				   binding);

	fprintf(binding->outfile,"\n\n");

	return 0;
}


static int
output_api_operations(struct binding *binding)
{
	int res = 0;

	/* finalise */
	if (binding->has_private) {
		/* finalizer with private to free */
		fprintf(binding->outfile,
			"static void jsclass_finalize(JSContext *cx, JSObject *obj)\n"
			"{\n"
			"\tstruct jsclass_private *private;\n"
			"\n"
			"\tprivate = JS_GetInstancePrivate(cx, obj, &JSClass_%s, NULL);\n",
			binding->interface);

		if (options->dbglog) {
			fprintf(binding->outfile,
				"\tJSLOG(\"jscontext:%%p jsobject:%%p private:%%p\", cx, obj, private);\n");
			}

		if (binding->finalise != NULL) {
			output_code_block(binding,
				genbind_node_getnode(binding->finalise));
		}

		fprintf(binding->outfile,
			"\tif (private != NULL) {\n"
			"\t\tfree(private);\n"
			"\t}\n"
			"}\n\n");
	} else if (binding->finalise != NULL) {
		/* finaliser without private data */
		fprintf(binding->outfile,
			"static void jsclass_finalize(JSContext *cx, JSObject *obj)\n"
			"{\n");

		if (options->dbglog) {
			fprintf(binding->outfile,
				"\tJSLOG(\"jscontext:%%p jsobject:%%p\", cx, obj);\n");
		}

		output_code_block(binding,
				  genbind_node_getnode(binding->finalise));

		fprintf(binding->outfile,
			"}\n\n");

	}

	/* generate class default property add implementation */
	if (binding->addproperty != NULL) {
		fprintf(binding->outfile,
			"static JSBool JSAPI_PROP(add, JSContext *cx, JSObject *obj, jsval *vp)\n"
			"{\n");

		if (binding->has_private) {

			fprintf(binding->outfile,
				"\tstruct jsclass_private *private;\n"
				"\n"
				"\tprivate = JS_GetInstancePrivate(cx, obj, &JSClass_%s, NULL);\n",
				binding->interface);

			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p private:%%p\", cx, obj, private);\n");
			}
		} else {
			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p\", cx, obj);\n");
			}

		}


		output_code_block(binding,
				  genbind_node_getnode(binding->addproperty));

		fprintf(binding->outfile,
			"\treturn JS_TRUE;\n"
			"}\n\n");
	}

	/* generate class default property delete implementation */
	if (binding->delproperty != NULL) {
		fprintf(binding->outfile,
			"static JSBool JSAPI_PROP(del, JSContext *cx, JSObject *obj, jsval *vp)\n"
			"{\n");

		if (binding->has_private) {

			fprintf(binding->outfile,
				"\tstruct jsclass_private *private;\n"
				"\n"
				"\tprivate = JS_GetInstancePrivate(cx, obj, &JSClass_%s, NULL);\n",
				binding->interface);

			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p private:%%p\", cx, obj, private);\n");
			}
		} else {
			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p\", cx, obj);\n");
			}

		}


		output_code_block(binding,
				  genbind_node_getnode(binding->delproperty));

		fprintf(binding->outfile,
			"\treturn JS_TRUE;\n"
			"}\n\n");
	}

	/* generate class default property get implementation */
	if (binding->getproperty != NULL) {
		fprintf(binding->outfile,
			"static JSBool JSAPI_PROP(get, JSContext *cx, JSObject *obj, jsval *vp)\n"
			"{\n");

		if (binding->has_private) {

			fprintf(binding->outfile,
				"\tstruct jsclass_private *private;\n"
				"\n"
				"\tprivate = JS_GetInstancePrivate(cx, obj, &JSClass_%s, NULL);\n",
				binding->interface);

			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p private:%%p\", cx, obj, private);\n");
			}
		} else {
			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p\", cx, obj);\n");
			}

		}


		output_code_block(binding,
				  genbind_node_getnode(binding->getproperty));

		fprintf(binding->outfile,
			"\treturn JS_TRUE;\n"
			"}\n\n");
	}

	/* generate class default property set implementation */
	if (binding->setproperty != NULL) {
		fprintf(binding->outfile,
			"static JSBool JSAPI_STRICTPROP(set, JSContext *cx, JSObject *obj, jsval *vp)\n"
			"{\n");

		if (binding->has_private) {

			fprintf(binding->outfile,
				"\tstruct jsclass_private *private;\n"
				"\n"
				"\tprivate = JS_GetInstancePrivate(cx, obj, &JSClass_%s, NULL);\n",
				binding->interface);

			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p private:%%p\", cx, obj, private);\n");
			}
		} else {
			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p\", cx, obj);\n");
			}

		}


		output_code_block(binding,
				  genbind_node_getnode(binding->setproperty));

		fprintf(binding->outfile,
			"\treturn JS_TRUE;\n"
			"}\n\n");
	}

	/* generate class enumerate implementation */
	if (binding->enumerate != NULL) {
		fprintf(binding->outfile,
			"static JSBool jsclass_enumerate(JSContext *cx, JSObject *obj)\n"
			"{\n");

		if (binding->has_private) {

			fprintf(binding->outfile,
				"\tstruct jsclass_private *private;\n"
				"\n"
				"\tprivate = JS_GetInstancePrivate(cx, obj, &JSClass_%s, NULL);\n",
				binding->interface);

			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p private:%%p\", cx, obj, private);\n");
			}
		} else {
			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p\", cx, obj);\n");
			}

		}

		output_code_block(binding, genbind_node_getnode(binding->enumerate));

		fprintf(binding->outfile,
			"\treturn JS_TRUE;\n"
			"}\n\n");
	}

	/* generate class resolver implementation */
	if (binding->resolve != NULL) {
		fprintf(binding->outfile,
			"static JSBool jsclass_resolve(JSContext *cx, JSObject *obj, jsval id, uintN flags, JSObject **objp)\n"
			"{\n");

		if (binding->has_private) {

			fprintf(binding->outfile,
				"\tstruct jsclass_private *private;\n"
				"\n"
				"\tprivate = JS_GetInstancePrivate(cx, obj, &JSClass_%s, NULL);\n",
				binding->interface);
			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p private:%%p\", cx, obj, private);\n");
			}
		} else {
			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p\", cx, obj);\n");
			}

		}


		output_code_block(binding, genbind_node_getnode(binding->resolve));

		fprintf(binding->outfile,
			"\treturn JS_TRUE;\n"
			"}\n\n");
	}

	/* generate trace/mark entry */
	if (binding->mark != NULL) {
		fprintf(binding->outfile,
			"static JSAPI_MARKOP(jsclass_mark)\n"
			"{\n");
		if (binding->has_private) {

			fprintf(binding->outfile,
				"\tstruct jsclass_private *private;\n"
				"\n"
				"\tprivate = JS_GetInstancePrivate(JSAPI_MARKCX, obj, &JSClass_%s, NULL);\n",
				binding->interface);

			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p private:%%p\", JSAPI_MARKCX, obj, private);\n");
			}
		} else {
			if (options->dbglog) {
				fprintf(binding->outfile,
					"\tJSLOG(\"jscontext:%%p jsobject:%%p\", JSAPI_MARKCX, obj);\n");
			}

		}

		output_code_block(binding, genbind_node_getnode(binding->mark));

		fprintf(binding->outfile,
			"\tJSAPI_MARKOP_RETURN(JS_TRUE);\n"
			"}\n\n");
	}
	return res;
}

void
output_code_block(struct binding *binding, struct genbind_node *codelist)
{
	struct genbind_node *code_node;

	code_node = genbind_node_find_type(codelist,
					   NULL,
					   GENBIND_NODE_TYPE_CBLOCK);
	if (code_node != NULL) {
		fprintf(binding->outfile,
			"%s\n",
			genbind_node_gettext(code_node));
	}
}



static int
output_forward_declarations(struct binding *binding)
{
	/* forward declare add property */
	if (binding->addproperty != NULL) {
		fprintf(binding->outfile,
			"static JSBool JSAPI_PROP(add, JSContext *cx, JSObject *obj, jsval *vp);\n\n");
	}

	/* forward declare del property */
	if (binding->delproperty != NULL) {
		fprintf(binding->outfile,
			"static JSBool JSAPI_PROP(del, JSContext *cx, JSObject *obj, jsval *vp);\n\n");
	}

	/* forward declare get property */
	if (binding->getproperty != NULL) {
		fprintf(binding->outfile,
			"static JSBool JSAPI_PROP(get, JSContext *cx, JSObject *obj, jsval *vp);\n\n");
	}

	/* forward declare set property */
	if (binding->setproperty != NULL) {
		fprintf(binding->outfile,
			"static JSBool JSAPI_STRICTPROP(set, JSContext *cx, JSObject *obj, jsval *vp);\n\n");
	}

	/* forward declare the enumerate */
	if (binding->enumerate != NULL) {
		fprintf(binding->outfile,
			"static JSBool jsclass_enumerate(JSContext *cx, JSObject *obj);\n\n");
	}

	/* forward declare the resolver */
	if (binding->resolve != NULL) {
		fprintf(binding->outfile,
			"static JSBool jsclass_resolve(JSContext *cx, JSObject *obj, jsval id, uintN flags, JSObject **objp);\n\n");
	}

	/* forward declare the GC mark operation */
	if (binding->mark != NULL) {
		fprintf(binding->outfile,
			"static JSAPI_MARKOP(jsclass_mark);\n\n");
	}

	/* forward declare the finalizer */
	if (binding->has_private || (binding->finalise != NULL)) {
		fprintf(binding->outfile,
			"static void jsclass_finalize(JSContext *cx, JSObject *obj);\n\n");
	}

	return 0;
}



/** generate structure definition for internal class data
 *
 * Every javascript object instance has an internal context to keep
 * track of its state in object terms this would be considered the
 * "this" pointer giving access to the classes members.
 *
 * Member access can be considered protected as all interfaces
 * (classes) and subclasses generated within this binding have access
 * to members.
 *
 * @param binding the binding to generate the structure for.
 * @return 0 on success with output written and -1 on error.
 */
static int
output_private_declaration(struct binding *binding)
{

	if (!binding->has_private) {
		return 0;
	}

	fprintf(binding->outfile, "struct jsclass_private {\n");

	genbind_node_foreach_type(binding->binding_list,
				   GENBIND_NODE_TYPE_BINDING_PRIVATE,
				   webidl_private_cb,
				   binding);

	genbind_node_foreach_type(binding->binding_list,
				   GENBIND_NODE_TYPE_BINDING_INTERNAL,
				   webidl_private_cb,
				   binding);

	fprintf(binding->outfile, "};\n\n");


	return 0;
}

static int
output_preamble(struct binding *binding)
{
	genbind_node_foreach_type(binding->gb_ast,
				   GENBIND_NODE_TYPE_PREAMBLE,
				   webidl_preamble_cb,
				   binding);

	fprintf(binding->outfile,"\n\n");

	if (binding->hdrfile) {
		binding->outfile = binding->hdrfile;

		fprintf(binding->outfile,
			"#ifndef _%s_\n"
			"#define _%s_\n\n",
			binding->hdrguard,
			binding->hdrguard);

		binding->outfile = binding->srcfile;
	}


	return 0;
}


static int
output_header_comments(struct binding *binding)
{
	const char *preamble = HDR_COMMENT_PREAMBLE;
	fprintf(binding->outfile, preamble, options->infilename);

	genbind_node_foreach_type(binding->gb_ast,
				   GENBIND_NODE_TYPE_HDRCOMMENT,
				   webidl_hdrcomment_cb,
				   binding);

	fprintf(binding->outfile,"\n */\n\n");

	if (binding->hdrfile != NULL) {
		binding->outfile = binding->hdrfile;

		fprintf(binding->outfile, preamble, options->infilename);

		genbind_node_foreach_type(binding->gb_ast,
					   GENBIND_NODE_TYPE_HDRCOMMENT,
					   webidl_hdrcomment_cb,
					   binding);

		fprintf(binding->outfile,"\n */\n\n");

		binding->outfile = binding->srcfile;
	}
	return 0;
}

static bool
binding_has_private(struct genbind_node *binding_list)
{
	struct genbind_node *node;

	node = genbind_node_find_type(binding_list,
				      NULL,
				      GENBIND_NODE_TYPE_BINDING_PRIVATE);

	if (node != NULL) {
		return true;
	}

	node = genbind_node_find_type(binding_list,
				      NULL,
				      GENBIND_NODE_TYPE_BINDING_INTERNAL);
	if (node != NULL) {
		return true;
	}
	return false;
}



/** obtain the name to use for the binding.
 *
 * @todo it should be possible to specify the binding name instead of
 * just using the name of the first interface.
 */
static const char *get_binding_name(struct genbind_node *binding_node)
{
	return genbind_node_gettext(
		genbind_node_find_type(
			genbind_node_getnode(
				genbind_node_find_type(
					genbind_node_getnode(binding_node),
					NULL,
					GENBIND_NODE_TYPE_BINDING_INTERFACE)),
			NULL,
			GENBIND_NODE_TYPE_IDENT));
}

static struct binding *
binding_new(struct options *options,
	    struct genbind_node *genbind_ast,
	    struct genbind_node *binding_node)
{
	struct binding *nb;

	int interfacec; /* numer of interfaces in the interface map */
	struct binding_interface *interfaces; /* binding interface map */

	struct genbind_node *binding_list;
	char *hdrguard = NULL;
	struct webidl_node *webidl_ast = NULL;
	int res;

	/* walk AST and load any web IDL files required */
	res = read_webidl(genbind_ast, &webidl_ast);
	if (res != 0) {
		fprintf(stderr, "Error: failed reading Web IDL\n");
		return NULL;
	}

	/* build the bindings interface (class) name map */
	if (build_interface_map(binding_node,
				webidl_ast,
				&interfacec,
				&interfaces) != 0) {
		/* the binding must have at least one interface */
		fprintf(stderr, "Error: Binding must have a valid interface\n");
		return NULL;
	}

	/* header guard */
	if (options->hdrfilename != NULL) {
		int guardlen;
		int pos;

		guardlen = strlen(options->hdrfilename);
		hdrguard = calloc(1, guardlen + 1);
		for (pos = 0; pos < guardlen; pos++) {
			if (isalpha(options->hdrfilename[pos])) {
				hdrguard[pos] = toupper(options->hdrfilename[pos]);
			} else {
				hdrguard[pos] = '_';
			}
		}
	}

	binding_list = genbind_node_getnode(binding_node);
	if (binding_list == NULL) {
		return NULL;
	}

	nb = calloc(1, sizeof(struct binding));

	nb->gb_ast = genbind_ast;
	nb->wi_ast = webidl_ast;

	/* keep the binding list node */
	nb->binding_list = binding_list;

	/* store the interface mapping */
	nb->interfaces = interfaces;
	nb->interfacec = interfacec;

	/* @todo get rid of the interface element out of the binding
	 * struct and use the interface map instead.
	 */
	nb->name = nb->interface = get_binding_name(binding_node);
	nb->outfile = options->outfilehandle;
	nb->srcfile = options->outfilehandle;
	nb->hdrfile = options->hdrfilehandle;
	nb->hdrguard = hdrguard;
	nb->has_private = binding_has_private(binding_list);

	/* class API */
	nb->addproperty = genbind_node_find_type_ident(genbind_ast,
						      NULL,
						      GENBIND_NODE_TYPE_API,
						      "addproperty");

	nb->delproperty = genbind_node_find_type_ident(genbind_ast,
						      NULL,
						      GENBIND_NODE_TYPE_API,
						      "delproperty");

	nb->getproperty = genbind_node_find_type_ident(genbind_ast,
						      NULL,
						      GENBIND_NODE_TYPE_API,
						      "getproperty");

	nb->setproperty = genbind_node_find_type_ident(genbind_ast,
						      NULL,
						      GENBIND_NODE_TYPE_API,
						      "setproperty");

	nb->enumerate = genbind_node_find_type_ident(genbind_ast,
						     NULL,
						     GENBIND_NODE_TYPE_API,
						     "enumerate");

	nb->resolve = genbind_node_find_type_ident(genbind_ast,
						   NULL,
						   GENBIND_NODE_TYPE_API,
						   "resolve");

	nb->finalise = genbind_node_find_type_ident(genbind_ast,
						    NULL,
						    GENBIND_NODE_TYPE_API,
						    "finalise");

	nb->mark = genbind_node_find_type_ident(genbind_ast,
						NULL,
						GENBIND_NODE_TYPE_API,
						"mark");
	return nb;
}


int
jsapi_libdom_output(struct options *options,
		    struct genbind_node *genbind_ast,
		    struct genbind_node *binding_node)
{
	int res;
	struct binding *binding;

	/* get general binding information used in output */
	binding = binding_new(options, genbind_ast, binding_node);
	if (binding == NULL) {
		return 40;
	}

	/* start with comment block */
	res = output_header_comments(binding);
	if (res) {
		return 50;
	}

	res = output_preamble(binding);
	if (res) {
		return 60;
	}

	res = output_private_declaration(binding);
	if (res) {
		return 70;
	}

	res = output_forward_declarations(binding);
	if (res) {
		return 75;
	}

	res = output_jsclasses(binding);
	if (res) {
		return 80;
	}

	res = output_property_tinyid(binding);
	if (res) {
		return 85;
	}

	/* user code output just before interface code bodies emitted */
	res = output_prologue(binding);
	if (res) {
		return 89;
	}

	/* method (function) and property body generation */

	res = output_function_bodies(binding);
	if (res) {
		return 90;
	}

	res = output_property_body(binding);
	if (res) {
		return 100;
	}

	/* method (function) and property specifier generation */

	res = output_function_spec(binding);
	if (res) {
		return 110;
	}

	res = output_property_spec(binding);
	if (res) {
		return 120;
	}

	/* binding specific operations (destructors etc.) */

	res = output_api_operations(binding);
	if (res) {
		return 130;
	}

	res = output_class_init(binding);
	if (res) {
		return 140;
	}

	res = output_class_new(binding);
	if (res) {
		return 150;
	}

	res = output_epilogue(binding);
	if (res) {
		return 160;
	}

	fclose(binding->outfile);

	return 0;
}
