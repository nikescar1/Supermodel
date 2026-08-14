/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free 
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/
 
/*
 * ppc.cpp
 *
 * PowerPC emulator main module. Written by Ville Linde for the original
 * Supermodel project.
 */

/* IBM/Motorola PowerPC 4xx/6xx Emulator */

#include "ppc.h"

#include <cstring>	// memset()
#include "Supermodel.h"
#include "CPU/Bus.h"

// Typedefs that Supermodel no longer provides
typedef unsigned int	UINT;

// Model 3 context provides read/write handlers
static class IBus	*Bus = NULL;	// pointer to Model 3 bus object (for access handlers)

#ifdef SUPERMODEL_DEBUGGER
// Pointer to current PPC debugger (if any)
static class Debugger::CPPCDebug *PPCDebug = NULL;
#endif

void ppc603_exception(int exception);
static void ppc603_check_interrupts(void);

#define RD				((op >> 21) & 0x1F)
#define RT				((op >> 21) & 0x1f)
#define RS				((op >> 21) & 0x1f)
#define RA				((op >> 16) & 0x1f)
#define RB				((op >> 11) & 0x1f)
#define RC				((op >> 6) & 0x1f)

#define MB				((op >> 6) & 0x1f)
#define ME				((op >> 1) & 0x1f)
#define SH				((op >> 11) & 0x1f)
#define BO				((op >> 21) & 0x1f)
#define BI				((op >> 16) & 0x1f)
#define CRFD			((op >> 23) & 0x7)
#define CRFA			((op >> 18) & 0x7)
#define FXM				((op >> 12) & 0xff)
#define SPR				(((op >> 16) & 0x1f) | ((op >> 6) & 0x3e0))

#define SIMM16			(INT32)(INT16)(op & 0xffff)
#define UIMM16			(UINT32)(op & 0xffff)

#define RCBIT			(op & 0x1)
#define OEBIT			(op & 0x400)
#define AABIT			(op & 0x2)
#define LKBIT			(op & 0x1)

#define REG(x)			(ppc.r[x])
#define LR				(ppc.lr)
#define CTR				(ppc.ctr)
#define XER				(ppc.xer)
#define CR(x)			(ppc.cr[x])
#define MSR				(ppc.msr)
#define SRR0			(ppc.srr0)
#define SRR1			(ppc.srr1)
#define SRR2			(ppc.srr2)
#define SRR3			(ppc.srr3)
#define EVPR			(ppc.evpr)
#define EXIER			(ppc.exier)
#define EXISR			(ppc.exisr)
#define DEC				(ppc.dec)


// Stuff added for the 6xx
#define FPR(x)			(ppc.fpr[x])
#define FM				((op >> 17) & 0xFF)
#define SPRF			(((op >> 6) & 0x3E0) | ((op >> 16) & 0x1F))


#define CHECK_SUPERVISOR()			\
	if((ppc.msr & 0x4000) != 0){	\
	}

#define CHECK_FPU_AVAILABLE()		\
	if((ppc.msr & 0x2000) == 0){	\
	}

static UINT32		ppc_field_xlat[256];



#define FPSCR_FX		0x80000000
#define FPSCR_FEX		0x40000000
#define FPSCR_VX		0x20000000
#define FPSCR_OX		0x10000000
#define FPSCR_UX		0x08000000
#define FPSCR_ZX		0x04000000
#define FPSCR_XX		0x02000000



#define BITMASK_0(n)	(UINT32)(((UINT64)1 << (n)) - 1)
#define CRBIT(x)		((ppc.cr[(x) / 4] & (1 << (3 - ((x) % 4)))) ? 1 : 0)
#define _BIT(n)			(1 << (n))
#define GET_ROTATE_MASK(mb,me)		(ppc_rotate_mask[mb][me])
#define ADD_CA(r,a,b)		((UINT32)(r) < (UINT32)(a))
#define SUB_CA(r,a,b)		(!((UINT32)(a) < (UINT32)(b)))
#define ADD_OV(r,a,b)		((~((a) ^ (b)) & ((a) ^ (r))) & 0x80000000)
#define SUB_OV(r,a,b)		(( ((a) ^ (b)) & ((a) ^ (r))) & 0x80000000)

#define XER_SO			0x80000000
#define XER_OV			0x40000000
#define XER_CA			0x20000000

#define MSR_POW			0x00040000	/* Power Management Enable */
#define MSR_WE			0x00040000
#define MSR_CE			0x00020000
#define MSR_ILE			0x00010000	/* Interrupt Little Endian Mode */
#define MSR_EE			0x00008000	/* External Interrupt Enable */
#define MSR_PR			0x00004000	/* Problem State */
#define MSR_FP			0x00002000	/* Floating Point Available */
#define MSR_ME			0x00001000	/* Machine Check Enable */
#define MSR_FE0			0x00000800
#define MSR_SE			0x00000400	/* Single Step Trace Enable */
#define MSR_BE			0x00000200	/* Branch Trace Enable */
#define MSR_DE			0x00000200
#define MSR_FE1			0x00000100
#define MSR_IP			0x00000040	/* Interrupt Prefix */
#define MSR_IR			0x00000020	/* Instruction Relocate */
#define MSR_DR			0x00000010	/* Data Relocate */
#define MSR_PE			0x00000008
#define MSR_PX			0x00000004
#define MSR_RI			0x00000002	/* Recoverable Interrupt Enable */
#define MSR_LE			0x00000001

#define TSR_ENW			0x80000000
#define TSR_WIS			0x40000000

#define BYTE_REVERSE16(x)	((((x) >> 8) | ((x) << 8)) & 0xFFFF)
#define BYTE_REVERSE32(x)	(((x) >> 24) | (((x) << 8) & 0x00FF0000) | (((x) >> 8) & 0x0000FF00) | ((x) << 24))

typedef union {
	UINT64	id;
	double	fd;
} FPR;

typedef union {
	UINT32 i;
	float f;
} FPR32;

typedef struct {
	UINT32 u;
	UINT32 l;
} BATENT;


typedef struct {
	bool	fatalError;	// if true, halt PowerPC until hard reset
	
	UINT32 r[32];
	UINT32 pc;
	UINT32 npc;

	UINT32 *op;

	UINT32 lr;
	UINT32 ctr;
	UINT32 xer;
	UINT32 msr;
	UINT8 cr[8];
	UINT32 pvr;
	UINT32 srr0;
	UINT32 srr1;
	UINT32 srr2;
	UINT32 srr3;
	UINT32 hid0;
	UINT32 hid1;
	UINT32 hid2;
	UINT32 sdr1;
	UINT32 sprg[4];

	UINT32 dsisr;
	UINT32 dar;
	UINT32 ear;
	UINT32 dmiss;
	UINT32 dcmp;
	UINT32 hash1;
	UINT32 hash2;
	UINT32 imiss;
	UINT32 icmp;
	UINT32 rpa;


	BATENT ibat[4];
	BATENT dbat[4];

	UINT32 evpr;
	UINT32 exier;
	UINT32 exisr;
	UINT32 bear;
	UINT32 besr;
	UINT32 iocr;
	UINT32 br[8];
	UINT32 iabr;
	UINT32 esr;
	UINT32 iccr;
	UINT32 dccr;
	UINT32 pit;
	UINT32 pit_counter;
 	UINT32 pit_int_enable;
	UINT32 tsr;
	UINT32 dbsr;
	UINT32 sgr;
	UINT32 pid;

	int reserved;
	UINT32 reserved_address;

	int interrupt_pending;
	int external_int;

	UINT64 tb;		/* 56-bit timebase register */

	int (*irq_callback)(int irqline);

	PPC_FETCH_REGION	cur_fetch;
	PPC_FETCH_REGION	* fetch;

	// STUFF added for the 6xx series
	UINT32 dec;
	UINT32 fpscr;

	FPR	fpr[32];
	UINT32 sr[16];

	// Timing related
	int timer_ratio;
	UINT32 timer_frac;
	int tb_base_icount;
	int dec_base_icount;
	int dec_trigger_cycle;
	// The count the inner interpreter loop runs down to. Zero unless the
	// decrementer is due to fire inside this slice, in which case it is the
	// count at which that happens. See ppc603_execute.
	int icount_stop;

	// How many instructions each of the two dispatch paths ran.
	//
	// This began as a sample of where code was being fetched from, which is
	// what said the cache had to cover RAM: better than 99 percent of these
	// games' instructions are executed from there. Now that the cache exists
	// the useful question is how much of the work it actually takes, so the
	// same two counters answer that instead. They are exact rather than
	// sampled and still cost nothing, because the two loops are already
	// separate and each one's share is the count it ran down.
	UINT64 dec_cached_insns;
	UINT64 dec_uncached_insns;

	// Where the decoded dispatch cache is up to, kept in step with `op`.
	// NULL when execution is somewhere the cache does not cover, which is
	// how the loop above knows to fall back to working the handler out.
	// See the cache itself, further down; the name avoids the decrementer
	// register, which got there first.
	void (**dec_cursor)(UINT32);

	// What the idle detector saw last time it was asked. See ppc_note_spin.
	UINT32 spin_pc;
	int spin_icount;
	UINT64 spin_bus;
	// How many instructions have been skipped rather than executed, and how
	// many were executed, so the two can be reported against each other.
	UINT64 spin_skipped;
	
	// Cycle related
	UINT64 total_cycles;
	int icount;
	int cur_cycles;
	int bus_freq_multiplier;
	int cycles_per_second;

#if HAS_PPC603
	int is603;
#endif
#if HAS_PPC602
	int is602;
#endif
} PPC_REGS;



typedef struct {
	int code;
	int subcode;
	void (* handler)(UINT32);
} PPC_OPCODE;



static PPC_REGS ppc;
static UINT32 ppc_rotate_mask[32][32];

// Defined below, where the dispatch tables it reads are declared.
static void ppc_decode_stub(UINT32 op);
static inline void ppc_set_dec(UINT32 newpc);

