/* Output functions and primitives.
 */

#include "kernel.h"

#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <varargs.h>

#ifdef FLUSH_TIOCFLUSH
#  include <sys/ioctl.h>
#else
#ifdef FLUSH_TCFLSH
#  include <termio.h>
#endif
#endif

extern int errno;

int Saved_Errno;

static Object V_Print_Depth, V_Print_Length;

Init_Print () {
    Define_Variable (&V_Print_Depth, "print-depth",
	Make_Integer (DEF_PRINT_DEPTH));
    Define_Variable (&V_Print_Length, "print-length",
	Make_Integer (DEF_PRINT_LEN));
}

Print_Length () {
    Object pl;

    pl = Var_Get (V_Print_Length);
    return TYPE(pl) == T_Fixnum ? FIXNUM(pl) : DEF_PRINT_LEN;
}

Print_Depth () {
    Object pd;

    pd = Var_Get (V_Print_Depth);
    return TYPE(pd) == T_Fixnum ? FIXNUM(pd) : DEF_PRINT_DEPTH;
}

Print_Char (port, c) Object port; register c; {
    char buf[1];

    if (PORT(port)->flags & P_STRING) {
	buf[0] = c;
	Print_String (port, buf, 1);
    } else {
	if (putc (c, PORT(port)->file) == EOF)  {
	    Saved_Errno = errno;   /* errno valid here? */
	    Primitive_Error ("write error on ~s: ~E", port);
	}
    }
}

Print_String (port, buf, len) Object port; register char *buf; register len; {
    register n;
    register struct S_Port *p;
    Object new;
    GC_Node;

    p = PORT(port);
    n = STRING(p->name)->size - p->ptr;
    if (n < len) {
	GC_Link (port);
	n = len - n;
	if (n < STRING_GROW_SIZE)
	    n = STRING_GROW_SIZE;
	new = Make_String ((char *)0, STRING(p->name)->size + n);
	p = PORT(port);
	GC_Unlink;
	bcopy (STRING(p->name)->data, STRING(new)->data, p->ptr);
	p->name = new;
    }
    bcopy (buf, STRING(p->name)->data + p->ptr, len);
    p->ptr += len;
}

#ifndef VPRINTF
vfprintf (f, fmt, ap) register FILE *f; register char *fmt; va_list ap; {
    _doprnt (fmt, ap, f);
}

vsprintf (s, fmt, ap) register char *s, *fmt; va_list ap; {
    FILE x;
    x._flag = _IOWRT|_IOSTRG;
    x._ptr = s;
    x._cnt = 1024;
    _doprnt (fmt, ap, &x);
    putc ('\0', &x);
}
#endif

/*VARARGS0*/
Printf (va_alist) va_dcl {
    va_list args;
    Object port;
    char *fmt;
    char buf[1024];

    va_start (args);
    port = va_arg (args, Object);
    fmt = va_arg (args, char *);
    if (PORT(port)->flags & P_STRING) {
	vsprintf (buf, fmt, args);
	Print_String (port, buf, strlen (buf));
    } else {
	vfprintf (PORT(port)->file, fmt, args);
	if (ferror (PORT(port)->file)) {
	    Saved_Errno = errno;   /* errno valid here? */
	    Primitive_Error ("write error on ~s: ~E", port);
	}
    }
    va_end (args);
}

Object General_Print (argc, argv, raw) Object *argv; {
    General_Print_Object (argv[0], argc == 2 ? argv[1] : Curr_Output_Port, raw);
    return Void;
}

Object P_Write (argc, argv) Object *argv; {
    return General_Print (argc, argv, 0);
}

Object P_Display (argc, argv) Object *argv; {
    return General_Print (argc, argv, 1);
}

Object P_Write_Char (argc, argv) Object *argv; {
    Check_Type (argv[0], T_Character);
    return General_Print (argc, argv, 1);
}

/*VARARGS1*/
Object P_Newline (argc, argv) Object *argv; {
    General_Print_Object (Newline, argc == 1 ? argv[0] : Curr_Output_Port, 1);
    return Void;
}

Object P_Print (argc, argv) Object *argv; {
    Object port;
    GC_Node;

    port = argc == 2 ? argv[1] : Curr_Output_Port;
    GC_Link (port);
    General_Print_Object (argv[0], port, 0);
    Print_Char (port, '\n');
    Flush_Output (port);
    GC_Unlink;
    return Void;
}

