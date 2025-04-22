/*	tcap.c
 *
 *	Unix V7 SysV and BS4 Termcap video driver
 *
 *	modified by Petri Kutvonen
 */

/*
 * Defining this to 1 breaks tcapopen() - it doesn't check if the
 * sceen size has changed.
 *	-lbt
 */
#define USE_BROKEN_OPTIMIZATION 0
#define	termdef	1 /* Don't define "term" external. */

#ifdef _WIN32
#include <windows.h>
#else
#include <curses.h>
#include <term.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

#if TERMCAP

#if UNIX
#include <signal.h>
#endif

#define	MARGIN	8
#define	SCRSIZ	64
#define	NPAUSE	10    /* # times thru update to pause. */
#define BEL     0x07
#define ESC     0x1B

#ifdef _WIN32
static HANDLE hConsole;
static CONSOLE_SCREEN_BUFFER_INFO csbi;
#endif

static void tcapkopen(void);
static void tcapkclose(void);
static void tcapmove(int, int);
static void tcapeeol(void);
static void tcapeeop(void);
static void tcapbeep(void);
static void tcaprev(int);
static int tcapcres(char *);

static void tcapopen(void);
#if PKCODE
static void tcapclose(void);
#endif

#if COLOR
static void tcapfcol(void);
static void tcapbcol(void);
#endif
#if SCROLLCODE
static void tcapscroll_reg(int from, int to, int linestoscroll);
static void tcapscroll_delins(int from, int to, int linestoscroll);
static void tcapscrollregion(int top, int bot);
#endif

#ifndef _WIN32
#define TCAPSLEN 315
static char tcapbuf[TCAPSLEN];
static char *UP, PC, *CM, *CE, *CL, *SO, *SE;

#if PKCODE
static char *TI, *TE;
#if USE_BROKEN_OPTIMIZATION
static int term_init_ok = 0;
#endif
#endif

#if SCROLLCODE
static char *CS, *DL, *AL, *SF, *SR;
#endif
#endif // !_WIN32

struct terminal term = {
	0,
	0,
	0,
	0,
	MARGIN,
	SCRSIZ,
	NPAUSE,
	tcapopen,
#if PKCODE
	tcapclose,
#else
	ttclose,
#endif
	tcapkopen,
	tcapkclose,
	ttgetc,
	ttputc,
	ttflush,
	tcapmove,
	tcapeeol,
	tcapeeop,
	tcapbeep,
	tcaprev,
	tcapcres
#if COLOR
	, tcapfcol,
	tcapbcol
#endif
#if SCROLLCODE
	, NULL
#endif
};

static void tcapopen(void)
{
#ifdef _WIN32
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleScreenBufferInfo(hConsole, &csbi);

	term.t_ncol = csbi.dwSize.X;
	term.t_nrow = csbi.dwSize.Y - 1;

	term.t_mcol = term.t_ncol;
	term.t_mrow = term.t_nrow;

	ttopen();
#else
	char *t, *p;
	char tcbuf[1024];
	char *tv_stype;
	char err_str[72];
	int int_col, int_row;

#if PKCODE && USE_BROKEN_OPTIMIZATION
	if (!term_init_ok) {
#endif
		if ((tv_stype = getenv("TERM")) == NULL) {
			puts("Environment variable TERM not defined!");
			exit(1);
		}

		if ((tgetent(tcbuf, tv_stype)) != 1) {
			sprintf(err_str, "Unknown terminal type %s!", tv_stype);
			puts(err_str);
			exit(1);
		}

		getscreensize(&int_col, &int_row);
		term.t_nrow = int_row - 1;
		term.t_ncol = int_col;

		if ((term.t_nrow <= 0)
			&& (term.t_nrow = (short) tgetnum("li") - 1) == -1) {
			puts("termcap entry incomplete (lines)");
			exit(1);
		}

		if ((term.t_ncol <= 0)
			&& (term.t_ncol = (short) tgetnum("co")) == -1) {
			puts("Termcap entry incomplete (columns)");
			exit(1);
		}
#ifdef SIGWINCH
		term.t_mrow = MAXROW;
		term.t_mcol = MAXCOL;
#else
		term.t_mrow = term.t_nrow > MAXROW ? MAXROW : term.t_nrow;
		term.t_mcol = term.t_ncol > MAXCOL ? MAXCOL : term.t_ncol;
#endif
		p = tcapbuf;
		t = tgetstr("pc", &p);
		if (t)
			PC = *t;
		else
			PC = 0;

		CL = tgetstr("cl", &p);
		CM = tgetstr("cm", &p);
		CE = tgetstr("ce", &p);
		UP = tgetstr("up", &p);
		SE = tgetstr("se", &p);
		SO = tgetstr("so", &p);
		if (SO != NULL)
			revexist = TRUE;
#if PKCODE
		if (tgetnum("sg") > 0) {
			revexist = FALSE;
			SE = NULL;
			SO = NULL;
		}
		TI = tgetstr("ti", &p);
		TE = tgetstr("te", &p);
#endif

		if (CL == NULL || CM == NULL || UP == NULL) {
			puts("Incomplete termcap entry\n");
			exit(1);
		}

		if (CE == NULL)
			eolexist = FALSE;
#if SCROLLCODE
		CS = tgetstr("cs", &p);
		SF = tgetstr("sf", &p);
		SR = tgetstr("sr", &p);
		DL = tgetstr("dl", &p);
		AL = tgetstr("al", &p);

		if (CS && SR) {
			if (SF == NULL)
				SF = "\n";
			term.t_scroll = tcapscroll_reg;
		} else if (DL && AL) {
			term.t_scroll = tcapscroll_delins;
		} else {
			term.t_scroll = NULL;
		}
#endif
		if (p >= &tcapbuf[TCAPSLEN]) {
			puts("Terminal description too big!\n");
			exit(1);
		}
#if PKCODE && USE_BROKEN_OPTIMIZATION
		term_init_ok = 1;
	}
#endif
	ttopen();
#endif
}