// Stops the interpreter dead.
//
// The inner loops used to ask whether this had happened on every emulated
// instruction, to catch something that ends the run. Setting the count the
// loop is running down to means it falls out of its own accord, and the loop
// above it, which does still ask, decides what to do. Same device the
// decrementer and the decode cache both use.
static inline void ppc_halt(void)
{
	ppc.fatalError = true;
	ppc.icount_stop = ppc.icount;
}

static void ppc_change_pc(UINT32 newpc)
{
	UINT32 offset	= newpc - ppc.cur_fetch.start;		//  unsigned wrap around can happen, that's defined behavour 
	UINT32 range	= ppc.cur_fetch.end - ppc.cur_fetch.start;

	if (offset <= range)
	{
		ppc.op = &ppc.cur_fetch.ptr[offset / 4];
		ppc_set_dec(newpc);
		return;
	}

	for(UINT32 i = 0; ppc.fetch[i].ptr != NULL; i++)
	{
		offset	= newpc - ppc.fetch[i].start;
		range	= ppc.fetch[i].end - ppc.fetch[i].start;

		if (offset <= range)
		{
			ppc.cur_fetch = ppc.fetch[i];

			ppc.op = &ppc.cur_fetch.ptr[offset / 4];
			ppc_set_dec(newpc);
			return;
		}
	}

	DebugLog("Invalid PC %08X, previous PC %08X\n", newpc, ppc.pc);
	ErrorLog("PowerPC is out of bounds. Halting emulation until reset.");
	ppc_halt();
}

/*
 * Main RAM, reached without going through the bus object.
 *
 * Every load and store the interpreter performs is a virtual call on IBus, and
 * the overwhelming majority of them land in the flat RAM at the bottom of the
 * map, where the handler does nothing but compare the address and index an
 * array. On a desktop that call is cheap enough not to matter. On a phone the
 * emulated processor is the entire frame budget: measured on a Snapdragon,
 * Sega Rally spends 26.5 ms of a 17.4 ms frame inside this loop, and an
 * indirect call that cannot be inlined is paid for on every one of those
 * instructions that touches memory.
 *
 * So the region is borrowed once, by pointer, and the common case becomes a
 * compare and an index with nothing between it and the caller. Anything that
 * is not this region, and anything misaligned, goes to the bus exactly as
 * before. The byte swizzling is not decoration: Supermodel stores RAM with
 * each aligned word already byte reversed, so an 8 bit access indexes addr^3
 * and a 16 bit one addr^2. These have to agree with CModel3's own handlers to
 * the letter, or the two paths will disagree about what memory holds.
 *
 * Zero size disables all of it, which is what a machine that never attached
 * RAM gets.
 */
static UINT8	*RAM = NULL;
static UINT32	RAMSize = 0;

// Which way these tests almost always go.
//
// Every one of them is "is this address ordinary memory", and it nearly always
// is: the exceptions are the memory mapped registers, and a game touches those
// a few thousand times a frame against a few hundred thousand ordinary loads
// and stores. Saying so lets the compiler put the ordinary case in a straight
// line and push the call to the bus out of the way of it, which is worth more
// than the branch prediction: a call in the middle of a hot path forces every
// live register around it onto the stack.
#if defined(__GNUC__) || defined(__clang__)
#define PPC_LIKELY(x)	__builtin_expect(!!(x), 1)
#define PPC_UNLIKELY(x)	__builtin_expect(!!(x), 0)
#else
#define PPC_LIKELY(x)	(x)
#define PPC_UNLIKELY(x)	(x)
#endif

// Keeps a load's slow half out of its fast half.
//
// A load has to put the result somewhere after the read, so unlike a store
// it cannot simply hand the address to the bus and return. That leaves the
// destination register live across the call, and the compiler answers by
// building a stack frame and spilling four registers into it, on every load,
// including the overwhelming majority that never leave RAM. Splitting the
// two halves means the fast one is a leaf with no frame at all and the slow
// one is reached by a tail call, which needs no frame either.
#if defined(__GNUC__) || defined(__clang__)
#define PPC_NOINLINE	__attribute__((noinline))
#else
#define PPC_NOINLINE	__declspec(noinline)
#endif

// How many accesses have gone somewhere other than RAM or the program ROM.
//
// Not part of the processor's state and not saved with it: it exists so that
// the idle detector further down can tell a loop reading a device register,
// whose value changes on its own, from one reading ordinary memory, which
// cannot change while the processor is the only thing running.
static UINT64	BusTouches = 0;

// Whether waiting loops may be skipped rather than executed. On by default and
// switchable because the argument for it, sound as it looks, is an argument
// about what a game can and cannot do rather than a measurement of what it
// does; a game that misbehaves needs a way back without a new build.
static bool	IdleSkip = true;

void ppc_set_idle_skip(bool enabled)
{
	IdleSkip = enabled;
}

// The longest waiting loop worth recognising, in instructions. Long enough for
// a poll that masks a bit before comparing it, short enough that walking the
// body costs nothing. See ppc_spin_loop, and ppc_invalidate_window, which has
// to reach as far.
#define PPC_SPIN_MAX	8

static UINT8	*ROM = NULL;

/*
 * The decoded dispatch cache.
 *
 * Working out which function handles an instruction is a switch on the primary
 * opcode and then an index into one of five tables, and it is done every time
 * the instruction executes. A loop body runs millions of times a second and
 * decodes to the same handler every one of them.
 *
 * So the answer is remembered, one entry per instruction word of main RAM,
 * and the inner loop reads it instead of working it out. That replaces about
 * seven instructions with a single load. Measured against the alternative:
 * these games run 99.3 percent of their instructions out of RAM rather than
 * the program ROM, which is why the cache is over RAM and has to deal with
 * being written to.
 *
 * Every entry starts as a stub that decodes on demand, fills itself in, and
 * runs the handler it found. That is what makes this need no page tables, no
 * allocation while running and no test in the inner loop for whether an entry
 * is present: an undecoded entry is simply an entry whose handler happens to
 * be the decoder. Invalidating is the same idea backwards, and costs one store:
 * putting the stub back means the next execution decodes again.
 *
 * One flat array over the whole 8 MB rather than pages, so that stepping from
 * one instruction to the next is an increment with nothing to check. It costs
 * sixteen megabytes, which is the price of not having a bounds test in the
 * hottest loop in the emulator.
 */
typedef void (*PPCHandler)(UINT32);

static PPCHandler	*Dec = NULL;
static UINT32		DecEntries = 0;

/*
 * Which pages have ever held code, one byte each.
 *
 * Invalidating on every store would otherwise be the cache's own undoing. A
 * store of four bytes would write eight bytes of Dec, so a routine copying a
 * buffer would drag twice its length through the data cache in entries that
 * describe memory holding no instructions at all. Games move a great deal more
 * data than they do code.
 *
 * The byte says whether anything on the page behind it has ever been decoded.
 * Almost nothing has: a store to a data page reads one byte, finds a zero and
 * leaves Dec alone. Eight megabytes of RAM is two thousand and forty-eight
 * pages, so the whole map is two kilobytes and simply stays in the cache.
 *
 * What makes it safe is that only the decoder sets a byte, and the decoder is
 * the only thing that puts a real handler anywhere. So a clear byte means
 * every entry on that page is still the stub, and a stub does not need
 * invalidating. The map is never cleared for a page that goes quiet, which
 * costs a little work on a page holding both code and data and is correct.
 */
// The map itself rather than a pointer to it, so reaching a byte is an index
// off the mask and not a load of somewhere to look first. Two kilobytes covers
// the eight megabytes every one of these machines has; a mask of zero means
// there is no cache, and reads the one byte that is then always zero.
#define CODE_PAGE_MAX	2048
static UINT8		CodePage[CODE_PAGE_MAX];
static UINT32		CodePageMask = 0;

// Puts the stub back for the word containing `address`, so the next execution
// of it decodes again. Callers have already established that the address is
// inside RAM.
// The word written and the few after it.
//
// One entry would be enough if every entry described only its own word, but a
// branch that closes a waiting loop holds a decision made by reading the
// instructions in front of it, and those are at most PPC_SPIN_MAX words back.
// So writing one of them has to throw the branch away as well. Outlined
// because the caller's fast path is the test above it, not this.
static PPC_NOINLINE void ppc_invalidate_window(UINT32 first)
{
	UINT32 last = first + PPC_SPIN_MAX;
	if (last >= DecEntries)
		last = DecEntries - 1;
	for (UINT32 i = first; i <= last; i++)
		Dec[i] = ppc_decode_stub;
}

static inline void ppc_invalidate(UINT32 address)
{
	if (PPC_UNLIKELY(CodePage[(address >> 12) & CodePageMask] != 0))
		ppc_invalidate_window(address >> 2);
}

// Notes that the word at `address` has been decoded, so stores to its page
// start clearing entries. See CodePage.
static inline void ppc_note_code(UINT32 address)
{
	CodePage[(address >> 12) & CodePageMask] = 1;
}

void ppc_invalidate_word(UINT32 address)
{
	if (address < RAMSize)
		ppc_invalidate(address);
}

void ppc_invalidate_all(void)
{
	for (UINT32 i = 0; i < DecEntries; i++)
		Dec[i] = ppc_decode_stub;
	// Nothing is decoded any more, so nothing needs invalidating either.
	memset(CodePage, 0, (size_t) CodePageMask + 1);
}

// Points the cache cursor at `newpc`, and says so when that moves execution
// between memory the cache covers and memory it does not.
//
// Rather than testing for the change on every instruction, the inner loop is
// sent back out to the one above it, which picks the loop that suits where
// execution now is. Same device as the decrementer uses.
static inline void ppc_set_dec(UINT32 newpc)
{
	PPCHandler *want = (Dec != NULL && newpc < RAMSize) ? &Dec[newpc >> 2] : NULL;
	if ((want == NULL) != (ppc.dec_cursor == NULL))
		ppc.icount_stop = ppc.icount;
	ppc.dec_cursor = want;
}