Object P_Clear_Output_Port (argc, argv) Object *argv; {
    Discard_Output (argc == 1 ? argv[0] : Curr_Output_Port);
    return Void;
}

Discard_Output (port) Object port; {
    register FILE *f;

    Check_Output_Port (port);
    if (PORT(port)->flags & P_STRING)
	return;
    f = PORT(port)->file;
#ifdef FLUSH_FPURGE
    (void)fpurge (f);
#else
#ifdef FLUSH_BSD
    f->_cnt = 0;
    f->_ptr = f->_base;
#endif
#endif
#ifdef FLUSH_TIOCFLUSH
    (void)ioctl (fileno (f), TIOCFLUSH, (char *)0);
#else
#ifdef FLUSH_TCFLSH
    (void)ioctl (fileno (f), TCFLSH, (char *)1);
#endif
#endif
}

Object P_Flush_Output_Port (argc, argv) Object *argv; {
    Flush_Output (argc == 1 ? argv[0] : Curr_Output_Port);
    return Void;
}

Flush_Output (port) Object port; {
    Check_Output_Port (port);
    if (PORT(port)->flags & P_STRING)
	return;
    if (fflush (PORT(port)->file) == EOF) {
	Saved_Errno = errno;   /* errno valid here? */
	Primitive_Error ("write error on ~s: ~E", port);
    }
}

Object P_Get_Output_String (port) Object port; {
    register struct S_Port *p;
    Object str;
    GC_Node;

    Check_Output_Port (port);
    GC_Link (port);
    str = Make_String ((char *)0, PORT(port)->ptr);
    p = PORT(port);
    bcopy (STRING(p->name)->data, STRING(str)->data, p->ptr);
    p->ptr = 0;
    GC_Unlink;
    return str;
}
    
Check_Output_Port (port) Object port; {
    Check_Type (port, T_Port);
    if (!(PORT(port)->flags & P_OPEN))
	Primitive_Error ("port has been closed: ~s", port);
    if (!IS_OUTPUT(port))
	Primitive_Error ("not an output port: ~s", port);
}

General_Print_Object (x, port, raw) Object x, port; {
    Check_Output_Port (port);
    Print_Object (x, port, raw, Print_Depth (), Print_Length ());
}

Print_Object (x, port, raw, depth, length) Object x, port;
	register raw, depth, length; {
    register t;
    GC_Node2;

    GC_Link2 (port, x);
    t = TYPE(x);
    switch (t) {
    case T_Null:
	Printf (port, "()");
	break;
    case T_Fixnum:
	Printf (port, "%d", FIXNUM(x));
	break;
    case T_Bignum:
	Print_Bignum (port, x);
	break;
    case T_Flonum:
	Printf (port, "%s", Flonum_To_String (x));
	break;
    case T_Boolean:
	Printf (port, "#%c", FIXNUM(x) ? 't' : 'f');
	break;
    case T_Unbound:
	Printf (port, "#[unbound]");
	break;
    case T_Special:
	Printf (port, "#[special]");
	break;
    case T_Character: {
	int c = CHAR(x);
	if (raw)
	    Print_Char (port, c);
	else
	    Pr_Char (port, c);
	break;
    }
    case T_Symbol:
	Pr_Symbol (port, x, raw);
	break;
    case T_Pair:
	Pr_List (port, x, raw, depth, length);
	break;
    case T_Environment:
	Printf (port, "#[environment %lu]", POINTER(x));
	break;
    case T_String:
	Pr_String (port, x, raw);
	break;
    case T_Vector:
	Pr_Vector (port, x, raw, depth, length);
	break;
    case T_Primitive:
	Printf (port, "#[primitive %s]", PRIM(x)->name);
	break;
    case T_Compound:
	if (Nullp (COMPOUND(x)->name)) {
	    Printf (port, "#[compound %lu]", POINTER(x));
	} else {
	    Printf (port, "#[compound ");
	    Print_Object (COMPOUND(x)->name, port, raw, depth, length);
	    Print_Char (port, ']');
	}
	break;
    case T_Control_Point:
	Printf (port, "#[control-point %lu]", POINTER(x));
	break;
    case T_Promise:
	Printf (port, "#[promise %lu]", POINTER(x));
	break;
    case T_Port: {
	int str = PORT(x)->flags & P_STRING;
	char *p;
	switch (PORT(x)->flags & (P_INPUT|P_BIDIR)) {
	case 0:       p = "output";       break;
	case P_INPUT: p = "input";        break;
	default:      p = "input-output"; break;
	}
	Printf (port, "#[%s-%s-port ", str ? "string" : "file", p);
	if (str)
	    Printf (port, "%lu", POINTER(x));
	else
	    Pr_String (port, PORT(x)->name, 0);
	Print_Char (port, ']');
	break;
    }
    case T_End_Of_File:
	Printf (port, "#[end-of-file]");
	break;
    case T_Autoload:
	Printf (port, "#[autoload ");
	Print_Object (AUTOLOAD(x)->files, port, raw, depth, length);
	Print_Char (port, ']');
	break;
    case T_Macro:
	if (Nullp (MACRO(x)->name)) {
	    Printf (port, "#[macro %lu]", POINTER(x));
	} else {
	    Printf (port, "#[macro ");
	    Print_Object (MACRO(x)->name, port, raw, depth, length);
	    Print_Char (port, ']');
	}
	break;
    case T_Broken_Heart:
	Printf (port, "!!broken-heart!!");
	break;
    default:
	if (t < 0 || t >= Num_Types)
	    Panic ("bad type in print");
	(Types[t].print)(x, port, raw, depth, length);
    }
    GC_Unlink;
}

