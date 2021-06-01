// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int, int);

static int panicked = 0;

static struct {
	struct spinlock lock;
	int locking;
} cons;

#define CONSOLE_CNT  6
static struct {
	int view[25*80];
	int pos;
} console[CONSOLE_CNT];
static int active_console = 0;

static void
printint(int xx, int base, int sign, int cproc)
{
	static char digits[] = "0123456789abcdef";
	char buf[16];
	int i;
	uint x;

	if(sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do{
		buf[i++] = digits[x % base];
	}while((x /= base) != 0);

	if(sign)
		buf[i++] = '-';

	while(--i >= 0)
		consputc(buf[i], cproc);
}

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
	int i, c, locking, cproc;
	uint *argp;
	char *s;

	locking = cons.locking;
	if(locking)
		acquire(&cons.lock);

	if (fmt == 0)
		panic("null fmt");

	// This isn't nice, but we can't get active process with myproc here
	cproc = active_console;
	argp = (uint*)(void*)(&fmt + 1);
	for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
		if(c != '%'){
			consputc(c, cproc);
			continue;
		}
		c = fmt[++i] & 0xff;
		if(c == 0)
			break;
		switch(c){
		case 'd':
			printint(*argp++, 10, 1, cproc);
			break;
		case 'x':
		case 'p':
			printint(*argp++, 16, 0, cproc);
			break;
		case 's':
			if((s = (char*)*argp++) == 0)
				s = "(null)";
			for(; *s; s++)
				consputc(*s, cproc);
			break;
		case '%':
			consputc('%', cproc);
			break;
		default:
			// Print unknown % sequence to draw attention.
			consputc('%', cproc);
			consputc(c, cproc);
			break;
		}
	}

	if(locking)
		release(&cons.lock);
}

void
panic(char *s)
{
	int i;
	uint pcs[10];

	cli();
	cons.locking = 0;
	// use lapiccpunum so that we can call panic from mycpu()
	cprintf("lapicid %d: panic: ", lapicid());
	cprintf(s);
	cprintf("\n");
	getcallerpcs(&s, pcs);
	for(i=0; i<10; i++)
		cprintf(" %p", pcs[i]);
	panicked = 1; // freeze other CPU
	for(;;)
		;
}

#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

static void
crefresh()
{
	int i;
	for (i = 0; i < 24*80; i++) {
		crt[i] = console[active_console].view[i];
	}

	crt[1915] = 't' | 0x0700;
	crt[1916] = 't' | 0x0700;
	crt[1917] = 'y' | 0x0700;
	crt[1918] = ('0' + active_console + 1) | 0x0700;

	outb(CRTPORT, 14);
	outb(CRTPORT+1, console[active_console].pos>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, console[active_console].pos);
}

static void
cgaputc(int c, int cproc)
{
	// Get from CGA
	// outb(CRTPORT, 14);
	// pos = inb(CRTPORT+1) << 8;
	// outb(CRTPORT, 15);
	// pos |= inb(CRTPORT+1);
	// console.pos = pos;

	if(c == '\n') {
		console[cproc].pos += 80 - console[cproc].pos%80;
	}
	else if(c == BACKSPACE){
		if (console[cproc].pos > 0) {
			console[cproc].pos--;
		}
	} else {
		// crt[console.pos] = (c&0xff) | 0x0700;  // black on white
		console[cproc].view[console[cproc].pos++] = (c&0xff) | 0x0700;  // black on white
	}

	if (console[cproc].pos < 0 || console[cproc].pos > 25*80)
		panic("pos under/overflow");

	if ((console[cproc].pos/80) >= 24){  // Scroll up.
		memmove(console[cproc].view, console[cproc].view+80, sizeof(console[cproc].view[0])*23*80);
		// memmove(crt, crt+80, sizeof(crt[0])*23*80);
		console[cproc].pos -= 80;
		memset(console[cproc].view+console[cproc].pos, 0, sizeof(console[cproc].view[0])*(24*80 - console[cproc].pos));
		// memset(crt+console.pos, 0, sizeof(crt[0])*(24*80 - console.pos));
	}

	// outb(CRTPORT, 14);
	// outb(CRTPORT+1, console.pos>>8);
	// outb(CRTPORT, 15);
	// outb(CRTPORT+1, console.pos);
	// crt[console.pos] = ' ' | 0x0700;
	console[cproc].view[console[cproc].pos] = ' ' | 0x0700;

	// Only if active
	if (active_console == cproc) {
		crefresh();
	}
}

// static int debug_pos = 0;
// static void
// debugputc(int c)
// {
// 	if(c == '\n')
// 		debug_pos += 80 - debug_pos%80;
// 	else if(c == BACKSPACE){
// 		if(debug_pos > 0) --debug_pos;
// 	} else
// 		crt[debug_pos++] = (c&0xff) | 0x0700;  // black on white
// }

// void
// consputid()
// {
// 	if(console_id < 1 || console_id > 9)
// 		panic("identifier not supported");