void ppc_attach_ram(UINT8 *ram, UINT32 size)
{
	RAM = ram;
	RAMSize = (ram != NULL) ? size : 0;

	if (Dec != NULL)
	{
		free(Dec);
		Dec = NULL;
		DecEntries = 0;
	}
	CodePageMask = 0;
	CodePage[0] = 0;
	ppc.dec_cursor = NULL;

	if (RAM == NULL || RAMSize == 0)
		return;

	// The page map is indexed with a mask rather than a bounds test, which
	// wants a power of two, and it is a fixed size. Every machine this runs
	// on has eight megabytes; anything else simply goes without the cache
	// rather than growing a second way of doing this.
	const UINT32 pages = RAMSize >> 12;
	if (pages == 0 || pages > CODE_PAGE_MAX || (pages & (pages - 1)) != 0)
		return;

	// A few entries of slack past the end, matching the fetch pointer, which
	// has always been free to walk off the end of a region before a branch
	// takes it somewhere real.
	DecEntries = (RAMSize / 4) + 16;
	Dec = (PPCHandler *) malloc(DecEntries * sizeof(PPCHandler));
	if (Dec == NULL)
	{
		// No cache is not an error. The interpreter works out the handler the
		// way it always did and everything below falls back to that.
		DecEntries = 0;
		return;
	}
	CodePageMask = pages - 1;
	ppc_invalidate_all();
}

void ppc_dec_mix(UINT64 *cached, UINT64 *uncached)
{
	*cached = ppc.dec_cached_insns;
	*uncached = ppc.dec_uncached_insns;
}

UINT8 *ppc_direct_ram(void)
{
	return RAM;
}

void ppc_attach_rom(UINT8 *rom)
{
	ROM = rom;
}

// The fixed half of the program ROM, at 0xFF800000 and up. Read only, so
// unlike RAM there is nothing to keep coherent, and the swizzle matches
// CModel3::Read8/16/32 exactly. Reads only: a write there must still go to the
// bus, which is where the banking and the ignoring live.
#define IN_ROM(address) (ROM != NULL && (address) >= 0xFF800000)
#define ROM_AT(address) (&ROM[(address) & 0x7FFFFF])

static inline UINT8 READ8(UINT32 address)
{
	if (PPC_LIKELY(address < RAMSize))
		return RAM[address^3];
	if (IN_ROM(address))
		return *(UINT8 *) ROM_AT(address^3);
	BusTouches++;
	return Bus->Read8(address);
}

static inline UINT16 READ16(UINT32 address)
{
	if (PPC_LIKELY(address < RAMSize && !(address&1)))
		return *(UINT16 *) &RAM[address^2];
	if (IN_ROM(address) && !(address&1))
		return *(UINT16 *) ROM_AT(address^2);
	BusTouches++;
	return Bus->Read16(address);
}

static inline UINT32 READ32(UINT32 address)
{
	if (PPC_LIKELY(address < RAMSize && !(address&3)))
		return *(UINT32 *) &RAM[address];
	if (IN_ROM(address) && !(address&3))
		return *(UINT32 *) ROM_AT(address);
	BusTouches++;
	return Bus->Read32(address);
}

static inline UINT64 READ64(UINT32 address)
{
	// The first test also rules out an address near the top of the map whose
	// +8 would wrap around and look in range.
	if (address < RAMSize && address+8 <= RAMSize && !(address&3))
	{
		UINT64 data = *(UINT32 *) &RAM[address];
		data <<= 32;
		data |= *(UINT32 *) &RAM[address+4];
		return data;
	}
	BusTouches++;
	return Bus->Read64(address);
}

static inline void WRITE8(UINT32 address, UINT8 data)
{
	if (PPC_LIKELY(address < RAMSize))
	{
		RAM[address^3] = data;
		ppc_invalidate(address);
		return;
	}
	BusTouches++;
	Bus->Write8(address,data);
}

static inline void WRITE16(UINT32 address, UINT16 data)
{
	if (PPC_LIKELY(address < RAMSize && !(address&1)))
	{
		*(UINT16 *) &RAM[address^2] = data;
		ppc_invalidate(address);
		return;
	}
	BusTouches++;
	Bus->Write16(address,data);
}

static inline void WRITE32(UINT32 address, UINT32 data)
{
	if (PPC_LIKELY(address < RAMSize && !(address&3)))
	{
		*(UINT32 *) &RAM[address] = data;
		ppc_invalidate(address);
		return;
	}
	BusTouches++;
	Bus->Write32(address,data);
}

static inline void WRITE64(UINT32 address, UINT64 data)
{
	if (address < RAMSize && address+8 <= RAMSize && !(address&3))
	{
		*(UINT32 *) &RAM[address+0] = (UINT32) (data>>32);
		*(UINT32 *) &RAM[address+4] = (UINT32) data;
		ppc_invalidate(address+0);
		ppc_invalidate(address+4);
		return;
	}
	BusTouches++;
	Bus->Write64(address,data);
}


/*********************************************************************/


static inline void SET_CR0(INT32 rd)
{
	if( rd < 0 ) {
		CR(0) = 0x8;
	} else if( rd > 0 ) {
		CR(0) = 0x4;
	} else {
		CR(0) = 0x2;
	}

	if( XER & XER_SO )
		CR(0) |= 0x1;
}

static inline void SET_CR1(void)
{
	CR(1) = (ppc.fpscr >> 28) & 0xf;
}

static inline void SET_ADD_OV(UINT32 rd, UINT32 ra, UINT32 rb)
{
	if( ADD_OV(rd, ra, rb) )
		XER |= XER_SO | XER_OV;
	else
		XER &= ~XER_OV;
}

static inline void SET_SUB_OV(UINT32 rd, UINT32 ra, UINT32 rb)
{
	if( SUB_OV(rd, ra, rb) )
		XER |= XER_SO | XER_OV;
	else
		XER &= ~XER_OV;
}

static inline void SET_ADD_CA(UINT32 rd, UINT32 ra, UINT32 rb)
{
	if( ADD_CA(rd, ra, rb) )
		XER |= XER_CA;
	else
		XER &= ~XER_CA;
}

static inline void SET_SUB_CA(UINT32 rd, UINT32 ra, UINT32 rb)
{
	if( SUB_CA(rd, ra, rb) )
		XER |= XER_CA;
	else
		XER &= ~XER_CA;
}

static inline UINT32 check_condition_code(UINT32 bo, UINT32 bi)
{
	UINT32 bo0 = (bo & 0x10) ? 1 : 0;
	UINT32 bo1 = (bo & 0x08) ? 1 : 0;
	UINT32 bo2 = (bo & 0x04) ? 1 : 0;
	UINT32 bo3 = (bo & 0x02) ? 1 : 0;

	if (bo2 == 0)
		--CTR;

	UINT32 ctr_ok = bo2 | ((CTR != 0) ^ bo3);
	UINT32 condition_ok = bo0 | (CRBIT(bi) ^ (bo1 ^ 0x1));

	return ctr_ok & condition_ok;
}

static inline UINT64 ppc_read_timebase(void)
{
	int cycles = ppc.tb_base_icount - ppc.icount;

	// Timebase is incremented according to timer ratio, so adjust value accordingly
	return ppc.tb + (cycles / ppc.timer_ratio);
}

static inline void ppc_write_timebase_l(UINT32 tbl)
{
	UINT64 tb = ppc_read_timebase();

	ppc.tb_base_icount = ppc.icount + ((ppc.tb_base_icount - ppc.icount) % ppc.timer_ratio);

	ppc.tb = (tb&~0xffffffff)|tbl;
}

static inline void ppc_write_timebase_h(UINT32 tbh)
{
	UINT64 tb = ppc_read_timebase();

	ppc.tb_base_icount = ppc.icount + ((ppc.tb_base_icount - ppc.icount) % ppc.timer_ratio);
	
	ppc.tb = (tb&0xffffffff)|((UINT64)(tbh) << 32);
}

static inline UINT32 read_decrementer(void)
{
	int cycles = ppc.dec_base_icount - ppc.icount;

	// Decrementer is decremented at same rate as timebase, so adjust value accordingly
	return DEC - (cycles / ppc.timer_ratio);
}

static inline void write_decrementer(UINT32 value)
{
	if (((value&0x80000000) && !(read_decrementer()&0x80000000)))
	{
		/* trigger interrupt */
		ppc.interrupt_pending |= 0x2;
		ppc603_check_interrupts();
	}

	ppc.dec_base_icount = ppc.icount + ((ppc.dec_base_icount - ppc.icount) % ppc.timer_ratio);
	
	DEC = value;

	// Check if decrementer exception occurs during execution (exception occurs after decrementer
	// has passed through zero)
	if ((UINT32)(ppc.dec_base_icount / ppc.timer_ratio) > DEC)
		ppc.dec_trigger_cycle = ppc.dec_base_icount - ((1 + DEC) * ppc.timer_ratio);
	else
		ppc.dec_trigger_cycle = 0x7fffffff;

	// The inner loop is comparing against a floor worked out before this ran,
	// so it has to be sent back out to work it out again. Setting the floor to
	// where the count already is ends the loop on its next test without any
	// other effect.
	ppc.icount_stop = ppc.icount;
}

/*********************************************************************/

