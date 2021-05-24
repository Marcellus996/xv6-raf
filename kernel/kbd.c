#include "types.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

int
kbdgetc(void)
{
	// Variable name is stupid, this is binary representation for (alt, ctrl, shift)
	static uint shift;
	static uchar *charcode[8] = {
		// ctrl > shift > alt
		// 000        001      010      011    100      101      110    111
		normalmap, shiftmap, ctlmap, ctlmap, altmap, shiftmap, ctlmap, ctlmap 
	};
	uint st, data, c;

	st = inb(KBSTATP);
	if((st & KBS_DIB) == 0)
		return -1;
	data = inb(KBDATAP);

	if(data == 0xE0){
		// Clean up (E0ESC = 1000000)
		shift |= E0ESC;
		return 0;
	} else if(data & 0x80){
		// Key released
		data = (shift & E0ESC ? data : data & 0x7F);
		shift &= ~(shiftcode[data] | E0ESC);
		return 0;
	} else if(shift & E0ESC){
		// Last character was an E0 escape; or with 0x80
		data |= 0x80;
		shift &= ~E0ESC;
	}

	shift |= shiftcode[data];
	shift ^= togglecode[data];
	c = charcode[shift & (CTL | SHIFT | ALT)][data];
	if(shift & CAPSLOCK){
		if('a' <= c && c <= 'z')
			c += 'A' - 'a';
		else if('A' <= c && c <= 'Z')
			c += 'a' - 'A';
	}
	return c;
}

void
kbdintr(void)
{
	consoleintr(kbdgetc);
}
