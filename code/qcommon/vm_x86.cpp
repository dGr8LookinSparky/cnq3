/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// load time compiler and execution environment for x86, 32-bit and 64-bit

#include "vm_local.h"
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __FreeBSD__
#include <sys/types.h>
#endif

#ifndef _WIN32
#include <sys/mman.h> // for PROT_ stuff
#endif

/* need this on NX enabled systems (i386 with PAE kernel or
 * noexec32=on x86_64) */
#if defined(__linux__) || defined(__FreeBSD__)
#define VM_X86_MMAP
#endif

// bit mask:
// 1 - program stack overflow
// 2 - opcode stack overflow
// 4 - jump target range
// 8 - data read / write range
static const int vm_rtChecks = -1;

static void *VM_Alloc_Compiled( vm_t *vm, int codeLength, int tableLength );
static void VM_Destroy_Compiled( vm_t *vm );

/*
  -------------
  eax	scratch
  ebx*	dataBase
  ecx	scratch (required for shifts)
  edx	scratch (required for divisions)
  esi*	program stack
  edi*	opstack
  ebp*  current proc stack ( dataBase + program stack )
  -------------
  rax	scratch
  rbx*	dataBase
  rcx	scratch (required for shifts)
  rdx	scratch (required for divisions)
  rsi*	programStack
  rdi*	opstack
  rbp*  current proc stack ( dataBase + program stack )
  r8	instructionPointers
  r9    dataMask
  r12*  systemCall
  r13*  stackBottom
  r14*  opStackTop
  xmm0  scratch
  xmm1  scratch
  xmm2  scratch
  xmm3  scratch
  xmm4  scratch
  xmm5  scratch

  Windows  ABI: you are required to preserve the XMM6-XMM15 registers
  System V ABI: you don't have to preserve any of the XMM registers

  Example how data segment will look like during vmMain execution:
  | .... |
  |------| vm->programStack -=36 (8+12+16) // set by vmMain
  | ???? | +0 - unused
  | ???? | +4 - unused
  |-------
  | arg0 | +8  \
  | arg4 | +12  | - passed arguments, accessible from subroutines
  | arg8 | +16 /
  |------|
  | loc0 | +20 \
  | loc4 | +24  \ - locals, accessible only from local scope
  | loc8 | +28  /
  | lc12 | +32 /
  |------| vm->programStack -= 48 (8+40) // set by VM_CallCompiled()
  | ???? | +0 - unused
  | ???? | +4 - unused
  | arg0 | +8
  | arg1 | +12
  | arg2 | +16
  | arg3 | +20
  | arg4 | +24
  | arg5 | +28
  | arg6 | +32
  | arg7 | +36
  | arg8 | +40
  | arg9 | +44
  |------| vm->programStack = vm->dataMask + 1 // set by VM_Create()

  jump/call opStack rules:

  1) opStack must be 8 before conditional jump
  2) opStack must be 4 before unconditional jump
  3) opStack must be >=4 before OP_CALL
  4) opStack must remain the same after OP_CALL
  5) you may not jump in/call locations with opStack != 0

*/

#define ISS8(V) ( (V) >= -128 && (V) <= 127 )
#define ISU8(V) ( (V) >= 0 && (V) <= 127 )

#define REWIND(N) { compiledOfs -= (N); instructionOffsets[ ip-1 ] = compiledOfs; };

typedef enum
{
	REG_EAX = 0,
	REG_ECX
} reg_t;


typedef enum
{
	LAST_COMMAND_NONE = 0,
	LAST_COMMAND_MOV_EDI_EAX,
	LAST_COMMAND_MOV_EDI_CONST,
	LAST_COMMAND_MOV_EAX_EDI,
	LAST_COMMAND_MOV_EAX_EDI_CALL,
	LAST_COMMAND_SUB_DI_4,
	LAST_COMMAND_SUB_DI_8,
	LAST_COMMAND_STORE_FLOAT_EDI
} ELastCommand;

typedef enum
{
	FUNC_ENTR = 0,
	FUNC_CALL,
	FUNC_SYSC,
#if id386
	FUNC_FTOL,
#endif
	FUNC_BCPY,
	FUNC_PSOF,
	FUNC_OSOF,
	FUNC_BADJ,
	FUNC_ERRJ,
	FUNC_DATA,
	FUNC_LAST
} func_t;

static	byte     *code;
static	int      compiledOfs;
static	int      *instructionOffsets;
static	intptr_t *instructionPointers;

static  instruction_t *inst = NULL;
static  instruction_t *ci;
static  instruction_t *ni;

#if id386
static int fp_cw[2] = { 0x0000, 0x0F7F }; // [0] - current value, [1] - round towards zero
#endif

static	int			ip;
static	int			lastConst;
static	opcode_t	pop1;

static	ELastCommand	LastCommand;
static	int				floatStoreInstLength; // so we know by how much to rewind

int		funcOffset[FUNC_LAST];

#ifdef DEBUG_VM
static int	errParam = 0;
#endif

static void ErrJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code outside VM" );
}


static void BadJump( void )
{
	Com_Error( ERR_DROP, "program tried to execute code at bad location inside VM" );
}


static void BadStack( void )
{
	Com_Error( ERR_DROP, "program tried to overflow program stack" );
}


static void BadOpStack( void )
{
	Com_Error( ERR_DROP, "program tried to overflow opcode stack" );
}


static void BadData( void )
{
#ifdef DEBUG_VM
	Com_Error( ERR_DROP, "program tried to read/write out of data segment at %i", errParam );
#else
	Com_Error( ERR_DROP, "program tried to read/write out of data segment" );
#endif
}


static void (*const errJumpPtr)(void) = ErrJump;
static void (*const badJumpPtr)(void) = BadJump;
static void (*const badStackPtr)(void) = BadStack;
static void (*const badOpStackPtr)(void) = BadOpStack;
static void (*const badDataPtr)(void) = BadData;


static void VM_FreeBuffers( void )
{
	// should be freed in reversed allocation order
	Z_Free( instructionOffsets );
	Z_Free( inst );
}


#if id386
static qbool HasSSEFP( void )
{
	return ( cpu_features & CPU_SSE ) && ( cpu_features & CPU_SSE2 );
}
#endif


static void Emit1( int v )
{
	if ( code )
	{
		code[ compiledOfs ] = v;
	}
	compiledOfs++;

	LastCommand = LAST_COMMAND_NONE;
}


static void Emit4( int v )
{
	Emit1( v & 255 );
	Emit1( ( v >> 8 ) & 255 );
	Emit1( ( v >> 16 ) & 255 );
	Emit1( ( v >> 24 ) & 255 );
}


#if idx64
static void Emit8( int64_t v )
{
	Emit1( ( v >> 0 ) & 255 );
	Emit1( ( v >> 8 ) & 255 );
	Emit1( ( v >> 16 ) & 255 );
	Emit1( ( v >> 24 ) & 255 );
	Emit1( ( v >> 32 ) & 255 );
	Emit1( ( v >> 40 ) & 255 );
	Emit1( ( v >> 48 ) & 255 );
	Emit1( ( v >> 56 ) & 255 );
}
#endif


static void EmitPtr( const void *ptr )
{
#if idx64
	Emit8( (intptr_t)ptr );
#else
	Emit4( (intptr_t)ptr );
#endif
}


static int Hex( int c )
{
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'A' && c <= 'F' ) {
		return 10 + c - 'A';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return 10 + c - 'a';
	}

	VM_FreeBuffers();
	Com_Error( ERR_DROP, "Hex: bad char '%c'", c );

	return 0;
}


static void EmitString( const char *string )
{
	int		c1, c2;
	int		v;

	while ( 1 ) {
		c1 = string[0];
		c2 = string[1];

		v = ( Hex( c1 ) << 4 ) | Hex( c2 );
		Emit1( v );

		if ( !string[2] ) {
			break;
		}
		string += 3;
	}
}


static void EmitRexString( const char *string )
{
#if idx64
	Emit1( 0x48 );
#endif
	EmitString( string );
}


static void EmitAlign( int align )
{
	int i, n;

	n = compiledOfs & ( align - 1 );

	for ( i = 0; i < n ; i++ )
		EmitString( "90" );	// nop
}


static void EmitCommand( ELastCommand command )
{
	switch( command )
	{
		case LAST_COMMAND_MOV_EDI_EAX:
			EmitString( "89 07" );		// mov dword ptr [edi], eax
			break;

		case LAST_COMMAND_MOV_EAX_EDI:
			EmitString( "8B 07" );		// mov eax, dword ptr [edi]
			break;

		case LAST_COMMAND_SUB_DI_4:
			EmitRexString( "83 EF 04" );	// sub edi, 4
			break;

		case LAST_COMMAND_SUB_DI_8:
			EmitRexString( "83 EF 08" );	// sub edi, 8
			break;

		case LAST_COMMAND_STORE_FLOAT_EDI:
		{
			const int oldOffset = compiledOfs;
#if id386
			if ( HasSSEFP() )
#endif
				EmitString( "f3 0f 11 07" );	// movss dword ptr [edi], xmm0
#if id386
			else
				EmitString( "D9 1F" );			// fstp dword ptr [edi]
#endif
			floatStoreInstLength = compiledOfs - oldOffset;
			break;
		}

		default:
			break;
	}
	LastCommand = command;
}