static inline void ppc_set_spr(int spr, UINT32 value)
{
	switch (spr)
	{
		case SPR_LR:		LR = value; return;
		case SPR_CTR:		CTR = value; return;
		case SPR_XER:		XER = value; return;
		case SPR_SRR0:		ppc.srr0 = value; return;
		case SPR_SRR1:		ppc.srr1 = value; return;
		case SPR_SPRG0:		ppc.sprg[0] = value; return;
		case SPR_SPRG1:		ppc.sprg[1] = value; return;
		case SPR_SPRG2:		ppc.sprg[2] = value; return;
		case SPR_SPRG3:		ppc.sprg[3] = value; return;
		case SPR_PVR:		return;
			
		case SPR603E_DEC:
			write_decrementer(value);
			return;

		case SPR603E_TBL_W:
		case SPR603E_TBL_R: // special 603e case
			ppc_write_timebase_l(value);
			return;

		case SPR603E_TBU_R:
		case SPR603E_TBU_W: // special 603e case
			ppc_write_timebase_h(value);
			return;

		case SPR603E_HID0:			ppc.hid0 = value; return;
		case SPR603E_HID1:			ppc.hid1 = value; return;
		case SPR603E_HID2:			ppc.hid2 = value; return;

		case SPR603E_DSISR:			ppc.dsisr = value; return;
		case SPR603E_DAR:			ppc.dar = value; return;
		case SPR603E_EAR:			ppc.ear = value; return;
		case SPR603E_DMISS:			ppc.dmiss = value; return;
		case SPR603E_DCMP:			ppc.dcmp = value; return;
		case SPR603E_HASH1:			ppc.hash1 = value; return;
		case SPR603E_HASH2:			ppc.hash2 = value; return;
		case SPR603E_IMISS:			ppc.imiss = value; return;
		case SPR603E_ICMP:			ppc.icmp = value; return;
		case SPR603E_RPA:			ppc.rpa = value; return;

		case SPR603E_IBAT0L:		ppc.ibat[0].l = value; return;
		case SPR603E_IBAT0U:		ppc.ibat[0].u = value; return;
		case SPR603E_IBAT1L:		ppc.ibat[1].l = value; return;
		case SPR603E_IBAT1U:		ppc.ibat[1].u = value; return;
		case SPR603E_IBAT2L:		ppc.ibat[2].l = value; return;
		case SPR603E_IBAT2U:		ppc.ibat[2].u = value; return;
		case SPR603E_IBAT3L:		ppc.ibat[3].l = value; return;
		case SPR603E_IBAT3U:		ppc.ibat[3].u = value; return;
		case SPR603E_DBAT0L:		ppc.dbat[0].l = value; return;
		case SPR603E_DBAT0U:		ppc.dbat[0].u = value; return;
		case SPR603E_DBAT1L:		ppc.dbat[1].l = value; return;
		case SPR603E_DBAT1U:		ppc.dbat[1].u = value; return;
		case SPR603E_DBAT2L:		ppc.dbat[2].l = value; return;
		case SPR603E_DBAT2U:		ppc.dbat[2].u = value; return;
		case SPR603E_DBAT3L:		ppc.dbat[3].l = value; return;
		case SPR603E_DBAT3U:		ppc.dbat[3].u = value; return;

		case SPR603E_SDR1:
			ppc.sdr1 = value;
			return;

		case SPR603E_IABR:			ppc.iabr = value; return;
	}

	ErrorLog("PowerPC wrote to an invalid register. Halting emulation until reset.");
	DebugLog("ppc: set_spr: unknown spr %d (%03X) !\n", spr, spr);
	ppc_halt();
}

static inline UINT32 ppc_get_spr(int spr)
{
	switch(spr)
	{
		case SPR_LR:		return LR;
		case SPR_CTR:		return CTR;
		case SPR_XER:		return XER;
		case SPR_SRR0:		return ppc.srr0;
		case SPR_SRR1:		return ppc.srr1;
		case SPR_SPRG0:		return ppc.sprg[0];
		case SPR_SPRG1:		return ppc.sprg[1];
		case SPR_SPRG2:		return ppc.sprg[2];
		case SPR_SPRG3:		return ppc.sprg[3];
		case SPR_PVR:		return ppc.pvr;
		case SPR603E_TBL_R:
			DebugLog("ppc: get_spr: TBL_R\n");
			break;

		case SPR603E_TBU_R:
			DebugLog("ppc: get_spr: TBU_R\n");
			break;

		case SPR603E_TBL_W:		return (UINT32)(ppc_read_timebase());
		case SPR603E_TBU_W:		return (UINT32)(ppc_read_timebase() >> 32);
		case SPR603E_HID0:		return ppc.hid0;
		case SPR603E_HID1:		return ppc.hid1;
		case SPR603E_HID2:		return ppc.hid2;
		case SPR603E_DEC:		return read_decrementer();
		case SPR603E_SDR1:		return ppc.sdr1;
		case SPR603E_DSISR:		return ppc.dsisr;
		case SPR603E_DAR:		return ppc.dar;
		case SPR603E_EAR:		return ppc.ear;
		case SPR603E_DMISS:		return ppc.dmiss;
		case SPR603E_DCMP:		return ppc.dcmp;
		case SPR603E_HASH1:		return ppc.hash1;
		case SPR603E_HASH2:		return ppc.hash2;
		case SPR603E_IMISS:		return ppc.imiss;
		case SPR603E_ICMP:		return ppc.icmp;
		case SPR603E_RPA:		return ppc.rpa;
		case SPR603E_IBAT0L:	return ppc.ibat[0].l;
		case SPR603E_IBAT0U:	return ppc.ibat[0].u;
		case SPR603E_IBAT1L:	return ppc.ibat[1].l;
		case SPR603E_IBAT1U:	return ppc.ibat[1].u;
		case SPR603E_IBAT2L:	return ppc.ibat[2].l;
		case SPR603E_IBAT2U:	return ppc.ibat[2].u;
		case SPR603E_IBAT3L:	return ppc.ibat[3].l;
		case SPR603E_IBAT3U:	return ppc.ibat[3].u;
		case SPR603E_DBAT0L:	return ppc.dbat[0].l;
		case SPR603E_DBAT0U:	return ppc.dbat[0].u;
		case SPR603E_DBAT1L:	return ppc.dbat[1].l;
		case SPR603E_DBAT1U:	return ppc.dbat[1].u;
		case SPR603E_DBAT2L:	return ppc.dbat[2].l;
		case SPR603E_DBAT2U:	return ppc.dbat[2].u;
		case SPR603E_DBAT3L:	return ppc.dbat[3].l;
		case SPR603E_DBAT3U:	return ppc.dbat[3].u;
	}
	
	ErrorLog("PowerPC read from an invalid register. Halting emulation until reset.");
	DebugLog("ppc: get_spr: unknown spr %d (%03X) !\n", spr, spr);
	ppc_halt();
	return 0;
}

static inline void ppc_set_msr(UINT32 value)
{
	if( value & (MSR_ILE | MSR_LE) )
	{
		ErrorLog("PowerPC entered an unemulated mode. Halting emulation until reset.");
		DebugLog("ppc: set_msr: little_endian mode not supported !\n");
		ppc_halt();
	}

	MSR = value;

	ppc603_check_interrupts();
}

static inline UINT32 ppc_get_msr(void)
{
	return MSR;
}

static inline UINT32 ppc_get_cr(void)
{
	return CR(0) << 28 | CR(1) << 24 | CR(2) << 20 | CR(3) << 16 | CR(4) << 12 | CR(5) << 8 | CR(6) << 4 | CR(7);
}

/***********************************************************************/

static void (* optable19[1024])(UINT32);
static void (* optable31[1024])(UINT32);
static void (* optable59[1024])(UINT32);
static void (* optable63[1024])(UINT32);
static void (* optable[64])(UINT32);

/*
 * Which instructions the emulated processor actually spends its time on.
 *
 * Everything left to do to the interpreter is a choice between shapes of work
 * that are expensive to build and impossible to rank by reading the code:
 * writing out more handlers by hand, joining common pairs into one, or leaving
 * the interpreter alone. Guessing which instructions matter is how a fortnight
 * gets spent making four percent of the run faster.
 *
 * One in every 1024 executed instructions is counted, by primary opcode and,
 * for the integer group that holds most of them, by extended opcode too. The
 * sample costs a test and a branch in the inner loop, which is real, and it is
 * meant to be taken out again once it has said what it has to say.
 */
static UINT32	OpHist[64];
static UINT32	OpHist31[1024];

static inline void ppc_sample_opcode(UINT32 opcode)
{
	const UINT32 primary = opcode >> 26;
	OpHist[primary]++;
	if (primary == 31)
		OpHist31[(opcode >> 1) & 0x3ff]++;
}

// Copied out and then cleared, so each report describes the interval since the
// last one. Running totals were worse than useless here: a game spends its
// first few seconds booting, and once those samples are in the denominator
// they hold the figures away from what the game is doing now for the rest of
// the session.
void ppc_op_histogram(UINT32 *primary, UINT32 *ext31)
{
	memcpy(primary, OpHist, sizeof(OpHist));
	memcpy(ext31, OpHist31, sizeof(OpHist31));
	memset(OpHist, 0, sizeof(OpHist));
	memset(OpHist31, 0, sizeof(OpHist31));
}

/*
 * Loops that only wait.
 *
 * A game spends most of its emulated cycles doing nothing. It finishes the
 * work for a frame and then sits reading a word of memory over and over until
 * an interrupt handler changes it, and on this hardware that is not a small
 * share of the time: measured over a run of L.A. Machineguns, ninety-two
 * percent of everything executed was a load, a compare and a branch backwards,
 * with almost no arithmetic and almost no stores between them. Emulating that
 * faster is emulating waiting faster.
 *
 * It can be skipped outright rather than approximated, because of how the main
 * board is driven. CModel3::RunMainBoardFrame runs the processor in four
 * hundred and twenty-four slices a frame and services every device between
 * them, so within one slice nothing outside the processor moves. If a loop
 * cannot end without something outside changing, and nothing outside can
 * change until the slice does, then the loop runs to the end of the slice and
 * the only question is whether the cycles are spent finding that out.
 *
 * Proving a loop is that kind takes two halves. This half is static and runs
 * once, when the branch is decoded: the body has to be short, and it has to
 * hold nothing but loads, compares and register logic, with no stores, no
 * branches and nothing that reaches a special register. On top of that, every
 * register the body reads and also writes must be written before it is read,
 * which makes the body a function of registers it does not touch and of memory
 * alone. That last rule is what excludes a loop counting down to a timeout:
 * such a loop reads the counter it wrote last time round, so it is not the
 * same loop twice and it will end on its own.
 *
 * The other half is at run time, and is in ppc_note_spin.
 */

