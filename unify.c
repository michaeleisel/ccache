/*
   Copyright (C) Andrew Tridgell 2002
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/*
  C/C++ unifier

  the idea is that changes that don't affect the resulting C code should not change
  the hash
*/

#include "ccache.h"

static char *s_tokens[] = {
	"...",	">>=",	"<<=",	"+=",	"-=",	"*=",	"/=",	"%=",	"&=",	"^=",
	"|=",	">>",	"<<",	"++",	"--",	"->",	"&&",	"||",	"<=",	">=",
	"==",	"!=",	";",	"{",	"<%",	"}",	"%>",	",",	":",	"=",
	"(",	")",	"[",	"<:",	"]",	":>",	".",	"&",	"!",	"~",
	"-",	"+",	"*",	"/",	"%",	"<",	">",	"^",	"|",	"?",
	0
};

#define C_ALPHA 1
#define C_SPACE 2
#define C_TOKEN 4
#define C_QUOTE 8
#define C_DIGIT 16
#define C_HEX   32
#define C_FLOAT 64
#define C_SIGN  128

static struct {
	unsigned char type;
	unsigned char num_toks;
	char *toks[7];
} tokens[256];

/* build up the table used by the unifier */
static void build_table(void)
{
	unsigned char c;
	int i;
	static int done;

	if (done) return;
	done = 1;

	memset(tokens, 0, sizeof(tokens));
	for (c=0;c<128;c++) {
		if (isalpha(c) || c == '_') tokens[c].type |= C_ALPHA;
		if (isdigit(c)) tokens[c].type |= C_DIGIT;
		if (isspace(c)) tokens[c].type |= C_SPACE;
		if (isxdigit(c)) tokens[c].type |= C_HEX;
	}
	tokens['\''].type |= C_QUOTE;
	tokens['"'].type |= C_QUOTE;
	tokens['l'].type |= C_FLOAT;
	tokens['L'].type |= C_FLOAT;
	tokens['f'].type |= C_FLOAT;
	tokens['F'].type |= C_FLOAT;
	tokens['U'].type |= C_FLOAT;
	tokens['u'].type |= C_FLOAT;

	tokens['-'].type |= C_SIGN;
	tokens['+'].type |= C_SIGN;

	for (i=0;s_tokens[i];i++) {
		c = s_tokens[i][0];
		tokens[c].type |= C_TOKEN;
		tokens[c].toks[tokens[c].num_toks] = s_tokens[i];
		tokens[c].num_toks++;
	}
}

/* buffer up characters before hashing them */
static void pushchar(unsigned char c)
{
	static unsigned char buf[64];
	static int len;

	if (c == 0) {
		hash_buffer(buf, len);
		len = 0;
		hash_buffer(NULL, 0);
		return;
	}

	buf[len++] = c;
	if (len == 64) {
		hash_buffer(buf, len);
		len = 0;
	}
}

/* hash some C/C++ code after unifying */
void unify(unsigned char *p, size_t size)
{
	size_t ofs;
	unsigned char q;
	int i;

	build_table();

	for (ofs=0; ofs<size;) {
		if (p[ofs] == '#') {
			if ((size-ofs) > 2 && p[ofs+1] == ' ' && isdigit(p[ofs+2])) {
				do {
					ofs++;
				} while (ofs < size && p[ofs] != '\n');
				ofs++;
			} else {
				do {
					pushchar(p[ofs]);
					ofs++;
				} while (ofs < size && p[ofs] != '\n');
				pushchar('\n');
				ofs++;
			}
			continue;
		}

		if (tokens[p[ofs]].type & C_ALPHA) {
			do {
				pushchar(p[ofs]);
				ofs++;
			} while (ofs < size && 
				 (tokens[p[ofs]].type & (C_ALPHA|C_DIGIT)));
			pushchar('\n');
			continue;
		}

		if (tokens[p[ofs]].type & C_DIGIT) {
			do {
				pushchar(p[ofs]);
				ofs++;
			} while (ofs < size && 
				 (tokens[p[ofs]].type & C_DIGIT || p[ofs] == '.'));
			if (p[ofs] == 'x' || p[ofs] == 'X') {
				do {
					pushchar(p[ofs]);
					ofs++;
				} while (ofs < size && tokens[p[ofs]].type & C_HEX);
			}
			if (p[ofs] == 'E' || p[ofs] == 'e') {
				pushchar(p[ofs]);
				ofs++;
				while (ofs < size && 
				       tokens[p[ofs]].type & (C_DIGIT|C_SIGN)) {
					pushchar(p[ofs]);
					ofs++;
				}
			}
			while (ofs < size && tokens[p[ofs]].type & C_FLOAT) {
				pushchar(p[ofs]);
				ofs++;
			}
			pushchar('\n');
			continue;
		}

		if (tokens[p[ofs]].type & C_SPACE) {
			do {
				ofs++;
			} while (ofs < size && tokens[p[ofs]].type & C_SPACE);
			continue;
		}
			
		if (tokens[p[ofs]].type & C_QUOTE) {
			q = p[ofs];
			pushchar(p[ofs]);
			do {
				ofs++;
				while (ofs < size-1 && p[ofs] == '\\') {
					pushchar(p[ofs]);
					pushchar(p[ofs+1]);
					ofs+=2;
				}
				pushchar(p[ofs]);
			} while (ofs < size && p[ofs] != q);
			pushchar('\n');
			ofs++;
			continue;
		}

		if (tokens[p[ofs]].type & C_TOKEN) {
			q = p[ofs];
			for (i=0;tokens[q].num_toks;i++) {
				unsigned char *s = tokens[q].toks[i];
				int len = strlen(s);
				if (strncmp(&p[ofs], s, len) == 0) {
					int j;
					for (j=0;s[j];j++) {
						pushchar(s[j]);
						ofs++;
					}
					pushchar('\n');
					break;
				}
			}
			continue;
		}

		pushchar(p[ofs]);
		pushchar('\n');
		ofs++;
	}
	pushchar(0);
}