static void EmitAddEDI4( vm_t *vm )
{
	if ( LastCommand == LAST_COMMAND_NONE )
	{
		EmitRexString( "83 C7 04" );			// add edi,4
		return;
	}

	if ( LastCommand == LAST_COMMAND_SUB_DI_4 ) // sub edi, 4
	{
#if idx64
		REWIND( 4 );
#else
		REWIND( 3 );
#endif
		LastCommand = LAST_COMMAND_NONE;
		return;
	}

	if ( LastCommand == LAST_COMMAND_SUB_DI_8 ) // sub edi, 8
	{
#if idx64
		REWIND( 4 );
#else
		REWIND( 3 );
#endif
		EmitCommand( LAST_COMMAND_SUB_DI_4 );
		return;
	}

	EmitRexString( "83 C7 04" );				// add edi,4
}


static void EmitMovEAXEDI( vm_t *vm )
{
	opcode_t pop = pop1;
	pop1 = OP_UNDEF;

	if ( LastCommand == LAST_COMMAND_NONE )
	{
		EmitString( "8B 07" );		// mov eax, dword ptr [edi]
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI )
		return;

	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI_CALL )
		return;

	if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) // mov [edi], eax
	{
		REWIND( 2 );
		LastCommand = LAST_COMMAND_NONE;
		return;
	}

	if ( pop == OP_DIVI || pop == OP_DIVU || pop == OP_MULI || pop == OP_MULU ||
		pop == OP_STORE4 || pop == OP_STORE2 || pop == OP_STORE1 )
	{
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EDI_CONST ) // mov dword ptr [edi], 0x12345678
	{
		REWIND( 6 );
		if ( lastConst == 0 ) {
			EmitString( "31 C0" );		// xor eax, eax
		} else {
			EmitString( "B8" );			// mov	eax, 0x12345678
			Emit4( lastConst );
		}
		return;
	}

	EmitString( "8B 07" );		    // mov eax, dword ptr [edi]
}


void EmitMovECXEDI( vm_t *vm )
{
	opcode_t pop = pop1;
	pop1 = OP_UNDEF;

	if ( LastCommand == LAST_COMMAND_NONE )
	{
		EmitString( "8B 0F" );		// mov ecx, dword ptr [edi]
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI_CALL )
	{
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EAX_EDI ) // mov eax, dword ptr [edi]
	{
		REWIND( 2 );
		EmitString( "8B 0F" );		// mov ecx, dword ptr [edi]
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) // mov [edi], eax
	{
		REWIND( 2 );
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}

	if (pop == OP_DIVI || pop == OP_DIVU || pop == OP_MULI || pop == OP_MULU ||
		pop == OP_STORE4 || pop == OP_STORE2 || pop == OP_STORE1 )
	{
		EmitString( "89 C1" );		// mov ecx, eax
		return;
	}

	if ( LastCommand == LAST_COMMAND_MOV_EDI_CONST ) // mov dword ptr [edi], 0x12345678
	{
		REWIND( 6 );
		EmitString( "B9" );			// mov ecx, 0x12345678
		Emit4( lastConst );
		return;
	}

	EmitString( "8B 0F" );		    // mov ecx, dword ptr [edi]
}


static void EmitCheckReg( vm_t *vm, int reg, int size )
{
	int n;

	if ( !( vm_rtChecks & 8 ) )
		return;

#ifdef DEBUG_VM
	EmitString( "50" );			// push eax
	EmitRexString( "B8" );		// mov eax, &errParam
	EmitPtr( &errParam );
	EmitString( "C7 00" );		// mov [rax], ip-1
	Emit4( ip-1 );
	EmitString( "58" );			// pop eax
#endif

	if ( reg == REG_EAX )
		EmitString( "3D" );		// cmp eax, 0x12345678
	else
		EmitString( "81 F9" );	// cmp ecx, 0x12345678

	Emit4( vm->dataMask - ( size - 1 ) ); // FIXME: use vm->dataSize?

	// error reporting
	EmitString( "0F 87" );		// ja +errorFunction
	n = funcOffset[FUNC_DATA] - compiledOfs;
	Emit4( n - 6 );
}


static int EmitLoadFloatEDI_SSE( vm_t *vm )
{
	// movss dword ptr [edi], xmm0
	if ( LastCommand == LAST_COMMAND_STORE_FLOAT_EDI )
	{
		if ( !vm )
			return 1;
		REWIND( floatStoreInstLength );
		LastCommand = LAST_COMMAND_NONE;
		return 1;
	}

	EmitString( "f3 0f 10 07" ); // movss xmm0, dword ptr [edi]
	return 0;
}


#if id386
static int EmitLoadFloatEDI_X87( vm_t *vm )
{
	// fstp dword ptr [edi]
	if ( LastCommand == LAST_COMMAND_STORE_FLOAT_EDI )
	{
		if ( !vm )
			return 1;
		REWIND( 2 );
		LastCommand = LAST_COMMAND_NONE;
		return 1;
	}

	EmitString( "D9 07" ); // fld dword ptr [edi]
	return 0;
}
#endif


static int EmitLoadFloatEDI( vm_t *vm )
{
#if id386
	if ( !HasSSEFP() )
		return EmitLoadFloatEDI_X87( vm );
#endif

	return EmitLoadFloatEDI_SSE( vm );
}


const char *FarJumpStr( int op, int *n )
{
	switch ( op )
	{
		case OP_EQF:
		case OP_EQ:  *n = 2; return "0F 84"; // je

		case OP_NEF:
		case OP_NE:  *n = 2; return "0F 85"; // jne

		case OP_LTI: *n = 2; return "0F 8C"; // jl
		case OP_LEI: *n = 2; return "0F 8E"; // jle
		case OP_GTI: *n = 2; return "0F 8F"; // jg
		case OP_GEI: *n = 2; return "0F 8D"; // jge

		case OP_LTF:
		case OP_LTU: *n = 2; return "0F 82"; // jb

		case OP_LEF:
		case OP_LEU: *n = 2; return "0F 86"; // jbe

		case OP_GTF:
		case OP_GTU: *n = 2; return "0F 87"; // ja

		case OP_GEF:
		case OP_GEU: *n = 2; return "0F 83"; // jae

		case OP_JUMP: *n = 1; return "E9";   // jmp
	};
	return NULL;
}


void EmitJump( vm_t *vm, instruction_t *i, int op, int addr )
{
	const char *str;
	int v, jump_size;

	v = instructionOffsets[ addr ] - compiledOfs;

	str = FarJumpStr( op, &jump_size );
	EmitString( str );
	Emit4( v - 4 - jump_size );
}


static void EmitCallAddr( vm_t *vm, int addr )
{
	int v;
	v = instructionOffsets[ addr ] - compiledOfs;
	EmitString( "E8" );
	Emit4( v - 5 );
}


static void EmitCallOffset(func_t Func)
{
	int  v;
	v = funcOffset[ Func ] - compiledOfs;
	EmitString( "E8" );		// call +funcOffset[ Func ]
	Emit4( v - 5 );
}


#ifdef _WIN32
#define SHADOW_BASE 40
#else // linux/*BSD ABI
#define SHADOW_BASE 8
#endif

#define PUSH_STACK  32
#define PARAM_STACK 128