Pr_Char (port, c) Object port; register c; {
    register char *p = 0;

    switch (c) {
    case ' ':
	p = "#\\space";
	break;
    case '\t':
	p = "#\\tab";
	break;
    case '\n':
	p = "#\\newline";
	break;
    case '\r':
	p = "#\\return";
	break;
    case '\f':
	p = "#\\formfeed";
	break;
    case '\b':
	p = "#\\backspace";
	break;
    default:
	if (c > ' ' && c < '\177')
	    Printf (port, "#\\%c", c);
	else
	    Printf (port, "#\\%03o", (unsigned char)c);
    }
    if (p) Printf (port, p);
}

Pr_List (port, list, raw, depth, length) Object port, list;
	register raw, depth, length; {
    Object tail;
    register len;
    register char *s = 0;
    GC_Node2;

    if (depth == 0) {
	Printf (port, "&");
	return;
    }
    GC_Link2 (port, list);
    if (!Nullp (list) && ((tail = Cdr (list)), TYPE(tail) == T_Pair)
		      && ((tail = Cdr (tail)), Nullp (tail))) {
	tail = Car (list);
	if (EQ(tail, Sym_Quote))
	    s = "'";
	else if (EQ(tail, Sym_Quasiquote))
	    s = "`";
	else if (EQ(tail, Sym_Unquote))
	    s = ",";
	else if (EQ(tail, Sym_Unquote_Splicing))
	    s = ",@";
	if (s) {
	    Printf (port, s);
	    Print_Object (Car (Cdr (list)), port, raw,
		depth < 0 ? depth : depth-1, length);
	    GC_Unlink;
	    return;
	}
    }
    Print_Char (port, '(');
    for (len = 0; !Nullp (list); len++, list = tail) {
	if (length >= 0 && len >= length) {
	    Printf (port, "...");
	    break;
	}
	Print_Object (Car (list), port, raw, depth < 0 ? depth : depth-1,
	     length);
	tail = Cdr (list);
	if (!Nullp (tail)) {
	    if (TYPE(tail) == T_Pair)
		Print_Char (port, ' ');
	    else {
		Printf (port, " . ");
		Print_Object (tail, port, raw, depth < 0 ? depth : depth-1,
		     length);
		break;
	    }
	}
    }
    Print_Char (port, ')');
    GC_Unlink;
}

Pr_Vector (port, vec, raw, depth, length) Object port, vec;
	register raw, depth, length; {
    register i, j;
    GC_Node2;

    if (depth == 0) {
	Printf (port, "&");
	return;
    }
    GC_Link2 (port, vec);
    Printf (port, "#(");
    for (i = 0, j = VECTOR(vec)->size; i < j; i++) {
	if (i) Print_Char (port, ' ');
	if (length >= 0 && i >= length) {
	    Printf (port, "...");
	    break;
	}
	Print_Object (VECTOR(vec)->data[i], port, raw,
	     depth < 0 ? depth : depth-1, length);
    }
    Print_Char (port, ')');
    GC_Unlink;
}