// Whether one instruction may appear in the body of a waiting loop, and which
// registers it reads and writes if it may.
static bool ppc_spin_fields(UINT32 op, UINT32 *reads, UINT32 *writes)
{
	const UINT32 primary = op >> 26;
	const UINT32 ra = (op >> 16) & 0x1f;
	const UINT32 rs = (op >> 21) & 0x1f;	// also RT, and RD
	const UINT32 rb = (op >> 11) & 0x1f;

	*reads = 0;
	*writes = 0;

	switch (primary)
	{
		// The D-form loads, whose destination is the field at 21 and whose
		// base is the one at 16. A base of zero means no base, not r0. The
		// update forms are absent on purpose: they write the base as well,
		// which is exactly the thing this is trying to rule out.
		// addi and addis have the same shape and the same rule about a base of
		// zero, and they are how a loop walks a pointer or forms a constant.
		// Neither touches the carry, which is what keeps them here and keeps
		// addic out.
		case 32: case 34: case 40: case 42: case 14: case 15:
			*reads = (ra != 0) ? (1u << ra) : 0;
			*writes = 1u << rs;
			return true;

		// Compares against an immediate. These write a condition field and no
		// register at all, and a condition field is only ever read by the
		// branch that ends the loop.
		case 10: case 11:
			*reads = 1u << ra;
			return true;

		// The D-form logicals and the rotate, whose destination is at 16 and
		// whose source is at 21. The other way round from a load, which is
		// worth saying because getting it backwards would let a loop through
		// that writes what it reads.
		case 21: case 24: case 25: case 26: case 27: case 28: case 29:
			*reads = 1u << rs;
			*writes = 1u << ra;
			return true;

		case 31:
			switch ((op >> 1) & 0x3ff)
			{
				// Compares.
				case 0: case 32:
					*reads = (1u << ra) | (1u << rb);
					return true;

				// The indexed loads.
				case 23: case 87: case 279: case 343:
					*reads = ((ra != 0) ? (1u << ra) : 0) | (1u << rb);
					*writes = 1u << rs;
					return true;

				// Register to register logic and shifts: and, andc, nor, or,
				// orc, xor, nand, slw, srw, sraw.
				case 28: case 60: case 124: case 284: case 412: case 444:
				case 476: case 24: case 536: case 792:
					*reads = (1u << rs) | (1u << rb);
					*writes = 1u << ra;
					return true;

				// The ones with no second register: srawi, extsh, extsb,
				// cntlzw.
				case 824: case 922: case 954: case 26:
					*reads = 1u << rs;
					*writes = 1u << ra;
					return true;
			}
			return false;
	}
	return false;
}

/*
 * Why a loop that looked like a waiting one was not taken to be one.
 *
 * A game that skips nothing when every other game skips half its budget is
 * not a game without a waiting loop; it is a loop this rejected, and which of
 * the tests rejected it is the whole question. Guessing at that from an
 * instruction mix is how an afternoon goes. So each reason gets a counter,
 * and the branches that were turned down get a handler that keeps count of
 * how often they actually go round: a reason rejected once and never executed
 * is noise, and a reason rejected once and executed a million times a second
 * is the answer.
 */
enum SpinWhy
{
	SPIN_WHY_RANGE = 0,	// the jump is too long, so the body is too long
	SPIN_WHY_OP,		// the body holds something that is not a pure read
	SPIN_WHY_DEP,		// the body reads a register it writes, so it moves on
	SPIN_WHY_COUNT
};

// How often each rejected loop has closed, and the last one of each to do so.
// Only ever written from the interpreter thread.
static UINT64	SpinRejectHits[SPIN_WHY_COUNT];
static UINT32	SpinRejectPc[SPIN_WHY_COUNT];
// The same for the run-time half: which of its four tests said no.
static UINT64	SpinMissHits[4];
static UINT64	SpinHits;

// Whether the conditional branch at `address` closes a loop that can only be
// ended from outside. See above. `why` is filled in when the answer is no.
static bool ppc_spin_loop(UINT32 address, INT32 displacement, SpinWhy *why)
{
	*why = SPIN_WHY_RANGE;

	// Backwards, and to somewhere this can read.
	if (displacement >= 0 || displacement < -(PPC_SPIN_MAX * 4))
		return false;
	const UINT32 span = (UINT32) (-displacement) >> 2;
	const UINT32 target = address + (UINT32) displacement;
	if (target >= address || address >= RAMSize)
		return false;

	// The body is the `span` instructions from the target up to the branch,
	// which sits at target + span * 4, where this started. Read straight out
	// of RAM: Supermodel keeps it in the order the interpreter fetches it.
	UINT32 reads[PPC_SPIN_MAX];
	UINT32 writes[PPC_SPIN_MAX];
	UINT32 clobbered = 0;
	for (UINT32 i = 0; i < span; i++)
	{
		UINT32 word = *(UINT32 *) &RAM[target + i * 4];
		if (!ppc_spin_fields(word, &reads[i], &writes[i]))
		{
			*why = SPIN_WHY_OP;
			return false;
		}
		clobbered |= writes[i];
	}

	// Nothing may read a register the body writes before the body has written
	// it. That is what makes every pass round the loop identical.
	UINT32 written = 0;
	for (UINT32 i = 0; i < span; i++)
	{
		if (reads[i] & clobbered & ~written)
		{
			*why = SPIN_WHY_DEP;
			return false;
		}
		written |= writes[i];
	}
	return true;
}

/*
 * The run-time half of proving a loop is waiting.
 *
 * Called from the branch that closes a loop the analysis above accepted, on
 * the path where the branch is taken, so the loop is about to go round again.
 * Three things have to hold against the last time this same branch was taken:
 * it must be the same branch, exactly the loop's own length of instructions
 * must have been executed since, and nothing must have gone outside RAM. The
 * count is what rules out an interrupt having run and changed something in the
 * meantime; the bus check is what rules out the loop reading a device register
 * whose value moves on its own, or whose reading has an effect.
 *
 * When all three hold, the loop is reading memory that nothing can change
 * before the slice ends, so the rest of the slice is handed to the clock
 * rather than executed. The timebase and the decrementer are worked out from
 * the count at the end of ppc_execute, so time passes exactly as it would
 * have; the instructions simply do not.
 *
 * The stop is one above the count the loop is running down to, because the
 * loop decrements once more before testing.
 */
static inline void ppc_note_spin_span(UINT32 span)
{
	// The body and the branch itself, which is why it is one more than the
	// distance the branch jumps. Written as four tallies rather than one
	// condition so that a loop which is never skipped says which test it
	// fails, which is the only question worth asking about one.
	if (ppc.spin_pc != ppc.pc)
		SpinMissHits[0]++;
	else if (ppc.spin_bus != BusTouches)
		SpinMissHits[1]++;
	else if ((UINT32) (ppc.spin_icount - ppc.icount) != span + 1)
		SpinMissHits[2]++;
	else if (ppc.icount <= ppc.icount_stop + 1)
		SpinMissHits[3]++;
	else
	{
		SpinHits++;
		ppc.spin_skipped += (UINT64) (ppc.icount - (ppc.icount_stop + 1));
		ppc.icount = ppc.icount_stop + 1;
	}

	ppc.spin_pc = ppc.pc;
	ppc.spin_icount = ppc.icount;
	ppc.spin_bus = BusTouches;
}

// The conditional form, whose jump is the sixteen bit field.
static inline void ppc_note_spin(UINT32 op)
{
	ppc_note_spin_span((UINT32) (-(((INT32)(INT16)(op & 0xffff)) & ~0x3)) >> 2);
}

// How many instructions were skipped rather than executed. Against the two
// dispatch counts, this says how much of the emulated processor's time was
// spent waiting.
void ppc_idle_skipped(UINT64 *skipped)
{
	*skipped = ppc.spin_skipped;
}

/*
 * One line saying why a game is not skipping its waiting loop.
 *
 * Built here rather than by the caller because everything it needs is here:
 * the tallies, and the RAM the loop is in. The busiest rejection wins, and
 * the loop it names is dumped word for word, because the reason on its own
 * says which rule was broken and not which instruction broke it.
 *
 * Reading clears the tallies, so each line describes the interval before it.
 * A game that has settled into a loop it cannot skip prints the same line
 * every second, which is exactly the shape the problem has.
 */
void ppc_idle_report(char *out, size_t size)
{
	static const char *const kWhy[SPIN_WHY_COUNT] =
		{ "body-too-long", "body-op", "body-writes-what-it-reads" };
	static const char *const kMiss[4] =
		{ "other-branch", "bus-touched", "wrong-count", "slice-over" };

	// snprintf returns what it would have written, not what it did, so adding
	// its return to a cursor walks off the end of a buffer that filled up. One
	// cursor that stops at the end keeps every line below honest.
	size_t at = 0;
	const auto add = [&](const char *format, auto... rest)
	{
		if (at + 1 < size)
		{
			const int wrote = snprintf(out + at, size - at, format, rest...);
			at = (wrote < 0 || (size_t) wrote >= size - at) ? size - 1
			                                                : at + (size_t) wrote;
		}
	};

	add("idle: skipped %llu", (unsigned long long) SpinHits);
	for (int i = 0; i < 4; i++)
	{
		if (SpinMissHits[i] != 0)
			add(" %s %llu", kMiss[i], (unsigned long long) SpinMissHits[i]);
	}

	// Whichever rejected loop went round most is the one worth naming.
	int worst = -1;
	for (int i = 0; i < SPIN_WHY_COUNT; i++)
	{
		if (SpinRejectHits[i] != 0 &&
		    (worst < 0 || SpinRejectHits[i] > SpinRejectHits[worst]))
			worst = i;
	}
	if (worst >= 0)
	{
		// A relative branch jumps from its own address, which is what the
		// interpreter holds in ppc.pc while the handler runs, so the recorded
		// address is the branch itself.
		const UINT32 branch = SpinRejectPc[worst];
		add(" | rejected %s %llu at %08X", kWhy[worst],
		    (unsigned long long) SpinRejectHits[worst], branch);
		if (branch < RAMSize)
		{
			const UINT32 op = *(UINT32 *) &RAM[branch];
			const INT32 displacement = (INT32)(INT16)(op & 0xffff) & ~0x3;
			const INT32 span = -displacement >> 2;
			add(" span %d:", span);

			// The body, with the instructions that broke the rule marked. The
			// rule says which of them was possible, not which one it was, and
			// the second is the one worth reading. Only as much of a long body
			// as fits: a loop of two hundred instructions has already said
			// what it had to say by being one.
			if (displacement < 0 && (UINT32) -displacement <= branch)
			{
				const UINT32 target = branch + (UINT32) displacement;
				for (INT32 i = 0; i < span && i < 12; i++)
				{
					const UINT32 word = *(UINT32 *) &RAM[target + (UINT32) i * 4];
					UINT32 reads, writes;
					add(" %s%08X",
					    ppc_spin_fields(word, &reads, &writes) ? "" : "!", word);
				}
			}
			add(" [%08X]", op);
		}
	}

	memset(SpinRejectHits, 0, sizeof(SpinRejectHits));
	memset(SpinMissHits, 0, sizeof(SpinMissHits));
	SpinHits = 0;
}