static void EmitCallFunc(vm_t *vm)
{
	static int sysCallOffset = 0;
	int n;

	EmitString( "85 C0" );				// test eax, eax
	EmitString( "7C" );					// jl +offset (SystemCall)
	Emit1( sysCallOffset );				// will be valid after first pass
	sysCallOffset = compiledOfs;

	// jump target range check
	if ( vm_rtChecks & 4 ) {
		EmitString( "3D" );					// cmp eax, vm->instructionCount
		Emit4( vm->instructionCount );
		EmitString( "0F 83" );				// jae +funcOffset[FUNC_ERRJ]
		n = funcOffset[FUNC_ERRJ] - compiledOfs;
		Emit4( n - 6 );
	}

	EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4

	// save proc base and programStack
	EmitString( "55" );				// push ebp
	EmitString( "56" );				// push esi

	// calling another vm function
#if idx64
	EmitString( "41 FF 14 C0" );		// call dword ptr [r8+rax*8]
#else
	EmitString( "8D 0C 85" );			// lea ecx, [vm->instructionPointers+eax*4]
	EmitPtr( instructionPointers );
	EmitString( "FF 11" );				// call dword ptr [ecx]
#endif

	// restore proc base and programStack so there is
	// no need to validate programStack anymore
	EmitString( "5E" );				// pop esi
	EmitString( "5D" );				// pop ebp

	EmitString( "C3" );				// ret

sysCallOffset = compiledOfs - sysCallOffset;

	// systemCall:
	// convert negative num to system call number
	// and store right before the first arg
	EmitString( "F7 D0" );          // not eax

	EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4

	// we may jump here from ConstOptimize() also
funcOffset[FUNC_SYSC] = compiledOfs;

#if idx64
	// allocate stack for shadow(win32)+parameters
	EmitString( "48 81 EC" );				// sub rsp, 200
	Emit4( SHADOW_BASE + PUSH_STACK + PARAM_STACK );

	// save scratch registers
	EmitString( "48 8D 54 24" );			// lea rdx, [rsp+SHADOW_BASE]
	Emit1( SHADOW_BASE );
	EmitString( "48 89 32" );				// mov [rdx+00], rsi
	EmitString( "48 89 7A 08" );			// mov [rdx+08], rdi
	EmitString( "4C 89 42 10" );			// mov [rdx+16], r8
	EmitString( "4C 89 4A 18" );			// mov [rdx+24], r9

	// ecx = &int64_params[0]
	EmitString( "48 8D 4C 24" );			// lea rcx, [rsp+SHADOW_BASE+PUSH_STACK]
	Emit1( SHADOW_BASE + PUSH_STACK );

	// save syscallNum
	EmitString( "48 89 01" );				// mov [rcx], rax

	// vm->programStack = programStack - 4;
	EmitString( "48 BA" );					// mov rdx, &vm->programStack
	EmitPtr( &vm->programStack );
	EmitString( "8D 46 FC" );				// lea eax, [esi-4]
	EmitString( "89 02" );					// mov [rdx], eax

	// params = (vm->dataBase + programStack + 8);
	EmitString( "48 8D 74 33 08" );			// lea rsi, [rbx+rsi+8]

	// rcx = &int64_params[1]
	EmitString( "48 83 C1 08" );			// add rcx, 8

	// dest_params[1-15] = params[1-15];
	EmitString( "31 D2" );					// xor edx, edx
	// loop
	EmitString( "48 63 04 96" );			// movsxd rax, dword [rsi+rdx*4]
	EmitString( "48 89 04 D1" );			// mov qword ptr[rcx+rdx*8], rax
	EmitString( "48 83 C2 01" );			// add rdx, 1
	EmitString( "48 83 FA" );				// cmp rdx, 15
	Emit1( (PARAM_STACK/8) - 1 );
	EmitString( "7C EE" );					// jl -18

#ifdef _WIN32
	// rcx = &int64_params[0]
	EmitString( "48 83 E9 08" );			// sub rcx, 8
#else // linux/*BSD ABI
	// rdi = &int64_params[0]
	EmitString( "48 8D 79 F8" );			// lea rdi, [rcx-8]
#endif

	// currentVm->systemCall( param );
	EmitString( "41 FF 14 24" );			// call qword [r12]

	// restore registers
	EmitString( "48 8D 54 24" );			// lea rdx, [rsp+SHADOW_BASE]
	Emit1( SHADOW_BASE );
	EmitString( "48 8B 32" );				// mov rsi, [rdx+00]
	EmitString( "48 8B 7A 08" );			// mov rdi, [rdx+08]
	EmitString( "4C 8B 42 10" );			// mov r8,  [rdx+16]
	EmitString( "4C 8B 4A 18" );			// mov r9,  [rdx+24]

	// we added the return value: *(opstack+1) = eax
	EmitAddEDI4( vm );						// add edi, 4
	EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov [edi], eax

	// return stack
	EmitString( "48 81 C4" );				// add rsp, 200
	Emit4( SHADOW_BASE + PUSH_STACK + PARAM_STACK );

	EmitRexString( "8D 2C 33" );			// lea rbp, [rbx+rsi]

	EmitString( "C3" );						// ret

#else // i386

	// params = (int *)((byte *)currentVM->dataBase + programStack + 4);
	EmitString( "8D 4D 04" );				// lea ecx, [ebp+4]

	// function prologue
	EmitString( "55" );						// push ebp
	EmitRexString( "89 E5" );				// mov ebp, esp
	EmitRexString( "83 EC 04" );			// sub esp, 4
	// align stack before call
	EmitRexString( "83 E4 F0" );			// and esp, -16

	// ABI note: esi/edi must not change during call!

	// currentVM->programStack = programStack - 4;
	EmitString( "8D 56 FC" );				// lea edx, [esi-4]
	EmitString( "89 15" );					// mov [&vm->programStack], edx
	EmitPtr( &vm->programStack );

	// params[0] = syscallNum
	EmitString( "89 01" );					// mov [ecx], eax

	// cdecl - set params
	EmitString( "89 0C 24" );				// mov [esp], ecx

	// currentVm->systemCall( param );
	EmitString( "FF 15" );					// call dword ptr [&currentVM->systemCall]
	EmitPtr( &vm->systemCall );

	// we added the return value: *(opstack+1) = eax
#if 0
	EmitAddEDI4( vm );						// add edi, 4
	EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov [edi], eax
#else // break dependency from edi value?
	EmitString( "89 47 04" );				// mov [edi+4], eax
	EmitAddEDI4( vm );						// add edi, 4
#endif

	// function epilogue
	EmitRexString( "89 EC" );				// mov esp, ebp
	EmitString( "5D" );						// pop ebp
	EmitString( "C3" );						// ret
#endif
}


#if id386
static void EmitFTOLFunc(vm_t *vm)
{
	EmitRexString( "B8" );		// mov eax, &fp_cw[0]
	EmitPtr( &fp_cw[0] );
	EmitString( "9B D9 38" );	// fnstcw word ptr [eax]
	EmitString( "D9 68 04" );	// fldcw word ptr [eax+4]
	EmitString( "DB 1F" );		// fistp dword ptr [edi]
	EmitString( "D9 28" );		// fldcw word ptr [eax]
	EmitString( "C3" );			// ret
}
#endif


static void EmitBCPYFunc(vm_t *vm)
{
	// FIXME: range check
	EmitString( "56" );						// push esi
	EmitString( "57" );						// push edi
	EmitString( "8B 37" );					// mov esi,[edi]
	EmitString( "8B 7F FC" );				// mov edi,[edi-4]
	EmitString( "B8" );						// mov eax, datamask
	Emit4( vm->dataMask );
	EmitString( "21 C6" );					// and esi, eax
	EmitString( "21 C7" );					// and edi, eax
#if idx64
	EmitString( "48 01 DE" );				// add rsi, rbx
	EmitString( "48 01 DF" );				// add rdi, rbx
#else
	EmitString( "03 F3" );					// add esi, ebx
	EmitString( "03 FB" );					// add edi, ebx
#endif
	EmitString( "F3 A5" );					// rep movsd
	EmitString( "5F" );						// pop edi
	EmitString( "5E" );						// pop esi
	EmitCommand( LAST_COMMAND_SUB_DI_8 );	// sub edi, 8
	EmitString( "C3" );						// ret
}