#if PKCODE
static void tcapclose(void)
{
#ifdef _WIN32
	// nothing to do
#else
	putpad(tgoto(CM, 0, term.t_nrow));
	putpad(TE);
	ttflush();
	ttclose();
#endif
}
#endif

static void tcapkopen(void)
{
#ifdef _WIN32
	// nothing to do
#else
#if PKCODE
	putpad(TI);
	ttflush();
	ttrow = 999;
	ttcol = 999;
	sgarbf = TRUE;
#endif
	strcpy(sres, "NORMAL");
#endif
}

static void tcapkclose(void)
{
#ifdef _WIN32
	// nothing to do
#else
#if PKCODE
	putpad(TE);
	ttflush();
#endif
#endif
}

static void tcapmove(int row, int col)
{
#ifdef _WIN32
	COORD pos = { (SHORT)col, (SHORT)row };
	SetConsoleCursorPosition(hConsole, pos);
#else
	putpad(tgoto(CM, col, row));
#endif
}

static void tcapeeol(void)
{
#ifdef _WIN32
	DWORD written;
	COORD pos = csbi.dwCursorPosition;
	int len = csbi.dwSize.X - pos.X;
	FillConsoleOutputCharacter(hConsole, ' ', len, pos, &written);
	SetConsoleCursorPosition(hConsole, pos);
#else
	putpad(CE);
#endif
}

static void tcapeeop(void)
{
#ifdef _WIN32
	DWORD written;
	COORD pos = { 0, 0 };
	DWORD size = csbi.dwSize.X * csbi.dwSize.Y;
	FillConsoleOutputCharacter(hConsole, ' ', size, pos, &written);
	SetConsoleCursorPosition(hConsole, pos);
#else
	putpad(CL);
#endif
}

static void tcaprev(int state)
{
#ifdef _WIN32
	WORD attr;
	if (state) {
		GetConsoleScreenBufferInfo(hConsole, &csbi);
		attr = BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED;
		SetConsoleTextAttribute(hConsole, attr);
	} else {
		SetConsoleTextAttribute(hConsole, csbi.wAttributes);
	}
#else
	if (state) {
		if (SO != NULL)
			putpad(SO);
	} else if (SE != NULL)
		putpad(SE);
#endif
}

static int tcapcres(char *res)
{
	return TRUE;
}

#if COLOR
static void tcapfcol(void) {}
static void tcapbcol(void) {}
#endif

static void tcapbeep(void)
{
#ifdef _WIN32
	Beep(750, 100);
#else
	ttputc(BEL);
#endif
}

#ifndef _WIN32
static void putpad(char *str)
{
	tputs(str, 1, ttputc);
}
#endif

#endif /* TERMCAP */