Pr_Symbol (port, sym, raw) Object port, sym; {
    Object str;
    register c, i;
    GC_Node2;

    str = SYMBOL(sym)->name;
    if (raw) {
	Pr_String (port, str, raw);
	return;
    }
    GC_Link2 (port, str);
    for (i = 0; i < STRING(str)->size; i++) {
	c = STRING(str)->data[i];
	switch (c) {
	case '\\': case ';': case '#': case '(': case ')':
	case '\'': case '`': case ',': case '"': case '.':
	case '\t': case '\n': case ' ':
	    Print_Char (port, '\\');
	    Print_Char (port, c);
	    break;
	default:
	    if (c < ' ' || c >= '\177')
		Print_Special (port, c);
	    else
		Print_Char (port, c);
	}
    }
    GC_Unlink;
}

Pr_String (port, str, raw) Object port, str; {
    register char *p = STRING(str)->data;
    register c, i, len = STRING(str)->size;
    GC_Node2;

    if (raw) {
	if (PORT(port)->flags & P_STRING) {
	    Print_String (port, p, len);
	} else {
	    if (fwrite (p, 1, len, PORT(port)->file) != len) {
		Saved_Errno = errno;   /* errno valid here? */
		Primitive_Error ("write error on ~s: ~E", port);
	    }
	}
	return;
    }
    GC_Link2 (port, str);
    Print_Char (port, '"');
    for (i = 0; i < STRING(str)->size; i++) {
	c = STRING(str)->data[i];
	if (c == '\\' || c == '"')
	    Print_Char (port, '\\');
	if (c < ' ' || c >= '\177')
	    Print_Special (port, c);
	else
	    Print_Char (port, c);
    }
    Print_Char (port, '"');
    GC_Unlink;
}

Print_Special (port, c) Object port; register c; {
    register char *fmt = "\\%c";

    switch (c) {
    case '\b': c = 'b'; break;
    case '\t': c = 't'; break;
    case '\r': c = 'r'; break;
    case '\n': c = 'n'; break;
    default:
	fmt = "\\%03o";
    }
    Printf (port, fmt, (unsigned char)c);
}

Object P_Format (argc, argv) Object *argv; {
    Object port, str;
    register stringret = 0;
    GC_Node;

    port = argv[0];
    if (TYPE(port) == T_Boolean) {
	if (Truep (port)) {
	    port = Curr_Output_Port;
	} else {
	    stringret++;
	    port = P_Open_Output_String ();
	}
    } else if (TYPE(port) == T_Port) {
	Check_Output_Port (port);
    } else Wrong_Type_Combination (port, "port or #t or #f");
    str = argv[1];
    Check_Type (str, T_String);
    GC_Link (port);
    Format (port, STRING(str)->data, STRING(str)->size, argc-2, argv+2);
    GC_Unlink;
    return stringret ? P_Get_Output_String (port) : Void;
}

Format (port, fmt, len, argc, argv) Object port; const char *fmt;
	int len; Object *argv; {
    register const char *s, *ep;
    char *p;
    register c;
    char buf[256];
    extern sys_nerr;
#ifndef __bsdi__
 #ifndef __linux__
    extern char *sys_errlist[];
 #endif
#endif
    GC_Node;
    Alloca_Begin;

    GC_Link (port);
    Alloca (p, char*, len);
    bcopy (fmt, p, len);
    for (ep = p + len; p < ep; p++) {
	if (*p == '~') {
	    if (++p == ep) break;
	    if ((c = *p) == '~') {
		Print_Char (port, c);
	    } else if (c == '%') {
		Print_Char (port, '\n');
	    } else if (c == 'e' || c == 'E') {
		s = strerror(Saved_Errno);
		sprintf (buf, "%c%s", isupper (*s) ? tolower (*s) :
		    *s, *s ? "" : s+1);
		Print_Object (Make_String (buf, strlen (buf)), port,
		    c == 'E', 0, 0);
	    } else {
		if (--argc < 0)
		    Primitive_Error ("too few arguments");
		if (c == 's' || c == 'a') {
		    Print_Object (*argv, port, c == 'a', Print_Depth (),
			Print_Length ());
		    argv++;
		} else if (c == 'c') {
		    Check_Type (*argv, T_Character);
		    Print_Char (port, CHAR(*argv));
		    argv++;
		} else Print_Char (port, c);
	    }
	} else {
	    Print_Char (port, *p);
	}
    }
    Alloca_End;
    GC_Unlink;
}