static void EmitPSOFFunc(vm_t *vm)
{
	EmitRexString( "B8" );			// mov eax, badJumpPtr
	EmitPtr( &badStackPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


static void EmitOSOFFunc(vm_t *vm)
{
	EmitRexString( "B8" );			// mov eax, badOptackPtr
	EmitPtr( &badOpStackPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


static void EmitBADJFunc(vm_t *vm)
{
	EmitRexString( "B8" );			// mov eax, badJumpPtr
	EmitPtr( &badJumpPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


static void EmitERRJFunc(vm_t *vm)
{
	EmitRexString( "B8" );			// mov eax, badJumpPtr
	EmitPtr( &errJumpPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


static void EmitDATAFunc(vm_t *vm)
{
	EmitRexString( "B8" );			// mov eax, badDataPtr
	EmitPtr( &badDataPtr );
	EmitString( "FF 10" );			// call [eax]
	EmitString( "C3" );				// ret
}


#if id386
/*
=================
EmitFCalcEDI
=================
*/
static void EmitFCalcEDI(int op)
{
	switch ( op )
	{
		case OP_ADDF: EmitString( "D8 07" ); break; // fadd dword ptr [edi]
		case OP_SUBF: EmitString( "D8 27" ); break;	// fsub dword ptr [edi]
		case OP_MULF: EmitString( "D8 0F" ); break; // fmul dword ptr [edi]
		case OP_DIVF: EmitString( "D8 37" ); break; // fdiv dword ptr [edi]
		default: Com_Error( ERR_DROP, "bad float op" ); break;
	};
}


/*
=================
EmitFCalcPop
=================
*/
static void EmitFCalcPop(int op)
{
	switch ( op )
	{
		case OP_ADDF: EmitString( "DE C1" ); break; // faddp
		case OP_SUBF: EmitString( "DE E9" ); break; // fsubp
		case OP_MULF: EmitString( "DE C9" ); break; // fmulp
		case OP_DIVF: EmitString( "DE F9" ); break; // fdivp
		default: Com_Error( ERR_DROP, "bad opcode %02x", op ); break;
	};
}


/*
=================
CommuteFloatOp
=================
*/
static int CommuteFloatOp( int op )
{
	switch ( op ) {
		case OP_LEF: return OP_GEF;
		case OP_LTF: return OP_GTF;
		case OP_GEF: return OP_LEF;
		case OP_GTF: return OP_LTF;
		default: return op;
	}
}
#endif


/*
=================
ConstOptimize
=================
*/
static qboolean ConstOptimize(vm_t *vm)
{
	int v;
	int op1;

	op1 = ni->op;

	switch ( op1 ) {

	case OP_LOAD4:
		EmitAddEDI4( vm );
		if ( ISS8( ci->value ) ) {
			EmitString( "8B 43" );		// mov eax, dword ptr [ebx+0x7F]
			Emit1( ci->value );
		} else {
			EmitString( "8B 83" );		// mov eax, dword ptr [ebx+0x12345678]
			Emit4( ci->value );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX ); // mov dword ptr [edi], eax
		ip += 1;
		return qtrue;

	case OP_LOAD2:
		EmitAddEDI4( vm );
		if ( ISS8( ci->value ) ) {
			EmitString( "0F B7 43" );	// movzx eax, word ptr [ebx+0x7F]
			Emit1( ci->value );
		} else {
			EmitString( "0F B7 83" );	// movzx eax, word ptr [ebx+0x12345678]
			Emit4( ci->value );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX ); // mov dword ptr [edi], eax
		ip += 1;
		return qtrue;

	case OP_LOAD1:
		EmitAddEDI4( vm );
		if ( ISS8( ci->value ) ) {
			EmitString( "0F B6 43" );	// movzx eax, byte ptr [ebx+0x7F]
			Emit1( ci->value );
		} else {
			EmitString( "0F B6 83" );	// movzx eax, word ptr [ebx+0x12345678]
			Emit4( ci->value );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX ); // mov dword ptr [edi], eax
		ip += 1;
		return qtrue;

	case OP_STORE4:
		EmitMovEAXEDI( vm );
		if ( !ci->value ) {
			EmitString( "31 C9" );		// xor ecx, ecx
		} else {
			EmitString( "B9" );			// mov	ecx, 0x12345678
			Emit4( ci->value );
		}
		EmitCheckReg( vm, REG_EAX, 4 );
		EmitString( "89 0C 03" );     // mov dword ptr [ebx + eax], ecx
		EmitCommand( LAST_COMMAND_SUB_DI_4 );		// sub edi, 4
		ip += 1;
		return qtrue;

	case OP_STORE2:
		EmitMovEAXEDI( vm );
		if ( !ci->value ) {
			EmitString( "31 C9" );		// xor ecx, ecx
		} else {
			EmitString( "B9" );			// mov	ecx, 0x12345678
			Emit4( ci->value );
		}
		EmitCheckReg( vm, REG_EAX, 2 );
		EmitString( "66 89 0C 03" );   // mov word ptr [ebx + eax], cx
		EmitCommand( LAST_COMMAND_SUB_DI_4 ); // sub edi, 4
		ip += 1;
		return qtrue;

	case OP_STORE1:
		EmitMovEAXEDI( vm );
		if ( !ci->value ) {
			EmitString( "31 C9" );		// xor ecx, ecx
		} else {
			EmitString( "B9" );			// mov	ecx, 0x12345678
			Emit4( ci->value );
		}
		EmitCheckReg( vm, REG_EAX, 1 );
		EmitString( "88 0C 03" );		// mov byte ptr [ebx + eax], cl
		EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
		ip += 1;
		return qtrue;

	case OP_ADD:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISS8( v ) ) {
			EmitString( "83 C0" );	// add eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "05" );	    // add eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1; // OP_ADD
		return qtrue;

	case OP_SUB:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISS8( v ) ) {
			EmitString( "83 E8" );	// sub eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "2D" );		// sub eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_MULI:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISS8( v ) ) {
			EmitString( "6B C0" );	// imul eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "69 C0" );	// imul eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_MULF:
	case OP_DIVF:
	case OP_ADDF:
	case OP_SUBF:
		v = ci->value;
#if id386
		if ( HasSSEFP() ) {
#endif
			EmitLoadFloatEDI( vm );
			EmitString( "C7 45 00" );							// mov dword ptr [ebp], v
			Emit4( v );
			EmitString( "f3 0f 10 4d 00" );						// movss xmm1, dword ptr [ebp]
			switch( op1 ) {
				case OP_ADDF: EmitString( "0f 58 c1" ); break;	// addps xmm0, xmm1
				case OP_SUBF: EmitString( "0f 5c c1" ); break;	// subps xmm0, xmm1
				case OP_MULF: EmitString( "0f 59 c1" ); break;	// mulps xmm0, xmm1
				case OP_DIVF: EmitString( "0f 5e c1" ); break;	// divps xmm0, xmm1
			}
#if id386
		} else {
			EmitLoadFloatEDI( vm );
			EmitString( "C7 45 00" );	// mov [ebp], 0x12345678
			Emit4( v );
			EmitString( "D9 45 00" );	// fld dword ptr [ebp]
			EmitFCalcPop( op1 );		// fmulp/fdivp/faddp/fsubp
		}
#endif
		EmitCommand( LAST_COMMAND_STORE_FLOAT_EDI );
		ip +=1;
		return qtrue;

	case OP_LSH:
		v = ci->value;
		if ( v < 0 || v > 31 )
			break;
		EmitMovEAXEDI( vm );
		EmitString( "C1 E0" );	// shl eax, 0x12
		Emit1( v );
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1; // OP_LSH
		return qtrue;

	case OP_RSHI:
		v = ci->value;
		if ( v < 0 || v > 31 )
			break;
		EmitMovEAXEDI( vm );
		EmitString( "C1 F8" );	// sar eax, 0x12
		Emit1( v );
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_RSHU:
		v = ci->value;
		if ( v < 0 || v > 31 )
			break;
		EmitMovEAXEDI( vm );
		EmitString( "C1 E8" );	// shr eax, 0x12
		Emit1( v );
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_BAND:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISU8( v ) ) {
			EmitString( "83 E0" ); // and eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "25" ); // and eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_BOR:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISU8( v ) ) {
			EmitString( "83 C8" ); // or eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "0D" );    // or eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_BXOR:
		v = ci->value;
		EmitMovEAXEDI( vm );
		if ( ISU8( v ) ) {
			EmitString( "83 F0" ); // xor eax, 0x7F
			Emit1( v );
		} else {
			EmitString( "35" );    // xor eax, 0x12345678
			Emit4( v );
		}
		EmitCommand( LAST_COMMAND_MOV_EDI_EAX );
		ip += 1;
		return qtrue;

	case OP_JUMP:
		EmitJump( vm, ni, ni->op, ci->value );
		ip += 1; // OP_JUMP
		return qtrue;

	case OP_CALL:
#ifdef VM_LOG_SYSCALLS
		// [dataBase + programStack + 0] = ip;
		EmitString( "C7 45 00" );	// mov dword ptr [ebp], 0x12345678
		Emit4( ip );
#endif
		v = ci->value;
		// try to inline some syscalls
#if id386
		if ( HasSSEFP() ) {
#endif
			// we let the C run-time handle sin/cos because they'll do better than the x87 crap
			if ( v == ~TRAP_SQRT ) {
				EmitString( "f3 0f 10 45 08" );		// movss xmm0, dword ptr [ebp + 8]
				EmitAddEDI4( vm );
				EmitString( "f3 0f 51 c0" );		// sqrtss xmm0, xmm0
				EmitCommand( LAST_COMMAND_STORE_FLOAT_EDI );
				ip += 1;
				return qtrue;
			}
#if id386
		} else if ( v == ~TRAP_SIN || v == ~TRAP_COS || v == ~TRAP_SQRT ) {
			EmitString( "D9 45 08" );						// fld dword ptr [ebp + 8]
			switch ( v ) {
				case ~TRAP_SQRT: EmitString( "D9 FA" ); break;
				case ~TRAP_SIN:  EmitString( "D9 FE" ); break;
				case ~TRAP_COS:  EmitString( "D9 FF" ); break;
			}
			EmitAddEDI4( vm );
			EmitCommand( LAST_COMMAND_STORE_FLOAT_EDI );	// fstp dword ptr[edi]
			ip += 1;
			return qtrue;
		}
#endif

		if ( v < 0 ) // syscall
		{
			EmitString( "B8" );		// mov eax, 0x12345678
			Emit4( ~v );
			EmitCallOffset( FUNC_SYSC );
			LastCommand = LAST_COMMAND_MOV_EAX_EDI_CALL;
			ip += 1; // OP_CALL
			return qtrue;
		}
		EmitString( "55" );	// push ebp
		EmitString( "56" );	// push rsi
		EmitString( "53" );	// push rbx
		EmitCallAddr( vm, v ); // call +addr
		EmitString( "5B" );	// pop rbx
		EmitString( "5E" );	// pop rsi
		EmitString( "5D" );	// pop ebp
		ip += 1; // OP_CALL
		return qtrue;

	case OP_EQF:
	case OP_NEF:
	case OP_LTF:
	case OP_LEF:
	case OP_GTF:
	case OP_GEF:
#if id386
		if ( HasSSEFP() ) {
#endif
			EmitLoadFloatEDI( vm );
			EmitCommand( LAST_COMMAND_SUB_DI_4 );
			v = ci->value;
			if ( v == 0 ) {
				EmitString( "0f 57 c9" );			// xorps xmm1, xmm1
			} else {
				EmitString( "C7 45 00" );			// mov dword ptr [ebp], v
				Emit4( v );
				EmitString( "f3 0f 10 4d 00" );		// movss xmm1, dword ptr [ebp]
			}
			EmitString( "0f 2f c1" );				// comiss xmm0, xmm1
			EmitJump( vm, ni, ni->op, ni->value );
#if id386
		} else {
			EmitLoadFloatEDI( vm );
			EmitCommand( LAST_COMMAND_SUB_DI_4 );
			v = ci->value;
			if ( v == 0 ) {
				EmitString( "D9 EE" );		// fldz
			} else {
				EmitString( "C7 45 00" );	// mov [ebp], 0x12345678
				Emit4( v );
				EmitString( "D9 45 00" );	// fld dword ptr [ebp]
			}
			EmitString( "DF E9" );			// fucomip - requires a P6 class CPU (Pentium Pro)
			EmitString( "DD D8" );			// fstp st(0)
			EmitJump( vm, ni, CommuteFloatOp( ni->op ), ni->value );
		}
#endif
		ip +=1;
		return qtrue;

	case OP_EQ:
	case OP_NE:
	case OP_GEI:
	case OP_GTI:
	case OP_GTU:
	case OP_GEU:
	case OP_LTU:
	case OP_LEU:
	case OP_LEI:
	case OP_LTI:
		EmitMovEAXEDI( vm );
		EmitCommand( LAST_COMMAND_SUB_DI_4 );
		v = ci->value;
		if ( v == 0 && ( op1 == OP_EQ || op1 == OP_NE ) ) {
			EmitString( "85 C0" );       // test eax, eax
		} else {
			if ( ISS8( v ) ) {
				EmitString( "83 F8" );   // cmp eax, 0x7F
				Emit1( v );
			} else {
				EmitString( "3D" );      // cmp eax, 0xFFFFFFFF
				Emit4( v );
			}
		}
		EmitJump( vm, ni, ni->op, ni->value );
		ip += 1;
		return qtrue;

	default:
		break;
	}

	return qfalse;
}


/*
=================
VM_FindMOps

Search for known macro-op sequences
=================
*/
static void VM_FindMOps(instruction_t *buf, int instructionCount)
{
	int i, v, op0;
	instruction_t *ci;

	ci = buf;
	i = 0;

	while ( i < instructionCount )
	{
		op0 = ci->op;
		if ( op0 == OP_LOCAL ) {
			// OP_LOCAL + OP_LOCAL + OP_LOAD4 + OP_CONST + OP_XXX + OP_STORE4
			if ( (ci+1)->op == OP_LOCAL && ci->value == (ci+1)->value && (ci+2)->op == OP_LOAD4 && (ci+3)->op == OP_CONST && (ci+4)->op != OP_UNDEF && (ci+5)->op == OP_STORE4 ) {
				v = (ci+4)->op;
				if ( v == OP_ADD ) {
					ci->op = MOP_ADD4;
					ci += 6; i += 6;
					continue;
				}
				if ( v == OP_SUB ) {
					ci->op = MOP_SUB4;
					ci += 6; i += 6;
					continue;
				}
				if ( v == OP_BAND ) {
					ci->op = MOP_BAND4;
					ci += 6; i += 6;
					continue;
				}
				if ( v == OP_BOR ) {
					ci->op = MOP_BOR4;
					ci += 6; i += 6;
					continue;
				}
			}

			// skip useless sequences
			if ( (ci+1)->op == OP_LOCAL && (ci+0)->value == (ci+1)->value && (ci+2)->op == OP_LOAD4 && (ci+3)->op == OP_STORE4 ) {
				ci->op = MOP_IGNORE4;
				ci += 4; i += 4;
				continue;
			}
		}

		ci++;
		i++;
	}
}


/*
=================
EmitMOPs
=================
*/
static qboolean EmitMOPs(vm_t *vm, int op)
{
	int v, n;
	switch ( op )
	{
		//[local] += CONST
		case MOP_ADD4:
			n = inst[ip+2].value;
			v = ci->value; // local variable address
			if ( ISS8( n ) ) {
				if ( ISS8( v ) ) {
					EmitString( "83 45" );	// add dword ptr [ebp + 0x7F], 0x12
					Emit1( v );
					Emit1( n );
				} else {
					EmitString( "83 85" );	// add dword ptr [ebp + 0x12345678], 0x12
					Emit4( v );
					Emit1( n );
				}
			} else {
				if ( ISS8( v ) ) {
					EmitString( "81 45" );	// add dword ptr [ebp + 0x7F], 0x12345678
					Emit1( v );
					Emit4( n );
				} else {
					EmitString( "81 85" );	// add dword ptr [ebp + 0x12345678], 0x12345678
					Emit4( v );
					Emit4( n );
				}
			}
			ip += 5;
			return qtrue;

		//[local] -= CONST
		case MOP_SUB4:
			n = inst[ip+2].value;
			v = ci->value; // local variable address
			if ( ISS8( n ) ) {
				if ( ISS8( v ) ) {
					EmitString( "83 6D" );	// sub dword ptr [ebp + 0x7F], 0x12
					Emit1( v );
					Emit1( n );
				} else {
					EmitString( "83 AD" );	// sub dword ptr [ebp + 0x12345678], 0x12
					Emit4( v );
					Emit1( n );
				}
			} else {
				if ( ISS8( v ) ) {
					EmitString( "81 6D" );	// sub dword ptr [ebp + 0x7F], 0x12345678
					Emit1( v );
					Emit4( n );
				} else {
					EmitString( "81 AD" );	// sub dword ptr[esi+0x12345678], 0x12345678
					Emit4( v );
					Emit4( n );
				}
			}
			ip += 5;
			return qtrue;

		//[local] &= CONST
		case MOP_BAND4:
			n = inst[ip+2].value;
			v = ci->value; // local variable address
			if ( ISS8( n ) ) {
				if ( ISS8( v ) ) {
					EmitString( "83 65" );	// and dword ptr [ebp + 0x7F], 0x12
					Emit1( v );
					Emit1( n );
				} else {
					EmitString( "83 A5" );	// and dword ptr [ebp + 0x12345678], 0x12
					Emit4( v );
					Emit1( n );
				}
			} else {
				if ( ISS8( v ) ) {
					EmitString( "81 65" );	// and dword ptr [ebp + 0x7F], 0x12345678
					Emit1( v );
					Emit4( n );
				} else {
					EmitString( "81 A5" );	// and dword ptr [ebp + 0x12345678], 0x12345678
					Emit4( v );
					Emit4( n );
				}
			}
			ip += 5;
			return qtrue;

		//[local] |= CONST
		case MOP_BOR4:
			n = inst[ip+2].value;
			v = ci->value; // local variable address
			if ( ISS8( n ) ) {
				if ( ISS8( v ) ) {
					EmitString( "83 4D" );	// or dword ptr [ebp + 0x7F], 0x12
					Emit1( v );
					Emit1( n );
				} else {
					EmitString( "83 8D" );	// or dword ptr [ebp + 0x12345678], 0x12
					Emit4( v );
					Emit1( n );
				}
			} else {
				if ( ISS8( v ) ) {
					EmitString( "81 4D" );	// or dword ptr [ebp + 0x7F], 0x12345678
					Emit1( v );
					Emit4( n );
				} else {
					EmitString( "81 8D" );	// or dword ptr [ebp + 0x12345678], 0x12345678
					Emit4( v );
					Emit4( n );
				}
			}
			ip += 5;
			return qtrue;

		// [local] = [local]
		case MOP_IGNORE4:
			ip += 3;
			return qtrue;

	};
	return qfalse;
}


/*
=================
VM_Compile
=================
*/
qboolean VM_Compile( vm_t *vm, vmHeader_t *header ) {
	const char *errMsg;
	int     instructionCount;
	int		proc_base;
	int		proc_len;
	int		i, n, v;

	inst = (instruction_t*)Z_Malloc((header->instructionCount + 8) * sizeof(instruction_t));
	instructionOffsets = (int*)Z_Malloc( header->instructionCount * sizeof( int ) );

	errMsg = VM_LoadInstructions( header, inst );
	if ( !errMsg ) {
		errMsg = VM_CheckInstructions( inst, vm->instructionCount, vm->jumpTableTargets, vm->numJumpTableTargets, vm->dataLength );
	}
	if ( errMsg ) {
		VM_FreeBuffers();
		Com_Printf( "VM_CompileX86 error: %s\n", errMsg );
		return qfalse;
	}

	VM_FindMOps( inst, vm->instructionCount );

	code = NULL; // we will allocate memory later, after last defined pass
	instructionPointers = NULL;

	memset( funcOffset, 0, sizeof( funcOffset ) );

	instructionCount = header->instructionCount;

__compile:
	pop1 = OP_UNDEF;
	lastConst = 0;

	// translate all instructions
	ip = 0;
	compiledOfs = 0;
	LastCommand = LAST_COMMAND_NONE;

	proc_base = -1;
	proc_len = 0;

#if idx64
	EmitString( "53" );				// push rbx
	EmitString( "56" );				// push rsi
	EmitString( "57" );				// push rdi
	EmitString( "55" );				// push rbp
	EmitString( "41 54" );			// push r12
	EmitString( "41 55" );			// push r13
	EmitString( "41 56" );			// push r14
	EmitString( "41 57" );			// push r15

	EmitRexString( "BB" );			// mov rbx, vm->dataBase
	EmitPtr( vm->dataBase );

	EmitString( "49 B8" );			// mov r8, vm->instructionPointers
	EmitPtr( instructionPointers );

	EmitString( "49 C7 C1" );		// mov r9, vm->dataMask
	Emit4( vm->dataMask );

	EmitString( "49 BC" );			// mov r12, vm->systemCall
	EmitPtr( &vm->systemCall );

	EmitString( "49 C7 C5" );		// mov r13, vm->stackBottom
	Emit4( vm->stackBottom );

	EmitRexString( "B8" );			// mov rax, &vm->programStack
	EmitPtr( &vm->programStack );
	EmitString( "8B 30" );			// mov esi, [rax]

	EmitRexString( "B8" );			// mov rax, &vm->opStack
	EmitPtr( &vm->opStack );
	EmitRexString( "8B 38" );		// mov rdi, [rax]

	EmitRexString( "B8" );			// mov rax, &vm->opStackTop
	EmitPtr( &vm->opStackTop );
	EmitString( "4C 8B 30" );		// mov r14, [rax]

#else
	EmitString( "60" );				// pushad

	EmitRexString( "BB" );			// mov ebx, vm->dataBase
	EmitPtr( vm->dataBase );

	EmitString( "8B 35" );			// mov esi, [vm->programStack]
	EmitPtr( &vm->programStack );

	EmitString( "8B 3D" );			// mov edi, [vm->opStack]
	EmitPtr( &vm->opStack );
#endif

	EmitCallOffset( FUNC_ENTR );

#if idx64

#ifdef DEBUG_VM
	EmitRexString( "B8" );			// mov rax, &vm->programStack
	EmitPtr( &vm->programStack );
	EmitString( "89 30" );			// mov [rax], esi
#endif

	EmitRexString( "B8" );			// mov rax, &vm->opStack
	EmitPtr( &vm->opStack );
	EmitRexString( "89 38" );		// mov [rax], rdi

	EmitString( "41 5F" );			// pop r15
	EmitString( "41 5E" );			// pop r14
	EmitString( "41 5D" );			// pop r13
	EmitString( "41 5C" );			// pop r12
	EmitString( "5D" );				// pop rbp
	EmitString( "5F" );				// pop rdi
	EmitString( "5E" );				// pop rsi
	EmitString( "5B" );				// pop rbx
#else

#ifdef DEBUG_VM
	EmitString( "89 35" );			// [vm->programStack], esi
	EmitPtr( &vm->programStack );
#endif

	EmitString( "89 3D" );			// [vm->opStack], edi
	EmitPtr( &vm->opStack );

	EmitString( "61" );				// popad
#endif

	EmitString( "C3" );				// ret

	EmitAlign( 4 );

	 // main function entry offset
	funcOffset[FUNC_ENTR] = compiledOfs;

	while ( ip < instructionCount )
	{
		instructionOffsets[ ip ] = compiledOfs;

		ci = &inst[ ip ];
		ni = &inst[ ip + 1 ];
		ip++;

		if ( ci->jused ) {
			LastCommand = LAST_COMMAND_NONE;
			pop1 = OP_UNDEF;
		}

		switch ( ci->op ) {

		case OP_UNDEF:
		case OP_IGNORE:
			break;

		case OP_BREAK:
			EmitString( "CC" );		// int 3
			break;

		case OP_ENTER:
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "83 EE" );		// sub	esi, 0x12
				Emit1( v );
			} else {
				EmitString( "81 EE" );		// sub	esi, 0x12345678
				Emit4( v );
			}

			// locate endproc
			for ( n = -1, i = ip + 1; i < instructionCount; i++ ) {
				if ( inst[ i ].op == OP_PUSH && inst[ i + 1 ].op == OP_LEAVE ) {
					n = i;
					break;
				}
			}

			// should never happen because equal check in VM_LoadInstructions() but anyway
			if ( n == -1 ) {
				VM_FreeBuffers();
				Com_Printf( "VM_CompileX86 error: %s\n", "missing proc end" );
				return qfalse;
			}

			proc_base = ip + 1;
			proc_len = n - proc_base + 1 ;

			// programStack overflow check
			if ( vm_rtChecks & 1 ) {
#if idx64
				EmitString( "4C 39 EE" );		// cmp	rsi, r13
#else
				EmitString( "81 FE" );			// cmp	esi, vm->stackBottom
				Emit4( vm->stackBottom );
#endif
				EmitString( "0F 82" );			// jb +funcOffset[FUNC_PSOF]
				n = funcOffset[FUNC_PSOF] - compiledOfs;
				Emit4( n - 6 );
			}

			// opStack overflow check
			if ( vm_rtChecks & 2 ) {
				EmitRexString( "8D 47" );		// lea eax, [edi+0x7F]
				Emit1( ci->opStack );
#if idx64
				EmitString( "4C 39 F0" );		// cmp rax, r14
#else
				EmitString( "3B 05" );			// cmp eax, [&vm->opStackTop]
				EmitPtr( &vm->opStackTop );
#endif
				EmitString( "0F 87" );			// ja +funcOffset[FUNC_OSOF]
				n = funcOffset[FUNC_OSOF] - compiledOfs;
				Emit4( n - 6 );
			}

			EmitRexString( "8D 2C 33" );		// lea ebp, [ebx+esi]
			break;

		case OP_CONST:

			// we can safely perform optimizations only in case if
			// we are 100% sure that next instruction is not a jump label
			if ( !ni->jused && ConstOptimize( vm ) )
				break;

			EmitAddEDI4( vm );					// add edi, 4
			EmitString( "C7 07" );				// mov dword ptr [edi], 0x12345678
			lastConst = ci->value;
			Emit4( lastConst );
			LastCommand = LAST_COMMAND_MOV_EDI_CONST;
			break;

		case OP_LOCAL:
			// optimization: merge OP_LOCAL + OP_LOAD4
			if ( ni->op == OP_LOAD4 ) {
				EmitAddEDI4( vm );				// add edi, 4
				v = ci->value;
				if ( ISS8( v ) ) {
					EmitString( "8B 45" );		// mov eax, dword ptr [ebp + 0x7F]
					Emit1( v );
				} else {
					EmitString( "8B 85" );		// mov eax, dword ptr [ebp + 0x12345678]
					Emit4( v );
				}
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX ); // mov dword ptr [edi], eax
				ip++;
				break;
			}

			// optimization: merge OP_LOCAL + OP_LOAD2
			if ( ni->op == OP_LOAD2 ) {
				EmitAddEDI4( vm );				// add edi, 4
				v = ci->value;
				if ( ISS8( v ) ) {
					EmitString( "0F B7 45" );	// movzx eax, word ptr [ebp + 0x7F]
					Emit1( v );
				} else {
					EmitString( "0F B7 85" );	// movzx eax, word ptr [ebp + 0x12345678]
					Emit4( v );
				}
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX ); // mov dword ptr [edi], eax
				ip++;
				break;
			}

			// optimization: merge OP_LOCAL + OP_LOAD1
			if ( ni->op == OP_LOAD1 ) {
				EmitAddEDI4( vm );				// add edi, 4
				v = ci->value;
				if ( ISS8( v ) ) {
					EmitString( "0F B6 45" );	// movzx eax, byte ptr [ebp + 0x7F]
					Emit1( v );
				} else {
					EmitString( "0F B6 85" );	// movzx eax, byte ptr [ebp + 0x12345678]
					Emit4( v );
				}
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX ); // mov dword ptr [edi], eax
				ip++;
				break;
			}

			EmitAddEDI4( vm );					// add edi, 4
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "8D 46" );			// lea eax, [esi + 0x7F]
				Emit1( v );
			} else {
				EmitString( "8D 86" );			// lea eax, [esi + 0x12345678]
				Emit4( v );
			}
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov dword ptr [edi], eax
			break;

		case OP_ARG:
			EmitMovEAXEDI( vm );					// mov	eax, dword ptr [edi]
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "89 45" );				// mov	dword ptr [ebp + 0x7F], eax
				Emit1( v );
			} else {
				EmitString( "89 85" );				// mov	dword ptr [ebp + 0x12345678], eax
				Emit4( v );
			}
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_CALL:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitCallOffset( FUNC_CALL );			// call +FUNC_CALL
			break;

		case OP_PUSH:
			EmitAddEDI4( vm );						// add edi, 4
			break;

		case OP_POP:
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_LEAVE:
#ifdef DEBUG_VM
			v = ci->value;
			if ( ISS8( v ) ) {
				EmitString( "83 C6" );		// add	esi, 0x12
				Emit1( v );
			} else {
				EmitString( "81 C6" );		// add	esi, 0x12345678
				Emit4( v );
			}
#endif
			EmitString( "C3" );							// ret
			break;

		case OP_LOAD4:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) {
				REWIND( 2 );
				EmitCheckReg( vm, REG_EAX, 4 );			// range check eax
				EmitString( "8B 04 03" );				// mov	eax, dword ptr [ebx + eax]
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov dword ptr [edi], eax
				break;
			}
			EmitMovECXEDI( vm );						// mov ecx, dword ptr [edi]
			EmitCheckReg( vm, REG_ECX, 4 );				// range check ecx
			EmitString( "8B 04 0B" );					// mov	eax, dword ptr [ebx + ecx]
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );	// mov dword ptr [edi], eax
			break;

		case OP_LOAD2:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) {
				REWIND( 2 );
				EmitCheckReg( vm, REG_EAX, 2 );			// range check eax
				EmitString( "0F B7 04 03" );			// movzx eax, word ptr [ebx + eax]
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov dword ptr [edi], eax
				break;
			}
			EmitMovECXEDI( vm );						// mov ecx, dword ptr [edi]
			EmitCheckReg( vm, REG_ECX, 2 );				// range check ecx
			EmitString( "0F B7 04 0B" );				// movzx eax, word ptr [ebx + ecx]
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );	// mov dword ptr [edi], eax
			break;

		case OP_LOAD1:
			if ( LastCommand == LAST_COMMAND_MOV_EDI_EAX ) {
				REWIND( 2 );
				EmitCheckReg( vm, REG_EAX, 1 );			// range check eax
				EmitString( "0F B6 04 03" );			// movzx eax, byte ptr [ebx + eax]
				EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov dword ptr [edi], eax
				break;
			}
			EmitMovECXEDI( vm );						// mov ecx, dword ptr [edi]
			EmitCheckReg( vm, REG_ECX, 1 );				// range check ecx
			EmitString( "0F B6 04 0B" );				// movzx eax, byte ptr [ebx + ecx]
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );	// mov dword ptr [edi], eax
			break;

		case OP_STORE4:
			EmitMovEAXEDI( vm );						// mov eax, dword ptr [edi]
			EmitString( "8B 4F FC" );					// mov ecx, dword ptr [edi-4]
			EmitCheckReg( vm, REG_ECX, 4 );				// range check
			EmitString( "89 04 0B" );					// mov dword ptr [ebx + ecx], eax
			EmitCommand( LAST_COMMAND_SUB_DI_8 );		// sub edi, 8
			break;

		case OP_STORE2:
			EmitMovEAXEDI( vm );						// mov eax, dword ptr [edi]
			EmitString( "8B 4F FC" );					// mov ecx, dword ptr [edi-4]
			EmitCheckReg( vm, REG_ECX, 2 );				// range check
			EmitString( "66 89 04 0B" );				// mov word ptr [ebx + ecx], ax
			EmitCommand( LAST_COMMAND_SUB_DI_8 );		// sub edi, 8
			break;

		case OP_STORE1:
			EmitMovEAXEDI( vm );						// mov eax, dword ptr [edi]
			EmitString( "8B 4F FC" );					// mov ecx, dword ptr [edi-4]
			EmitCheckReg( vm, REG_ECX, 1 );				// range check
			EmitString( "88 04 0B" );					// mov byte ptr [ebx + ecx], eax
			EmitCommand( LAST_COMMAND_SUB_DI_8 );		// sub edi, 8
			break;

		case OP_EQ:
		case OP_NE:
		case OP_LTI:
		case OP_LEI:
		case OP_GTI:
		case OP_GEI:
		case OP_LTU:
		case OP_LEU:
		case OP_GTU:
		case OP_GEU:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitCommand( LAST_COMMAND_SUB_DI_8 );	// sub edi, 8
			EmitString( "39 47 04" );				// cmp dword ptr [edi+4], eax
			EmitJump( vm, ci, ci->op, ci->value );
			break;

		case OP_EQF:
		case OP_NEF:
		case OP_LTF:
		case OP_LEF:
		case OP_GTF:
		case OP_GEF:
#if id386
			if ( HasSSEFP() ) {
#endif
				EmitLoadFloatEDI( vm );
				EmitString( "f3 0f 10 4f fc" );			// movss xmm1, dword ptr [edi-4]
				EmitCommand( LAST_COMMAND_SUB_DI_8 );	// sub edi, 8
				EmitString( "0f 2f c8" );				// comiss xmm1, xmm0
				EmitJump( vm, ci, ci->op, ci->value );
#if id386
			} else {
				EmitLoadFloatEDI( vm );					// fld dword ptr [edi]
				EmitCommand( LAST_COMMAND_SUB_DI_8 );	// sub edi, 8
				EmitString( "D9 47 04" );				// fld dword ptr [edi+4]
				EmitString( "DF E9" );					// fucomip - requires a P6 class CPU (Pentium Pro)
				EmitString( "DD D8" );					// fstp st(0)
				EmitJump( vm, ci, ci->op, ci->value );
			}
#endif
			pop1 = OP_UNDEF;
			break;

		case OP_NEGI:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "F7 D8" );					// neg eax
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov dword ptr [edi], eax
			break;

		case OP_ADD:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "01 47 FC" );				// add dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_SUB:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "29 47 FC" );				// sub dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_DIVI:
			EmitString( "8B 47 FC" );				// mov eax,dword ptr [edi-4]
			EmitString( "99" );						// cdq
			EmitString( "F7 3F" );					// idiv dword ptr [edi]
			EmitString( "89 47 FC" );				// mov dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_DIVU:
			EmitString( "8B 47 FC" );				// mov eax,dword ptr [edi-4]
			EmitString( "33 D2" );					// xor edx, edx
			EmitString( "F7 37" );					// div dword ptr [edi]
			EmitString( "89 47 FC" );				// mov dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_MODI:
			EmitString( "8B 47 FC" );				// mov eax,dword ptr [edi-4]
			EmitString( "99" );						// cdq
			EmitString( "F7 3F" );					// idiv dword ptr [edi]
			EmitString( "89 57 FC" );				// mov dword ptr [edi-4],edx
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_MODU:
			EmitString( "8B 47 FC" );				// mov eax,dword ptr [edi-4]
			EmitString( "33 D2" );					// xor edx, edx
			EmitString( "F7 37" );					// div dword ptr [edi]
			EmitString( "89 57 FC" );				// mov dword ptr [edi-4],edx
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_MULI:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "F7 6F FC" );				// imul eax, dword ptr [edi-4]
			EmitString( "89 47 FC" );				// mov dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_MULU:
			EmitString( "8B 47 FC" );				// mov eax,dword ptr [edi-4]
			EmitString( "F7 27" );					// mul dword ptr [edi]
			EmitString( "89 47 FC" );				// mov dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_BAND:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "21 47 FC" );				// and dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_BOR:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "09 47 FC" );				// or dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_BXOR:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "31 47 FC" );				// xor dword ptr [edi-4],eax
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_BCOM:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitString( "F7 D0" );					// not eax
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov dword ptr [edi], eax
			break;

		case OP_LSH:
			EmitMovECXEDI( vm );					// mov ecx, dword ptr [edi]
			EmitString( "D3 67 FC" );				// shl dword ptr [edi-4], cl
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_RSHI:
			EmitMovECXEDI( vm );					// mov ecx, dword ptr [edi]
			EmitString( "D3 7F FC" );				// sar dword ptr [edi-4], cl
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_RSHU:
			EmitMovECXEDI( vm );					// mov ecx, dword ptr [edi]
			EmitString( "D3 6F FC" );				// shr dword ptr [edi-4], cl
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_NEGF:
			EmitLoadFloatEDI( vm );
