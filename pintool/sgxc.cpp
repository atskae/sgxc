/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2017 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/*
 *  This file contains an ISA-portable PIN tool for tracing memory accesses.
 */

#include <stdio.h>
#include <stdint.h>
#include "pin.H"

// defined in sgxc
#define LOAD 0
#define STORE 1
#define INSN 2

#define NON_ENCLAVE 0
#define ENCLAVE 1

#define ENCLAVE_MODE NON_ENCLAVE

#define ONE_BILLION 1000000000
#define FIVE_MIL 500000000

#define MAX_TRACES FIVE_MIL 
#define START_AT ONE_BILLION

string outfile;
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "sgxc.out", "Output file of memory references");

static uint64_t trace_number = 0; // when trace_number reaches START_AT, start collecting traces and increment numTraces
static uint64_t numTraces = 0;

/*
	Trace format: interval, enclave mode, mem_addr, operation
*/

FILE * trace;
clock_t previous_timestamp;
double avr_interval = 0.000832;

VOID stopPin() {
    numTraces = (numTraces < MAX_TRACES) ? trace_number : numTraces;
    
    fprintf(trace, "%lu#eof\n", numTraces);
    fclose(trace);
	printf("sgxc: collected %lu traces.\n", numTraces);
	exit(0);
}

VOID RecordInsnTrace(VOID* ip) {
	trace_number++;
    if(trace_number < START_AT) return;

    numTraces++;
    fprintf(trace, "%f %i %p %i\n", avr_interval, ENCLAVE_MODE, ip, INSN);
	if(numTraces >= MAX_TRACES) stopPin();
}

VOID RecordMemRead(VOID * ip, VOID * addr) {
	trace_number++;
    if(trace_number < START_AT) return;

    numTraces++;
    fprintf(trace, "%f %i %p %i\n", avr_interval, ENCLAVE_MODE, addr, LOAD);
	if(numTraces >= MAX_TRACES) stopPin();
}

VOID RecordMemWrite(VOID * ip, VOID * addr) {
    trace_number++;
    if(trace_number < START_AT) return;

    numTraces++;
    fprintf(trace, "%f %i %p %i\n", avr_interval, ENCLAVE_MODE, addr, STORE);
	if(numTraces >= MAX_TRACES) stopPin();
}

// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID *v)
{
    // Instruments memory accesses using a predicated call, i.e.
    // the instrumentation is called iff the instruction will actually be executed.
    //
    // On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
    // prefixed instructions appear as predicated instructions in Pin.
    
	INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordInsnTrace,
                IARG_INST_PTR, 
                IARG_END);

	UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
        // Note that in some architectures a single memory operand can be 
        // both read and written (for instance incl (%eax) on IA-32)
        // In that case we instrument it once for read and once for write.
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }
}

VOID Fini(INT32 code, VOID *v)
{
    stopPin();
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
   
INT32 Usage()
{
    PIN_ERROR( "This Pintool prints a trace of memory addresses\n" 
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return Usage();

    outfile = KnobOutputFile.Value();
    trace = fopen(outfile.c_str(), "w");

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
	previous_timestamp = clock();
    PIN_StartProgram();
    
    return 0;
}