// 	// 24 lines with 80 chars, 24 * 80 = 1920
// 	crt[1915] = 't' | 0x0700;
// 	crt[1916] = 't' | 0x0700;
// 	crt[1917] = 'y' | 0x0700;
// 	crt[1918] = ('0' + console_id) | 0x0700;
// }

// void
// consclearid()
// {
// 	crt[1915] = ' ' | 0x0700;
// 	crt[1916] = ' ' | 0x0700;
// 	crt[1917] = ' ' | 0x0700;
// 	crt[1918] = ' ' | 0x0700;
// }

void
consputc(int c, int cproc)
{
	if(panicked){
		cli();
		for(;;)
			;
	}

	if(c == BACKSPACE){
		uartputc('\b'); uartputc(' '); uartputc('\b');
	} else
		uartputc(c);
	cgaputc(c, cproc);
}

#define KEY_UP   0xE2
#define KEY_DN   0xE3
#define C(x)  ((x)-'@')  // Control-x
#define A(x)  ((x)+128)  // Alt-x

#define INPUT_BUF 128
struct {
	char buf[INPUT_BUF];
	uint r;  // Read index
	uint w;  // Write index
	uint e;  // Edit index
} input[CONSOLE_CNT];

#define HISTORY_CNT 8
struct {
	char present[INPUT_BUF];
	int present_len;
	int start_e;

	char past[HISTORY_CNT][INPUT_BUF];
	int past_len[HISTORY_CNT];

	int past_cnt;  // number of entries in past, limitted to HISTORY_CNT
	int pt_past;  // -1 in present, otherwise [0, past_end)
} history[CONSOLE_CNT];

void
inithistory()
{
	int i, j;
	for (j = 0; j < CONSOLE_CNT; j++)
	{
		history[j].present_len = 0;
		for (i = 0; i < HISTORY_CNT; i++)
		{
			history[j].past_len[i] = 0;
		}
		history[j].past_cnt = 0;
		history[j].pt_past = -1;
	}
}

void
clear_line(int c)
{
	while(input[c].e != input[c].w && input[c].buf[(input[c].e-1) % INPUT_BUF] != '\n'){
		input[c].e--;
		consputc(BACKSPACE, c);
	}
}

void
put_to_present(int c)
{
	int i, j;
	// debugputc('#');
	for (i = input[c].w, j = 0; i != input[c].e && input[c].buf[i] != '\n'; i = (i + 1) % INPUT_BUF, j++)
	{
		history[c].present[j] = input[c].buf[i];
		// debugputc(input.buf[i]);
	}
	// debugputc('#');
	history[c].present_len = j;
}

void
put_from_history(int c, int is_up)
{
	// debugputc('0' + history.pt_past);
	int i;
	char newc;

	// 0 -> -1
	// -1 -> 0
	// x -> y
	if (history[c].pt_past == -1 && is_up == 0)
	{
		// Clear current
		clear_line(c);
		// Put new
		for (i = 0; i < history[c].present_len; i++)
		{
			newc = history[c].present[i];
			input[c].buf[input[c].e++ % INPUT_BUF] = newc;
			consputc(newc, c);
		}
	}
	else if (history[c].pt_past == 0 && is_up == 1)
	{
		// Clear current
		clear_line(c);
		// Put new
		for (i = 0; i < history[c].past_len[history[c].pt_past]; i++)
		{
			newc = history[c].past[history[c].pt_past][i];
			input[c].buf[input[c].e++ % INPUT_BUF] = newc;
			consputc(newc, active_console);
		}
	}
	else
	{
		// Clear current
		clear_line(c);
		// Put new
		for (i = 0; i < history[c].past_len[history[c].pt_past]; i++)
		{
			newc = history[c].past[history[c].pt_past][i];
			input[c].buf[input[c].e++ % INPUT_BUF] = newc;
			consputc(newc, c);
		}
	}
}

void
put_to_history(int c)
{
	// debugputc('0' + history.pt_past);
	int i, j;
	
	put_to_present(c);
	if (history[c].present_len == 0 || history[c].present[0] == '\n') {
		// Empty line, do not add to history
		return;
	}

	// Move pasts
	for (i = HISTORY_CNT - 1; i - 1 >= 0; i--)
	{
		for (j = 0; j < INPUT_BUF; j++)
		{
			history[c].past[i][j] = history[c].past[i - 1][j];
		}
		history[c].past_len[i] = history[c].past_len[i - 1];
	}

	// Copy present
	for (j = 0; j < INPUT_BUF; j++)
	{
		history[c].past[0][j] = history[c].present[j];
	}
	history[c].past_len[0] = history[c].present_len;

	// Inc past counter
	if (history[c].past_cnt < HISTORY_CNT) {
		history[c].past_cnt++;
	}
}