#if id386
			if ( HasSSEFP() ) {
#endif
				EmitString( "0f 57 c9" );			// xorps xmm1, xmm1
				EmitString( "0f 5c c8" );			// subps xmm1, xmm0
				EmitString( "0f 28 c1" );			// movaps xmm0, xmm1
#if id386
			} else {
				EmitString( "D9 E0" );				// fchs
			}
#endif
			EmitCommand( LAST_COMMAND_STORE_FLOAT_EDI );
			break;

		case OP_ADDF:
		case OP_SUBF:
		case OP_DIVF:
		case OP_MULF:
#if id386
			if ( HasSSEFP() ) {
#endif
				EmitString( "f3 0f 10 47 fc" );		// movss xmm0, dword ptr [edi-4]
				EmitString( "f3 0f 10 0f" );		// movss xmm1, dword ptr [edi]
				switch( ci->op ) {
					case OP_ADDF: EmitString( "0f 58 c1" ); break;	// addps xmm0, xmm1
					case OP_SUBF: EmitString( "0f 5c c1" ); break;	// subps xmm0, xmm1
					case OP_MULF: EmitString( "0f 59 c1" ); break;	// mulps xmm0, xmm1
					case OP_DIVF: EmitString( "0f 5e c1" ); break;	// divps xmm0, xmm1
				}
				EmitString( "f3 0f 11 47 fc" );		// movss dword ptr [edi-4], xmm0
#if id386
			} else {
				EmitString( "D9 47 FC" );			// fld dword ptr [edi-4]
				EmitFCalcEDI( ci->op );				// fadd|fsub|fmul|fdiv dword ptr [edi]
				EmitString( "D9 5F FC" );			// fstp dword ptr [edi-4]
			}