// The branch shapes that are worth a handler of their own. See ppc_ops.c.
static void ppc_bc_true(UINT32 op);
static void ppc_bc_false(UINT32 op);
static void ppc_bc_true_idle(UINT32 op);
static void ppc_bc_false_idle(UINT32 op);
// The pair again for a loop the analysis turned down, which behave exactly
// like the plain ones and keep a tally besides. See ppc_idle_report.
template <bool want, SpinWhy why> static void ppc_bc_watch_t(UINT32 op);
static void ppc_b_idle(UINT32 op);

// The arithmetic and logic worth settling RC and OE for. See ppc_ops.c.
template <bool kRc> static void ppc_and_t(UINT32 op);
template <bool kRc> static void ppc_or_t(UINT32 op);
template <bool kRc> static void ppc_xor_t(UINT32 op);
template <bool kRc> static void ppc_rlwinm_t(UINT32 op);
template <bool kRc, bool kOe> static void ppc_add_t(UINT32 op);
template <bool kRc, bool kOe> static void ppc_subf_t(UINT32 op);
static void ppc_blr(UINT32 op);
static void ppc_bctr(UINT32 op);

static PPCHandler ppc_decode(UINT32 opcode)
{
	// The fields a branch is made of never change, so which of them applies
	// is settled here, once, rather than on every execution. BO is the five
	// bits at 21, and the low two are the link and absolute bits.
	const UINT32 bo = (opcode >> 21) & 0x1f;
	const UINT32 lk_aa = opcode & 0x3;

	switch (opcode >> 26)
	{
		case 16:
			// Relative, no link: the ordinary if and the ordinary loop.
			//
			// BO is 011zy to branch on a condition bit being set and 001zy to
			// branch on it being clear, where z is reserved and y is the static
			// prediction hint. Neither changes what the instruction does, and
			// an interpreter has no pipeline to hint at, so all four values of
			// each pair are the same branch and every one of them belongs
			// here. Matching 01100 and 00100 exactly, as this did, sent every
			// hinted branch a compiler emits to the general handler instead:
			// beq- and bne+ are ordinary output, and on Spikeout the branch
			// that closes the loop the game waits in is one of them. That put
			// its waiting loop out of reach of the idle detector, which only
			// ever sees the two shapes named here, and left the game running a
			// hundred million instructions a second to sit still.
			if (lk_aa == 0)
			{
				if ((bo & 0x1c) == 0x0c)	return ppc_bc_true;
				if ((bo & 0x1c) == 0x04)	return ppc_bc_false;
			}
			return optable[16];
		case 19:
			// blr and bctr, both unconditional and both without link.
			if (bo == 0x14 && lk_aa == 0)
			{
				if (((opcode >> 1) & 0x3ff) == 16)	return ppc_blr;
				if (((opcode >> 1) & 0x3ff) == 528)	return ppc_bctr;
			}
			return optable19[(opcode >> 1) & 0x3ff];
		case 21:
			// rlwinm, which is every shift and every bitfield extract a
			// compiler emits, and by some distance the most common of these.
			return (opcode & 1) ? ppc_rlwinm_t<true> : ppc_rlwinm_t<false>;
		case 31:
		{
			// The extended field already has the overflow bit in it, so the
			// four combinations of RC and OE are four separate table slots
			// and picking between them costs nothing here.
			const UINT32 rc = opcode & 1;
			switch ((opcode >> 1) & 0x3ff)
			{
				case 28:	return rc ? ppc_and_t<true> : ppc_and_t<false>;
				case 444:	return rc ? ppc_or_t<true> : ppc_or_t<false>;
				case 316:	return rc ? ppc_xor_t<true> : ppc_xor_t<false>;
				case 266:	return rc ? ppc_add_t<true, false> : ppc_add_t<false, false>;
				case 266 + 512:	return rc ? ppc_add_t<true, true> : ppc_add_t<false, true>;
				case 40:	return rc ? ppc_subf_t<true, false> : ppc_subf_t<false, false>;
				case 40 + 512:	return rc ? ppc_subf_t<true, true> : ppc_subf_t<false, true>;
			}
			return optable31[(opcode >> 1) & 0x3ff];
		}
		case 59:	return optable59[(opcode >> 1) & 0x3ff];
		case 63:	return optable63[(opcode >> 1) & 0x3ff];
		default:	return optable[opcode >> 26];
	}
}

// What every entry holds until the instruction is first executed, and what an
// entry is put back to when the memory under it is written.
static void ppc_decode_stub(UINT32 op)
{
	PPCHandler handler = ppc_decode(op);
	// The loop has already stepped past this entry, so it is the one behind.
	PPCHandler *entry = ppc.dec_cursor - 1;
	const UINT32 address = (UINT32) (entry - Dec) << 2;

	// Whether this branch closes a loop that only waits. The walk over the
	// body is not cheap, which is exactly why it belongs here: once, when the
	// instruction is first seen, rather than every time it runs.
	if (IdleSkip)
	{
		// The conditional forms, whose jump is the sixteen bit field.
		const INT32 conditional = (INT32)(INT16)(op & 0xffff) & ~0x3;
		SpinWhy why = SPIN_WHY_RANGE;
		if (handler == ppc_bc_true || handler == ppc_bc_false)
		{
			const bool want = handler == ppc_bc_true;
			if (ppc_spin_loop(address, conditional, &why))
				handler = want ? ppc_bc_true_idle : ppc_bc_false_idle;
			else if (conditional < 0)
			{
				// A backward branch is a loop, so every one this turns down is
				// worth counting: the reason on its own says which rule was
				// broken, and the count says whether the loop it was broken in
				// is one the game spends its life in. Forward branches are
				// left alone, being the ordinary if.
				handler = want
					? (why == SPIN_WHY_RANGE ? ppc_bc_watch_t<true, SPIN_WHY_RANGE>
					 : why == SPIN_WHY_OP    ? ppc_bc_watch_t<true, SPIN_WHY_OP>
					                         : ppc_bc_watch_t<true, SPIN_WHY_DEP>)
					: (why == SPIN_WHY_RANGE ? ppc_bc_watch_t<false, SPIN_WHY_RANGE>
					 : why == SPIN_WHY_OP    ? ppc_bc_watch_t<false, SPIN_WHY_OP>
					                         : ppc_bc_watch_t<false, SPIN_WHY_DEP>);
			}
		}
		else if ((op >> 26) == 18 && (op & 0x3) == 0)
		{
			// A branch that always jumps backwards over a body doing nothing
			// but read is a loop with no way out at all: it cannot end even in
			// principle until an interrupt changes something. So it never
			// needs the run-time half, only the static one. The jump is the
			// twenty-six bit field here, sign extended, which is why the
			// analysis is handed a displacement rather than working it out.
			INT32 li = (INT32) (op & 0x3fffffc);
			if (li & 0x2000000)
				li |= (INT32) 0xfc000000;
			if (ppc_spin_loop(address, li, &why))
				handler = ppc_b_idle;
		}
	}

	*entry = handler;
	// This page holds code, so stores to it have something to invalidate from
	// here on. Four bytes an entry, four kilobytes a page.
	ppc_note_code(address);
	handler(op);
}


#include "ppc603.c"

/********************************************************************/

#include "ppc_ops.c"
#include "ppc_ops.h"

/* Initialization and shutdown */

void ppc_base_init(void)
{
	size_t i,j;

	memset(&ppc, 0, sizeof(ppc));

	for( i=0; i < 64; i++ ) {
		optable[i] = ppc_invalid;
	}
	for( i=0; i < 1024; i++ ) {
		optable19[i] = ppc_invalid;
		optable31[i] = ppc_invalid;
		optable59[i] = ppc_invalid;
		optable63[i] = ppc_invalid;
	}

	/* Fill the opcode tables */
	for( i=0; i < (sizeof(ppc_opcode_common) / sizeof(PPC_OPCODE)); i++ ) {

		switch(ppc_opcode_common[i].code)
		{
			case 19:
				optable19[ppc_opcode_common[i].subcode] = ppc_opcode_common[i].handler;
				break;

			case 31:
				optable31[ppc_opcode_common[i].subcode] = ppc_opcode_common[i].handler;
				break;

			case 59:
			case 63:
				break;

			default:
				optable[ppc_opcode_common[i].code] = ppc_opcode_common[i].handler;
		}

	}

	/* Calculate rotate mask table */
	for( i=0; i < 32; i++ ) {
		for( j=0; j < 32; j++ ) {
			int mb = i;
			int me = j;
			UINT32 mask = ((UINT32)0xFFFFFFFF >> mb) ^ ((me >= 31) ? 0 : ((UINT32)0xFFFFFFFF >> (me + 1)));
			if( mb > me )
				mask = ~mask;

			ppc_rotate_mask[i][j] = mask;
		}
	}
}