// Captures keyboard, always to active_console
void
consoleintr(int (*getc)(void))
{
	int c, doprocdump = 0;

	acquire(&cons.lock);
	while((c = getc()) >= 0){
		switch(c){
		case C('P'):  // Process listing.
			// procdump() locks cons.lock indirectly; invoke later
			doprocdump = 1;
			break;
		case C('U'):  // Kill line.
			clear_line(active_console);
			break;
		case C('H'): case '\x7f':  // Backspace
			if(input[active_console].e != input[active_console].w){
				input[active_console].e--;
				consputc(BACKSPACE, active_console);
			}
			break;
		case A('1'): case A('2'): case A('3'): case A('4'): case A('5'): case A('6'):
			active_console = c - A('1');
			crefresh();
			break;
		case KEY_UP:
			// debugputc('u');
			if (history[active_console].pt_past == -1) {
				put_to_present(active_console);
			}
			if (history[active_console].pt_past + 1 < history[active_console].past_cnt) {
				history[active_console].pt_past++;
				put_from_history(active_console, 1);
			}
			break;
		case KEY_DN:
			// debugputc('d');
			if (history[active_console].pt_past == -1) {
				put_to_present(active_console);
			}
			if (history[active_console].pt_past - 1 >= -1) {
				history[active_console].pt_past--;
				put_from_history(active_console, 0);
			}
			break;
		default:
			if(c != 0 && input[active_console].e-input[active_console].r < INPUT_BUF){
				c = (c == '\r') ? '\n' : c;  // convert \r to \n
				input[active_console].buf[input[active_console].e++ % INPUT_BUF] = c;
				history[active_console].pt_past = -1;  // Reset history pointer
				consputc(c, active_console);
				if(c == '\n' || c == C('D') || input[active_console].e == input[active_console].r+INPUT_BUF){
					put_to_history(active_console);
					input[active_console].w = input[active_console].e;
					wakeup(&input[active_console].r);
				}
			}
			break;
		}
	}
	release(&cons.lock);
	if(doprocdump) {
		procdump();  // now call procdump() wo. cons.lock held
	}
}

int
consoleread(int nconsole, struct inode *ip, char *dst, int n)
{
	uint target;
	int c;

	iunlock(ip);
	target = n;
	acquire(&cons.lock);
	while(n > 0){
		while(input[nconsole].r == input[nconsole].w){
			if(myproc()->killed){
				release(&cons.lock);
				ilock(ip);
				return -1;
			}
			sleep(&input[nconsole].r, &cons.lock);
		}
		c = input[nconsole].buf[input[nconsole].r++ % INPUT_BUF];
		if(c == C('D')){  // EOF
			if(n < target){
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				input[nconsole].r--;
			}
			break;
		}
		*dst++ = c;
		--n;
		if(c == '\n')
			break;
	}
	release(&cons.lock);
	ilock(ip);

	return target - n;
}

int
consolewrite(int nconsole, struct inode *ip, char *buf, int n)
{
	int i;

	iunlock(ip);
	acquire(&cons.lock);
	for(i = 0; i < n; i++)
		consputc(buf[i] & 0xff, nconsole);
	release(&cons.lock);
	ilock(ip);

	return n;
}


// Can't think of a smarter way...
int
consoleread0(struct inode *ip, char *dst, int n)
{
	return consoleread(0, ip, dst, n);
}
int
consoleread1(struct inode *ip, char *dst, int n)
{
	return consoleread(1, ip, dst, n);
}
int
consoleread2(struct inode *ip, char *dst, int n)
{
	return consoleread(2, ip, dst, n);
}
int
consoleread3(struct inode *ip, char *dst, int n)
{
	return consoleread(3, ip, dst, n);
}
int
consoleread4(struct inode *ip, char *dst, int n)
{
	return consoleread(4, ip, dst, n);
}
int
consoleread5(struct inode *ip, char *dst, int n)
{
	return consoleread(5, ip, dst, n);
}

int
consolewrite0(struct inode *ip, char *buf, int n)
{
	return consolewrite(0, ip, buf, n);
}
int
consolewrite1(struct inode *ip, char *buf, int n)
{
	return consolewrite(1, ip, buf, n);
}
int
consolewrite2(struct inode *ip, char *buf, int n)
{
	return consolewrite(2, ip, buf, n);
}
int
consolewrite3(struct inode *ip, char *buf, int n)
{
	return consolewrite(3, ip, buf, n);
}
int
consolewrite4(struct inode *ip, char *buf, int n)
{
	return consolewrite(4, ip, buf, n);
}
int
consolewrite5(struct inode *ip, char *buf, int n)
{
	return consolewrite(5, ip, buf, n);
}

void
consoleinit(void)
{
	initlock(&cons.lock, "console");
	inithistory();

	devsw[1].write = consolewrite0;
	devsw[1].read = consoleread0;
	devsw[2].write = consolewrite1;
	devsw[2].read = consoleread1;
	devsw[3].write = consolewrite2;
	devsw[3].read = consoleread2;
	devsw[4].write = consolewrite3;
	devsw[4].read = consoleread3;
	devsw[5].write = consolewrite4;
	devsw[5].read = consoleread4;
	devsw[6].write = consolewrite5;
	devsw[6].read = consoleread5;

	cons.locking = 1;

	ioapicenable(IRQ_KBD, 0);
}