#endif
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4
			break;

		case OP_CVIF:
#if id386
			if ( HasSSEFP() ) {
#endif
				EmitString( "f3 0f 2a 07" );		// cvtsi2ss xmm0, dword ptr [edi]
#if id386
			} else {
				EmitString( "DB 07" );				// fild dword ptr [edi]
			}
#endif
			EmitCommand( LAST_COMMAND_STORE_FLOAT_EDI );
			break;

		case OP_CVFI:
#if id386
			if ( HasSSEFP() ) {
#endif
				EmitLoadFloatEDI( vm );
				EmitString( "f3 0f 2c c0" );		// cvttss2si eax, xmm0
				EmitString( "89 07" );				// mov dword ptr [edi], eax
#if id386
			} else {
				EmitLoadFloatEDI( vm );				// fld dword ptr [edi]
				// call the library conversion function
				EmitCallOffset( FUNC_FTOL );		// call +FUNC_FTOL
			}
#endif
			break;

		case OP_SEX8:
			EmitString( "0F BE 07" );				// movsx eax, byte ptr [edi]
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov dword ptr [edi], eax
			break;

		case OP_SEX16:
			EmitString( "0F BF 07" );				// movsx eax, word ptr [edi]
			EmitCommand( LAST_COMMAND_MOV_EDI_EAX );// mov dword ptr [edi], eax
			break;

		case OP_BLOCK_COPY:
			EmitString( "B9" );						// mov ecx, 0x12345678
			Emit4( ci->value >> 2 );
			EmitCallOffset( FUNC_BCPY );
			break;

		case OP_JUMP:
			EmitMovEAXEDI( vm );					// mov eax, dword ptr [edi]
			EmitCommand( LAST_COMMAND_SUB_DI_4 );	// sub edi, 4

			// jump target range check
			if ( vm_rtChecks & 4 ) {
				if ( proc_base != -1 ) {
					// allow jump within local function scope only
					EmitString( "89 C2" );			// mov edx, eax
					if ( ISS8( proc_base ) ) {
						EmitString( "83 EA" );		// sub edx, 0x7F
						Emit1( proc_base );
					} else {
						EmitString( "81 EA" );		// sub edx, 0x12345678
						Emit4( proc_base );
					}
					if ( ISS8( proc_len ) ) {
						EmitString( "83 FA" );		// cmp edx, 0x7F
						Emit1( proc_len );
					} else {
						EmitString( "81 FA" );		// cmp edx, 0x12345678
						Emit4( proc_len );
					}
				} else {
					EmitString( "3D" );				// cmp eax, 0x12345678
					Emit4( vm->instructionCount );
				}
				EmitString( "0F 83" );				// jae +funcOffset[FUNC_BADJ]
				n = funcOffset[FUNC_BADJ] - compiledOfs;
				Emit4( n - 6 );
			}