void ppc_init(const PPC_CONFIG *config)
{
	int pll_config = 0;
	float multiplier;
	int i ;

	// Until somebody says otherwise there is no directly reachable RAM and
	// every access goes to the bus. A previous machine's pointer must never
	// survive into this one.
	ppc_attach_ram(NULL, 0);
	ppc_attach_rom(NULL);

	ppc_base_init() ;

	optable[48] = ppc_lfs;
	optable[49] = ppc_lfsu;
	optable[50] = ppc_lfd;
	optable[51] = ppc_lfdu;
	optable[52] = ppc_stfs;
	optable[53] = ppc_stfsu;
	optable[54] = ppc_stfd;
	optable[55] = ppc_stfdu;
	optable31[631] = ppc_lfdux;
	optable31[599] = ppc_lfdx;
	optable31[567] = ppc_lfsux;
	optable31[535] = ppc_lfsx;
	optable31[595] = ppc_mfsr;
	optable31[659] = ppc_mfsrin;
	optable31[371] = ppc_mftb;
	optable31[210] = ppc_mtsr;
	optable31[242] = ppc_mtsrin;
	optable31[758] = ppc_dcba;
	optable31[759] = ppc_stfdux;
	optable31[727] = ppc_stfdx;
	optable31[983] = ppc_stfiwx;
	optable31[695] = ppc_stfsux;
	optable31[663] = ppc_stfsx;
	optable31[370] = ppc_tlbia;
	optable31[306] = ppc_tlbie;
	optable31[566] = ppc_tlbsync;
	optable31[310] = ppc_eciwx;
	optable31[438] = ppc_ecowx;

	optable63[264] = ppc_fabsx;
	optable63[21] = ppc_faddx;
	optable63[32] = ppc_fcmpo;
	optable63[0] = ppc_fcmpu;
	optable63[14] = ppc_fctiwx;
	optable63[15] = ppc_fctiwzx;
	optable63[18] = ppc_fdivx;
	optable63[72] = ppc_fmrx;
	optable63[136] = ppc_fnabsx;
	optable63[40] = ppc_fnegx;
	optable63[12] = ppc_frspx;
	optable63[26] = ppc_frsqrtex;
	optable63[22] = ppc_fsqrtx;
	optable63[20] = ppc_fsubx;
	optable63[583] = ppc_mffsx;
	optable63[70] = ppc_mtfsb0x;
	optable63[38] = ppc_mtfsb1x;
	optable63[711] = ppc_mtfsfx;
	optable63[134] = ppc_mtfsfix;
	optable63[64] = ppc_mcrfs;

	optable59[21] = ppc_faddsx;
	optable59[18] = ppc_fdivsx;
	optable59[24] = ppc_fresx;
	optable59[22] = ppc_fsqrtsx;
	optable59[20] = ppc_fsubsx;

	for(i = 0; i < 32; i++)
	{
		optable63[i * 32 | 29] = ppc_fmaddx;
		optable63[i * 32 | 28] = ppc_fmsubx;
		optable63[i * 32 | 25] = ppc_fmulx;
		optable63[i * 32 | 31] = ppc_fnmaddx;
		optable63[i * 32 | 30] = ppc_fnmsubx;
		optable63[i * 32 | 23] = ppc_fselx;

		optable59[i * 32 | 29] = ppc_fmaddsx;
		optable59[i * 32 | 28] = ppc_fmsubsx;
		optable59[i * 32 | 25] = ppc_fmulsx;
		optable59[i * 32 | 31] = ppc_fnmaddsx;
		optable59[i * 32 | 30] = ppc_fnmsubsx;
	}

	for(i = 0; i < 256; i++)
	{
		ppc_field_xlat[i] =
			((i & 0x80) ? 0xF0000000 : 0) |
			((i & 0x40) ? 0x0F000000 : 0) |
			((i & 0x20) ? 0x00F00000 : 0) |
			((i & 0x10) ? 0x000F0000 : 0) |
			((i & 0x08) ? 0x0000F000 : 0) |
			((i & 0x04) ? 0x00000F00 : 0) |
			((i & 0x02) ? 0x000000F0 : 0) |
			((i & 0x01) ? 0x0000000F : 0);
	}

	ppc.pvr = config->pvr;

	multiplier = (float)((config->bus_frequency_multiplier >> 4) & 0xf) +
				 (float)(config->bus_frequency_multiplier & 0xf) / 10.0f;
	ppc.bus_freq_multiplier = (int)(multiplier * 2);

	// tb and dec are incremented every four bus cycles, so calculate default timer ratio
	ppc.timer_ratio = 2 * ppc.bus_freq_multiplier;  
	
	switch (config->bus_frequency)
	{
		case BUS_FREQUENCY_16MHZ: ppc.cycles_per_second = (int)(multiplier * 16000000); break;
		case BUS_FREQUENCY_20MHZ: ppc.cycles_per_second = (int)(multiplier * 20000000); break;
		case BUS_FREQUENCY_25MHZ: ppc.cycles_per_second = (int)(multiplier * 25000000); break;
		case BUS_FREQUENCY_33MHZ: ppc.cycles_per_second = (int)(multiplier * 33000000); break;
		case BUS_FREQUENCY_40MHZ: ppc.cycles_per_second = (int)(multiplier * 40000000); break;
		case BUS_FREQUENCY_50MHZ: ppc.cycles_per_second = (int)(multiplier * 50000000); break;
		case BUS_FREQUENCY_60MHZ: ppc.cycles_per_second = (int)(multiplier * 60000000); break;
		case BUS_FREQUENCY_66MHZ: ppc.cycles_per_second = (int)(multiplier * 66000000); break;
		case BUS_FREQUENCY_75MHZ: ppc.cycles_per_second = (int)(multiplier * 75000000); break;
	}
	
	switch(config->pvr)
	{
		case PPC_MODEL_603E:	pll_config = mpc603e_pll_config[ppc.bus_freq_multiplier-1][config->bus_frequency]; break;
		case PPC_MODEL_603EV:	pll_config = mpc603ev_pll_config[ppc.bus_freq_multiplier-1][config->bus_frequency]; break;
		case PPC_MODEL_603R:	pll_config = mpc603r_pll_config[ppc.bus_freq_multiplier-1][config->bus_frequency]; break;
		default: break;
	}

	if (pll_config == -1)
	{
		//ErrorLog("PPC: Invalid bus/multiplier combination (bus frequency = %d, multiplier = %1.1f)", config->bus_frequency, multiplier);
	}

	ppc.hid1 = pll_config << 28;
}

void ppc_shutdown(void)
{

}

void ppc_set_irq_line(int irqline)
{
	if (irqline)
	{
		ppc.interrupt_pending |= 0x1;
		ppc603_check_interrupts();
	}
	else
	{
		ppc.interrupt_pending &= ~0x1;
	}
}

UINT32 ppc_get_pc(void)
{
	return ppc.pc;
}

void ppc_set_fetch(PPC_FETCH_REGION * fetch)
{
	ppc.fetch = fetch;
}

UINT64 ppc_total_cycles(void)
{
	return ppc.total_cycles + (UINT64)(ppc.cur_cycles - ppc.icount);
}

int ppc_get_cycles_per_sec()
{
	return ppc.cycles_per_second;
}

int ppc_get_bus_freq_multipler()
{
	return ppc.bus_freq_multiplier;
}

void ppc_set_timer_ratio(int ratio)
{
	ppc.timer_ratio = ratio;
}

int ppc_get_timer_ratio()
{
	return ppc.timer_ratio;
}

/******************************************************************************
 Supermodel Interface
******************************************************************************/

void ppc_attach_bus(IBus *BusPtr)
{
	Bus = BusPtr;
}

