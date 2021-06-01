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

static void consputc(int);

static int panicked = 0;

static int console_id = 1;

static struct {
	struct spinlock lock;
	int locking;
} cons;

static void
printint(int xx, int base, int sign)
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
		consputc(buf[i]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
	int i, c, locking;
	uint *argp;
	char *s;

	locking = cons.locking;
	if(locking)
		acquire(&cons.lock);

	if (fmt == 0)
		panic("null fmt");

	argp = (uint*)(void*)(&fmt + 1);
	for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
		if(c != '%'){
			consputc(c);
			continue;
		}
		c = fmt[++i] & 0xff;
		if(c == 0)
			break;
		switch(c){
		case 'd':
			printint(*argp++, 10, 1);
			break;
		case 'x':
		case 'p':
			printint(*argp++, 16, 0);
			break;
		case 's':
			if((s = (char*)*argp++) == 0)
				s = "(null)";
			for(; *s; s++)
				consputc(*s);
			break;
		case '%':
			consputc('%');
			break;
		default:
			// Print unknown % sequence to draw attention.
			consputc('%');
			consputc(c);
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
cgaputc(int c)
{
	int pos;

	// Cursor position: col + 80*row.
	outb(CRTPORT, 14);
	pos = inb(CRTPORT+1) << 8;
	outb(CRTPORT, 15);
	pos |= inb(CRTPORT+1);

	if(c == '\n')
		pos += 80 - pos%80;
	else if(c == BACKSPACE){
		if(pos > 0) --pos;
	} else
		crt[pos++] = (c&0xff) | 0x0700;  // black on white

	if(pos < 0 || pos > 25*80)
		panic("pos under/overflow");

	if((pos/80) >= 24){  // Scroll up.
		consclearid();
		memmove(crt, crt+80, sizeof(crt[0])*23*80);
		pos -= 80;
		memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
	}

	// Console identifier
	consputid();

	outb(CRTPORT, 14);
	outb(CRTPORT+1, pos>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, pos);
	crt[pos] = ' ' | 0x0700;
}

static int debug_pos = 0;

static void
debugputc(int c)
{
	if(c == '\n')
		debug_pos += 80 - debug_pos%80;
	else if(c == BACKSPACE){
		if(debug_pos > 0) --debug_pos;
	} else
		crt[debug_pos++] = (c&0xff) | 0x0700;  // black on white
}

void
consputid()
{
	if(console_id < 1 || console_id > 9)
		panic("identifier not supported");

	// 24 lines with 80 chars, 24 * 80 = 1920
	crt[1915] = 't' | 0x0700;
	crt[1916] = 't' | 0x0700;
	crt[1917] = 'y' | 0x0700;
	crt[1918] = ('0' + console_id) | 0x0700;
}

void
consclearid()
{
	crt[1915] = ' ' | 0x0700;
	crt[1916] = ' ' | 0x0700;
	crt[1917] = ' ' | 0x0700;
	crt[1918] = ' ' | 0x0700;
}

void
consputc(int c)
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
	cgaputc(c);
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
} input;

// TODO #define CONSOLE_CNT 6
#define HISTORY_CNT 8
struct {
	char present[INPUT_BUF];
	int present_len;
	int start_e;

	char past[HISTORY_CNT][INPUT_BUF];
	int past_len[HISTORY_CNT];

	int past_cnt;  // number of entries in past, limitted to HISTORY_CNT
	int pt_past;  // -1 in present, otherwise [0, past_end)
} history;

void
inithistory()
{
	int i;
	history.present_len = 0;
	for (i = 0; i < HISTORY_CNT; i++)
	{
		history.past_len[i] = 0;
	}
	history.past_cnt = 0;
	history.pt_past = -1;
}

void
clear_line()
{
	while(input.e != input.w && input.buf[(input.e-1) % INPUT_BUF] != '\n'){
		input.e--;
		consputc(BACKSPACE);
	}
}

void
put_to_present()
{
	int i, j;
	// debugputc('#');
	for (i = input.w, j = 0; i != input.e && input.buf[i] != '\n'; i = (i + 1) % INPUT_BUF, j++)
	{
		history.present[j] = input.buf[i];
		// debugputc(input.buf[i]);
	}
	// debugputc('#');
	history.present_len = j;
}

void
put_from_history(int is_up)
{
	// debugputc('0' + history.pt_past);
	int i;
	char c;

	// 0 -> -1
	// -1 -> 0
	// x -> y
	if (history.pt_past == -1 && is_up == 0)
	{
		// Clear current
		clear_line();
		// Put new
		for (i = 0; i < history.present_len; i++)
		{
			c = history.present[i];
			input.buf[input.e++ % INPUT_BUF] = c;
			consputc(c);
		}
	}
	else if (history.pt_past == 0 && is_up == 1)
	{
		// Clear current
		clear_line();
		// Put new
		for (i = 0; i < history.past_len[history.pt_past]; i++)
		{
			c = history.past[history.pt_past][i];
			input.buf[input.e++ % INPUT_BUF] = c;
			consputc(c);
		}
	}
	else
	{
		// Clear current
		clear_line();
		// Put new
		for (i = 0; i < history.past_len[history.pt_past]; i++)
		{
			c = history.past[history.pt_past][i];
			input.buf[input.e++ % INPUT_BUF] = c;
			consputc(c);
		}
	}
}

void
put_to_history()
{
	// debugputc('0' + history.pt_past);
	int i, j;
	
	put_to_present();
	if (history.present_len == 0 || history.present[0] == '\n') {
		// Empty line, do not add to history
		return;
	}

	// Move pasts
	for (i = HISTORY_CNT - 1; i - 1 >= 0; i--)
	{
		for (j = 0; j < INPUT_BUF; j++)
		{
			history.past[i][j] = history.past[i - 1][j];
		}
		history.past_len[i] = history.past_len[i - 1];
	}

	// Copy present
	for (j = 0; j < INPUT_BUF; j++)
	{
		history.past[0][j] = history.present[j];
	}
	history.past_len[0] = history.present_len;

	// Inc past counter
	if (history.past_cnt < HISTORY_CNT) {
		history.past_cnt++;
	}
}

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
			clear_line();
			break;
		case C('H'): case '\x7f':  // Backspace
			if(input.e != input.w){
				input.e--;
				consputc(BACKSPACE);
			}
			break;
		case A('1'): case A('2'): case A('3'): case A('4'): case A('5'): case A('6'):
			console_id = c - A('1') + 1;
			consputid();
			break;
		case KEY_UP:
			// debugputc('u');
			if (history.pt_past == -1) {
				put_to_present();
			}
			if (history.pt_past + 1 < history.past_cnt) {
				history.pt_past++;
				put_from_history(1);
			}
			break;
		case KEY_DN:
			// debugputc('d');
			if (history.pt_past == -1) {
				put_to_present();
			}
			if (history.pt_past - 1 >= -1) {
				history.pt_past--;
				put_from_history(0);
			}
			break;
		default:
			if(c != 0 && input.e-input.r < INPUT_BUF){
				c = (c == '\r') ? '\n' : c;  // convert \r to \n
				input.buf[input.e++ % INPUT_BUF] = c;
				history.pt_past = -1;  // Reset history pointer
				consputc(c);
				if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
					put_to_history();
					input.w = input.e;
					wakeup(&input.r);
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
consoleread(struct inode *ip, char *dst, int n)
{
	uint target;
	int c;

	iunlock(ip);
	target = n;
	acquire(&cons.lock);
	while(n > 0){
		while(input.r == input.w){
			if(myproc()->killed){
				release(&cons.lock);
				ilock(ip);
				return -1;
			}
			sleep(&input.r, &cons.lock);
		}
		c = input.buf[input.r++ % INPUT_BUF];
		if(c == C('D')){  // EOF
			if(n < target){
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				input.r--;
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
consolewrite(struct inode *ip, char *buf, int n)
{
	int i;

	iunlock(ip);
	acquire(&cons.lock);
	for(i = 0; i < n; i++)
		consputc(buf[i] & 0xff);
	release(&cons.lock);
	ilock(ip);

	return n;
}

void
consoleinit(void)
{
	initlock(&cons.lock, "console");

	devsw[CONSOLE].write = consolewrite;
	devsw[CONSOLE].read = consoleread;
	cons.locking = 1;

	inithistory();

	ioapicenable(IRQ_KBD, 0);
}