#if idx64
			EmitString( "41 FF 24 C0" );			// jmp dword ptr [r8 + rax*8]
#else
			EmitString( "FF 24 85" );				// jmp dword ptr [instructionPointers + eax * 4]
			EmitPtr( instructionPointers );
#endif
			break;

		case MOP_IGNORE4:
		case MOP_ADD4:
		case MOP_SUB4:
		case MOP_BAND4:
		case MOP_BOR4:

			if ( !EmitMOPs( vm, ci->op ) )
				Com_Error( ERR_FATAL, "VM_CompileX86: bad opcode %02X", ci->op );
			break;

		default:
			Com_Error( ERR_FATAL, "VM_CompileX86: bad opcode %02X", ci->op );
			VM_FreeBuffers();
			return qfalse;
		}

		pop1 = (opcode_t)ci->op;
	} // while( ip < header->instructionCount )

		// ****************
		// system functions
		// ****************
		EmitAlign( 4 );
		funcOffset[FUNC_CALL] = compiledOfs;
		EmitCallFunc( vm );

#if id386
		EmitAlign( 4 );
		funcOffset[FUNC_FTOL] = compiledOfs;
		EmitFTOLFunc( vm );
#endif

		EmitAlign( 4 );
		funcOffset[FUNC_BCPY] = compiledOfs;
		EmitBCPYFunc( vm );

		// ***************
		// error functions
		// ***************

		// bad jump
		EmitAlign( 4 );
		funcOffset[FUNC_BADJ] = compiledOfs;
		EmitBADJFunc( vm );

		// error jump
		EmitAlign( 4 );
		funcOffset[FUNC_ERRJ] = compiledOfs;
		EmitERRJFunc( vm );

		// programStack overflow
		EmitAlign( 4 );
		funcOffset[FUNC_PSOF] = compiledOfs;
		EmitPSOFFunc( vm );

		// opStack overflow
		EmitAlign( 4 );
		funcOffset[FUNC_OSOF] = compiledOfs;
		EmitOSOFFunc( vm );

		// read/write access violation
		EmitAlign( 4 );
		funcOffset[FUNC_DATA] = compiledOfs;
		EmitDATAFunc( vm );

		EmitAlign( sizeof( intptr_t ) ); // for instructionPointers

	n = header->instructionCount * sizeof( intptr_t );

	if ( code == NULL ) {
		code = (byte*)VM_Alloc_Compiled( vm, compiledOfs, n );
		if ( code == NULL ) {
			return qfalse;
		}
		instructionPointers = (intptr_t*)(byte*)(code + compiledOfs);
		goto __compile;
	}

	// offset all the instruction pointers for the new location
	for ( i = 0 ; i < header->instructionCount ; i++ ) {
		if ( !inst[i].jused ) {
			instructionPointers[ i ] = (intptr_t)badJumpPtr;
			continue;
		}
		instructionPointers[ i ] = (intptr_t)vm->codeBase.ptr + instructionOffsets[ i ];
	}

	VM_FreeBuffers();

#ifdef VM_X86_MMAP
	if ( mprotect( vm->codeBase.ptr, compiledOfs + n, PROT_READ|PROT_EXEC ) ) {
		VM_Destroy_Compiled( vm );
		Com_Error( ERR_FATAL, "VM_CompileX86: mprotect failed" );
		return qfalse;
	}
#elif _WIN32
	{
		DWORD oldProtect = 0;

		// remove write permissions.
		if ( !VirtualProtect( vm->codeBase.ptr, compiledOfs + n, PAGE_EXECUTE_READ, &oldProtect ) ) {
			VM_Destroy_Compiled( vm );
			Com_Error( ERR_FATAL, "VM_CompileX86: VirtualProtect failed" );
			return qfalse;
		}
	}
#endif

	vm->destroy = VM_Destroy_Compiled;

	Com_Printf( "VM file %s compiled to %i bytes of code\n", vm->name, compiledOfs );

	return qtrue;
}


/*
=================
VM_Alloc_Compiled
=================
*/
static void *VM_Alloc_Compiled(vm_t *vm, int codeLength, int tableLength)
{
	void	*ptr;
	int		length;

	length = codeLength + tableLength;
#ifdef VM_X86_MMAP
	ptr = mmap( NULL, length, PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );
	if ( ptr == MAP_FAILED ) {
		Com_Error( ERR_FATAL, "VM_CompileX86: mmap failed" );
		return NULL;
	}
#elif _WIN32
	// allocate memory with EXECUTE permissions under windows.
	ptr = VirtualAlloc( NULL, length, MEM_COMMIT, PAGE_EXECUTE_READWRITE );
	if ( !ptr ) {
		Com_Error( ERR_FATAL, "VM_CompileX86: VirtualAlloc failed" );
		return NULL;
	}
#else
	ptr = malloc( length );
	if ( !ptr ) {
		Com_Error( ERR_FATAL, "VM_CompileX86: malloc failed" );
		return NULL;
	}
#endif
	vm->codeBase.ptr = (byte*)ptr;
	vm->codeLength = codeLength;
	return vm->codeBase.ptr;
}


/*
==============
VM_Destroy_Compiled
==============
*/
static void VM_Destroy_Compiled(vm_t* vm)
{
#ifdef VM_X86_MMAP
	munmap( vm->codeBase.ptr, vm->codeLength );
#elif _WIN32
	VirtualFree( vm->codeBase.ptr, 0, MEM_RELEASE );
#else
	free( vm->codeBase.ptr );
#endif
	vm->codeBase.ptr = NULL;
}


/*
==============
VM_CallCompiled

This function is called directly by the generated code
==============
*/
int	VM_CallCompiled( vm_t *vm, int *args )
{
	int		opStack[MAX_OPSTACK_SIZE];
	int		stackOnEntry;
	int		*image;
	vm_t	*oldVM;
	int		*oldOpTop;
	int		i;

	oldVM = currentVM;
	currentVM = vm;

	// we might be called recursively, so this might not be the very top
	stackOnEntry = vm->programStack;
	oldOpTop = vm->opStackTop;
	vm->programStack -= 8 + (VMMAIN_CALL_ARGS*4);

	// set up the stack frame
	image = (int*)( vm->dataBase + vm->programStack );
	for ( i = 0; i < VMMAIN_CALL_ARGS; i++ ) {
		image[ i + 2 ] = args[ i ];
	}
	image[1] =  0;	// return stack
	image[0] = -1;	// will terminate loop on return

	vm->opStack = opStack;
	vm->opStackTop = opStack + ARRAY_LEN( opStack ) - 1;

	vm->codeBase.func(); // go into generated code

	if ( vm->opStack != &opStack[1] ) {
		Com_Error( ERR_DROP, "opStack corrupted in compiled code" );
	}

#ifdef DEBUG_VM
	if ( vm->programStack != stackOnEntry - CALL_PSTACK ) {
		Com_Error( ERR_DROP, "programStack corrupted in compiled code" );
	}
#endif

	vm->programStack = stackOnEntry;
	vm->opStackTop = oldOpTop;

	// in case we were recursively called by another vm
	currentVM = oldVM;

	return vm->opStack[0];
}