void ppc_save_state(CBlockFile *SaveState)
{
	SaveState->NewBlock("PowerPC", __FILE__);
	
	// Cycle counting
	SaveState->Write(&ppc.icount, sizeof(ppc.icount));
	SaveState->Write(&ppc.cur_cycles, sizeof(ppc.cur_cycles));
	SaveState->Write(&ppc.total_cycles, sizeof(ppc.total_cycles));
	
	// Registers
	SaveState->Write(ppc.r, sizeof(ppc.r));
	SaveState->Write(&ppc.pc, sizeof(ppc.pc));
	SaveState->Write(&ppc.npc, sizeof(ppc.npc));
	SaveState->Write(&ppc.lr, sizeof(ppc.lr));
	SaveState->Write(&ppc.ctr, sizeof(ppc.ctr));
	SaveState->Write(&ppc.xer, sizeof(ppc.xer));
	SaveState->Write(&ppc.msr, sizeof(ppc.msr));
	SaveState->Write(ppc.cr, sizeof(ppc.cr));
	SaveState->Write(&ppc.pvr, sizeof(ppc.pvr));
	SaveState->Write(&ppc.srr0, sizeof(ppc.srr0));
	SaveState->Write(&ppc.srr1, sizeof(ppc.srr1));
	SaveState->Write(&ppc.srr2, sizeof(ppc.srr2));
	SaveState->Write(&ppc.srr3, sizeof(ppc.srr3));
	SaveState->Write(&ppc.hid0, sizeof(ppc.hid0));	
	SaveState->Write(&ppc.hid1, sizeof(ppc.hid1));
	SaveState->Write(&ppc.hid2, sizeof(ppc.hid2));
	SaveState->Write(&ppc.sdr1, sizeof(ppc.sdr1));
	SaveState->Write(ppc.sprg, sizeof(ppc.sprg));
	SaveState->Write(&ppc.dsisr, sizeof(ppc.dsisr));
	SaveState->Write(&ppc.dar, sizeof(ppc.dar));
	SaveState->Write(&ppc.ear, sizeof(ppc.ear));
	SaveState->Write(&ppc.dmiss, sizeof(ppc.dmiss));
	SaveState->Write(&ppc.dcmp, sizeof(ppc.dcmp));
	SaveState->Write(&ppc.hash1, sizeof(ppc.hash1));
	SaveState->Write(&ppc.hash2, sizeof(ppc.hash2));
	SaveState->Write(&ppc.imiss, sizeof(ppc.imiss));
	SaveState->Write(&ppc.icmp, sizeof(ppc.icmp));
	SaveState->Write(&ppc.rpa, sizeof(ppc.rpa));
	SaveState->Write(ppc.ibat, sizeof(ppc.ibat));
	SaveState->Write(ppc.dbat, sizeof(ppc.dbat));
	
	// These are probably PPC 4xx registers, but who cares, save 'em anyway!
	SaveState->Write(&ppc.evpr, sizeof(ppc.evpr));
	SaveState->Write(&ppc.exier, sizeof(ppc.exier));
	SaveState->Write(&ppc.exisr, sizeof(ppc.exisr));
	SaveState->Write(&ppc.bear, sizeof(ppc.bear));
	SaveState->Write(&ppc.besr, sizeof(ppc.besr));
	SaveState->Write(&ppc.iocr, sizeof(ppc.iocr));
	SaveState->Write(ppc.br, sizeof(ppc.br));
	SaveState->Write(&ppc.iabr, sizeof(ppc.iabr));
	SaveState->Write(&ppc.esr, sizeof(ppc.esr));
	SaveState->Write(&ppc.iccr, sizeof(ppc.iccr));
	SaveState->Write(&ppc.dccr, sizeof(ppc.dccr));
	SaveState->Write(&ppc.pit, sizeof(ppc.pit));
	SaveState->Write(&ppc.pit_counter, sizeof(ppc.pit_counter));
	SaveState->Write(&ppc.pit_int_enable, sizeof(ppc.pit_int_enable));
	SaveState->Write(&ppc.tsr, sizeof(ppc.tsr));
	SaveState->Write(&ppc.dbsr, sizeof(ppc.dbsr));
	SaveState->Write(&ppc.sgr, sizeof(ppc.sgr));
	SaveState->Write(&ppc.pid, sizeof(ppc.pid));
	
	SaveState->Write(&ppc.reserved, sizeof(ppc.reserved));
	SaveState->Write(&ppc.reserved_address, sizeof(ppc.reserved_address));
	SaveState->Write(&ppc.external_int, sizeof(ppc.external_int));
	
	SaveState->Write(&ppc.tb, sizeof(ppc.tb));
	
	SaveState->Write(&ppc.dec, sizeof(ppc.dec));
	SaveState->Write(&ppc.timer_frac, sizeof(ppc.timer_frac));
	SaveState->Write(&ppc.fpscr, sizeof(ppc.fpscr));
	
	SaveState->Write(ppc.fpr, sizeof(ppc.fpr));
	SaveState->Write(ppc.sr, sizeof(ppc.sr));
}

void ppc_load_state(CBlockFile *SaveState)
{	
	// Every entry describes memory that is about to be replaced wholesale.
	ppc_invalidate_all();

	if (Result::OKAY != SaveState->FindBlock("PowerPC"))
	{
		ErrorLog("Unable to load PowerPC state. Save state file is corrupt.");
		return;
	}
	
	// Timer and decrementer
	SaveState->Read(&ppc.icount, sizeof(ppc.icount));
	SaveState->Read(&ppc.cur_cycles, sizeof(ppc.cur_cycles));
	SaveState->Read(&ppc.total_cycles, sizeof(ppc.total_cycles));
	
	// Registers
	SaveState->Read(ppc.r, sizeof(ppc.r));
	SaveState->Read(&ppc.pc, sizeof(ppc.pc));
	SaveState->Read(&ppc.npc, sizeof(ppc.npc));
	ppc_change_pc(ppc.npc);
	SaveState->Read(&ppc.lr, sizeof(ppc.lr));
	SaveState->Read(&ppc.ctr, sizeof(ppc.ctr));
	SaveState->Read(&ppc.xer, sizeof(ppc.xer));
	SaveState->Read(&ppc.msr, sizeof(ppc.msr));
	SaveState->Read(ppc.cr, sizeof(ppc.cr));
	SaveState->Read(&ppc.pvr, sizeof(ppc.pvr));
	SaveState->Read(&ppc.srr0, sizeof(ppc.srr0));
	SaveState->Read(&ppc.srr1, sizeof(ppc.srr1));
	SaveState->Read(&ppc.srr2, sizeof(ppc.srr2));
	SaveState->Read(&ppc.srr3, sizeof(ppc.srr3));
	SaveState->Read(&ppc.hid0, sizeof(ppc.hid0));	
	SaveState->Read(&ppc.hid1, sizeof(ppc.hid1));
	SaveState->Read(&ppc.hid2, sizeof(ppc.hid2));
	SaveState->Read(&ppc.sdr1, sizeof(ppc.sdr1));
	SaveState->Read(ppc.sprg, sizeof(ppc.sprg));
	SaveState->Read(&ppc.dsisr, sizeof(ppc.dsisr));
	SaveState->Read(&ppc.dar, sizeof(ppc.dar));
	SaveState->Read(&ppc.ear, sizeof(ppc.ear));
	SaveState->Read(&ppc.dmiss, sizeof(ppc.dmiss));
	SaveState->Read(&ppc.dcmp, sizeof(ppc.dcmp));
	SaveState->Read(&ppc.hash1, sizeof(ppc.hash1));
	SaveState->Read(&ppc.hash2, sizeof(ppc.hash2));
	SaveState->Read(&ppc.imiss, sizeof(ppc.imiss));
	SaveState->Read(&ppc.icmp, sizeof(ppc.icmp));
	SaveState->Read(&ppc.rpa, sizeof(ppc.rpa));
	SaveState->Read(ppc.ibat, sizeof(ppc.ibat));
	SaveState->Read(ppc.dbat, sizeof(ppc.dbat));
	
	SaveState->Read(&ppc.evpr, sizeof(ppc.evpr));
	SaveState->Read(&ppc.exier, sizeof(ppc.exier));
	SaveState->Read(&ppc.exisr, sizeof(ppc.exisr));
	SaveState->Read(&ppc.bear, sizeof(ppc.bear));
	SaveState->Read(&ppc.besr, sizeof(ppc.besr));
	SaveState->Read(&ppc.iocr, sizeof(ppc.iocr));
	SaveState->Read(ppc.br, sizeof(ppc.br));
	SaveState->Read(&ppc.iabr, sizeof(ppc.iabr));
	SaveState->Read(&ppc.esr, sizeof(ppc.esr));
	SaveState->Read(&ppc.iccr, sizeof(ppc.iccr));
	SaveState->Read(&ppc.dccr, sizeof(ppc.dccr));
	SaveState->Read(&ppc.pit, sizeof(ppc.pit));
	SaveState->Read(&ppc.pit_counter, sizeof(ppc.pit_counter));
	SaveState->Read(&ppc.pit_int_enable, sizeof(ppc.pit_int_enable));
	SaveState->Read(&ppc.tsr, sizeof(ppc.tsr));
	SaveState->Read(&ppc.dbsr, sizeof(ppc.dbsr));
	SaveState->Read(&ppc.sgr, sizeof(ppc.sgr));
	SaveState->Read(&ppc.pid, sizeof(ppc.pid));
	
	SaveState->Read(&ppc.reserved, sizeof(ppc.reserved));
	SaveState->Read(&ppc.reserved_address, sizeof(ppc.reserved_address));
	SaveState->Read(&ppc.external_int, sizeof(ppc.external_int));
	
	SaveState->Read(&ppc.tb, sizeof(ppc.tb));
	
	SaveState->Read(&ppc.dec, sizeof(ppc.dec));
	SaveState->Read(&ppc.timer_frac, sizeof(ppc.timer_frac));
	SaveState->Read(&ppc.fpscr, sizeof(ppc.fpscr));
	
	SaveState->Read(ppc.fpr, sizeof(ppc.fpr));
	SaveState->Read(ppc.sr, sizeof(ppc.sr));
}

UINT32 ppc_get_gpr(unsigned num)
{
	return ppc.r[num&31];
}

double ppc_get_fpr(unsigned num)
{
	return ppc.fpr[num&31].fd;
}

UINT32 ppc_get_lr(void)
{
	return ppc.lr;
}
	
UINT32 ppc_read_spr(unsigned spr)
{
	return ppc_get_spr(spr);
}

UINT32 ppc_read_sr(unsigned num)
{
	return ppc.sr[num&15];
}

/******************************************************************************
 Debugger Interface
******************************************************************************/

#ifdef SUPERMODEL_DEBUGGER
void ppc_attach_debugger(Debugger::CPPCDebug *PPCDebugPtr)
{
	if (PPCDebug != NULL)
		ppc_detach_debugger();
	PPCDebug = PPCDebugPtr;
	Bus = PPCDebug->AttachBus(Bus);
}

void ppc_detach_debugger()
{
	if (PPCDebug == NULL)
		return;
	Bus = PPCDebug->DetachBus(); 
	PPCDebug = NULL;
}

void ppc_break()
{
	if (PPCDebug != NULL)
		PPCDebug->ForceBreak(true);
}
#else  // SUPERMODEL_DEBUGGER
void ppc_break()
{	
	//
}
#endif // SUPERMODEL_DEBUGGER

void ppc_set_pc(UINT32 pc)
{
	ppc.pc = pc;
	ppc_change_pc(pc);
	ppc.npc = pc + 4;
}

UINT8 ppc_get_cr(unsigned num)
{
	return ppc.cr[num&7];
}

void ppc_set_cr(unsigned num, UINT8 val)
{
	ppc.cr[num&7] = val;
}

void ppc_set_gpr(unsigned num, UINT32 val)
{
	ppc.r[num&31] = val;
}

void ppc_set_fpr(unsigned num, double val)
{
	ppc.fpr[num&31].fd = val;
}

void ppc_write_spr(unsigned spr, UINT32 val)
{
	ppc_set_spr(spr, val);
}

void ppc_write_sr(unsigned num, UINT32 val)
{
	ppc.sr[num&15] = val;
}

UINT32 ppc_read_msr()
{
	return ppc_get_msr();
}
