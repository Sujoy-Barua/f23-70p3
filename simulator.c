/*
 * EECS 370, University of Michigan, Fall 2023
 * Project 3: LC-2K Pipeline Simulator
 * Instructions are found in the project spec: https://eecs370.github.io/project_3_spec/
 * Make sure NOT to modify printState or any of the associated functions
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Machine Definitions
#define NUMMEMORY 65536 // maximum number of data words in memory
#define NUMREGS 8 // number of machine registers

#define ADD 0
#define NOR 1
#define LW 2
#define SW 3
#define BEQ 4
#define JALR 5 // will not implemented for Project 3
#define HALT 6
#define NOOP 7

const char* opcode_to_str_map[] = {
    "add",
    "nor",
    "lw",
    "sw",
    "beq",
    "jalr",
    "halt",
    "noop"
};

#define NOOPINSTR (NOOP << 22)

typedef enum {
    noHaz,
    EXMEM,
    MEMWB,
    WBEND
} HazType;

typedef struct IFIDStruct {
	int pcPlus1;
	int instr;
} IFIDType;

typedef struct IDEXStruct {
	int pcPlus1;
	int valA;
	int valB;
	int offset;
	int instr;
    HazType valAhazType;
    bool valAHaz;
    bool valBHaz;
    HazType valBhazType;
} IDEXType;

typedef struct EXMEMStruct {
	int branchTarget;
    int eq;
	int aluResult;
	int valB;
	int instr;
    HazType valAhazType;
    bool valAHaz;
    bool valBHaz;
    HazType valBhazType;
} EXMEMType;

typedef struct MEMWBStruct {
	int writeData;
    int instr;
    HazType valAhazType;
    bool valAHaz;
    bool valBHaz;
    HazType valBhazType;
} MEMWBType;

typedef struct WBENDStruct {
	int writeData;
	int instr;
    HazType valAhazType;
    bool valAHaz;
    bool valBHaz;
    HazType valBhazType;
} WBENDType;

typedef struct stateStruct {
	int pc;
	int instrMem[NUMMEMORY];
	int dataMem[NUMMEMORY];
	int reg[NUMREGS];
	unsigned int numMemory;
	IFIDType IFID;
	IDEXType IDEX;
	EXMEMType EXMEM;
	MEMWBType MEMWB;
	WBENDType WBEND;
	unsigned int cycles; // number of cycles run so far
} stateType;

typedef struct {
    int opcode;
    int regA;
    int regB;
    int destReg;
} current5Instr;

static inline int opcode(int instruction) {
    return instruction>>22;
}

static inline int field0(int instruction) {
    return (instruction>>19) & 0x7;
}

static inline int field1(int instruction) {
    return (instruction>>16) & 0x7;
}

static inline int field2(int instruction) {
    return instruction & 0xFFFF;
}

// convert a 16-bit number into a 32-bit Linux integer
static inline int convertNum(int num) {
    return num - ( (num & (1<<15)) ? 1<<16 : 0 );
}

void printState(stateType*);
void printInstruction(int);
void readMachineCode(stateType*, char*);

void current4InstrSetter(current5Instr* current4InstrArray, stateType* state, int stage) {
    if (stage == 0) {
        current4InstrArray[stage].opcode = opcode(state->IFID.instr);
        current4InstrArray[stage].regA = field0(state->IFID.instr);
        current4InstrArray[stage].regB = field1(state->IFID.instr);
        current4InstrArray[stage].destReg = field2(state->IFID.instr);
    } else if (stage == 1) {
        current4InstrArray[stage].opcode = opcode(state->IDEX.instr);
        current4InstrArray[stage].regA = field0(state->IDEX.instr);
        current4InstrArray[stage].regB = field1(state->IDEX.instr);
        current4InstrArray[stage].destReg = field2(state->IDEX.instr);
    } else if (stage == 2) {
        current4InstrArray[stage].opcode = opcode(state->EXMEM.instr);
        current4InstrArray[stage].regA = field0(state->EXMEM.instr);
        current4InstrArray[stage].regB = field1(state->EXMEM.instr);
        current4InstrArray[stage].destReg = field2(state->EXMEM.instr);
    } else if (stage == 3) {
        current4InstrArray[stage].opcode = opcode(state->MEMWB.instr);
        current4InstrArray[stage].regA = field0(state->MEMWB.instr);
        current4InstrArray[stage].regB = field1(state->MEMWB.instr);
        current4InstrArray[stage].destReg = field2(state->MEMWB.instr);
    } else if (stage == -1) {
        current4InstrArray[0].opcode = 7;
        current4InstrArray[0].regA = 0;
        current4InstrArray[0].regB = 0;
        current4InstrArray[0].destReg = 0;
    } else if (stage == -2) {
        current4InstrArray[1].opcode = 7;
        current4InstrArray[1].regA = 0;
        current4InstrArray[1].regB = 0;
        current4InstrArray[1].destReg = 0;
    }
}

void hazardResolver(stateType* state, int* valA, int* valB) {

    if (!state->IDEX.valAHaz) {
        *valA = state->IDEX.valA;
    } else if (state->IDEX.valAHaz && state->IDEX.valAhazType == EXMEM) {
        *valA = state->EXMEM.aluResult;
    } else if (state->IDEX.valAHaz && state->IDEX.valAhazType == MEMWB) {
        *valA = state->MEMWB.writeData;;
    } else if (state->IDEX.valAHaz && state->IDEX.valAhazType == WBEND) {
        *valA = state->WBEND.writeData;
    }

    if (!state->IDEX.valBHaz) {
        *valB = state->IDEX.valB;
    } else if (state->IDEX.valBHaz && state->IDEX.valBhazType == EXMEM) {
        *valB = state->EXMEM.aluResult;
    } else if (state->IDEX.valBHaz && state->IDEX.valBhazType == MEMWB) {
        *valB = state->MEMWB.writeData;
    } else if (state->IDEX.valBHaz && state->IDEX.valBhazType == WBEND) {
        *valB = state->WBEND.writeData;
    }
}


int main(int argc, char *argv[]) {

    /* Declare state and newState.
       Note these have static lifetime so that instrMem and
       dataMem are not allocated on the stack. */

    static stateType state, newState;

    if (argc != 2) {
        printf("error: usage: %s <machine-code file>\n", argv[0]);
        exit(1);
    }

    readMachineCode(&state, argv[1]);
    current5Instr current4InstrArray[4];

    // Initialize state here
    state.pc = 0;
    for (int i = 0; i < 8; i++) {
        state.reg[i] = 0;
    }
    state.IFID.instr = state.IDEX.instr = state.EXMEM.instr = state.MEMWB.instr = state.WBEND.instr = 0x1c00000;
    newState = state;
    bool stall = false;
    //int stallCounter = 0;

    while (opcode(state.MEMWB.instr) != HALT) {
        printState(&state);

        newState.cycles += 1;

        /*if (stall) {
            stallCounter--;
            if (stallCounter == 0) {
                stall = false;
            }
        } */

        /* ---------------------- IF stage --------------------- */
        if (state.EXMEM.eq == 1 && opcode(state.EXMEM.instr) == 4) {
            newState.pc = state.EXMEM.branchTarget;
            newState.IFID.pcPlus1 = state.EXMEM.branchTarget + 1;
            newState.IFID.instr = 7<<22;
        } else {
            newState.pc = state.pc + 1;
            newState.IFID.pcPlus1 = state.pc + 1;
            newState.IFID.instr = state.instrMem[state.pc];
        }

        /* ---------------------- ID stage --------------------- */
        if (!(state.EXMEM.eq == 1 && opcode(state.EXMEM.instr) == 4)) {
            newState.IDEX.pcPlus1 = state.IFID.pcPlus1;
            newState.IDEX.valA = state.reg[field0(state.IFID.instr)];
            newState.IDEX.valB = state.reg[field1(state.IFID.instr)];
            newState.IDEX.offset = convertNum(field2(state.IFID.instr));
            newState.IDEX.instr = state.IFID.instr;

            newState.IDEX.valAHaz = false;
            newState.IDEX.valAhazType = noHaz;
            newState.IDEX.valBHaz = false;
            newState.IDEX.valBhazType = noHaz;

             if ((opcode(state.IFID.instr) == 0) || (opcode(state.IFID.instr) == 1) || (opcode(state.IFID.instr) == 3) || (opcode(state.IFID.instr) == 4)) {   //current instruction is add or nor or sw or beq
                for (int i = 2; i > -1; i--) {
                    if (current4InstrArray[i].opcode == 0 || current4InstrArray[i].opcode == 1) { //if there was a previous intruction add or nor
                        if (field0(state.IFID.instr) == current4InstrArray[i].destReg) {
                            if (i == 0) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = EXMEM;
                            } else if (i == 1) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = MEMWB;
                            } else if (i == 2) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = WBEND;
                            }
                        }
                        if (field1(state.IFID.instr) == current4InstrArray[i].destReg) {
                            if (i == 0) {
                                newState.IDEX.valBHaz = true;
                                newState.IDEX.valBhazType = EXMEM;
                            } else if (i == 1) {
                                newState.IDEX.valBHaz = true;
                                newState.IDEX.valBhazType = MEMWB;
                            } else if (i == 2) {
                                newState.IDEX.valBHaz = true;
                                newState.IDEX.valBhazType = WBEND;
                            }
                        }
                    } else if (current4InstrArray[i].opcode == 2) {  //if there was a prev instr lw
                        if (field0(state.IFID.instr) == current4InstrArray[i].regB) {
                            if (i == 0) {
                                stall = true;
                                //stallCounter = 1;
                                newState.IDEX.instr = 7<<22;
                                newState.pc = state.pc;
                                newState.IFID.pcPlus1 = state.pc;
                                newState.IFID.instr = state.instrMem[state.pc-1];
                                goto stallDetected;
                            }
                            else if (i == 1) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = MEMWB;
                            } else if (i == 2) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = WBEND;
                            }
                        }
                        if (field1(state.IFID.instr) == current4InstrArray[i].regB) {
                            if (i == 0) {
                                stall = true;
                                //stallCounter = 1;
                                newState.IDEX.instr = 7<<22;
                                newState.pc = state.pc;
                                newState.IFID.pcPlus1 = state.pc;
                                newState.IFID.instr = state.instrMem[state.pc-1];
                                goto stallDetected;
                            } else if (i == 1) {
                                newState.IDEX.valBHaz = true;
                                newState.IDEX.valBhazType = MEMWB;
                            } else if (i == 2) {
                                newState.IDEX.valBHaz = true;
                                newState.IDEX.valBhazType = WBEND;
                            }
                        }
                    }
                }
            } else if (opcode(state.IFID.instr) == 2) {    //current instruction is lw
                for (int i = 2; i > -1; i--) {
                    if (current4InstrArray[i].opcode == 0 || current4InstrArray[i].opcode == 1) { //if there was a previous instruction add or nor
                        if (field0(state.IFID.instr) == current4InstrArray[i].destReg) {
                            if (i == 0) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = EXMEM;
                            } else if (i == 1) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = MEMWB;
                            } else if (i == 1) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = WBEND;
                            }
                        }
                    } else if (current4InstrArray[i].opcode == 2) {  //if there was a previous instruction lw
                        if (field0(state.IFID.instr) == current4InstrArray[i].regB) {
                            if (i == 0) {
                                stall = true;
                                //stallCounter = 1;
                                newState.IDEX.instr = 7<<22;
                                newState.pc = state.pc;
                                newState.IFID.pcPlus1 = state.pc;
                                newState.IFID.instr = state.instrMem[state.pc-1];
                                goto stallDetected;
                            } else if (i == 1) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = MEMWB;
                            } else if (i == 2) {
                                newState.IDEX.valAHaz = true;
                                newState.IDEX.valAhazType = WBEND;
                            }
                        }
                    }
                }
            }
stallDetected:
            if (!stall) {
                current4InstrSetter(current4InstrArray, &state, 0);
            } else {
                current4InstrSetter(current4InstrArray, &state, -1); // set 0th instr = noop
                stall = false;
            }
        } else {
            newState.IDEX.instr = 7<<22;
            current4InstrSetter(current4InstrArray, &state, -1);
        }
        

        /* ---------------------- EX stage --------------------- */
        if (!(state.EXMEM.eq == 1 && opcode(state.EXMEM.instr) == 4)) {
            const char* opcode_str = opcode_to_str_map[opcode(state.IDEX.instr)];
            newState.EXMEM.branchTarget = state.IDEX.pcPlus1 + state.IDEX.offset;
            int valA = 0;
            int valB = 0;
            hazardResolver(&state, &valA, &valB);
            if (!strcmp(opcode_str, "add")) {
                newState.EXMEM.aluResult = valA + valB;
            }else if (!strcmp(opcode_str, "nor")) {
                newState.EXMEM.aluResult = ~(valA | valB);
            } else if (!strcmp(opcode_str, "lw") || !strcmp(opcode_str, "sw")) {
                newState.EXMEM.aluResult = valA + state.IDEX.offset;
            } else if (!strcmp(opcode_str, "beq")) {
                if (valA == valB) {
                    newState.EXMEM.eq = 1;
                } else {
                    newState.EXMEM.eq = 0;
                }
            }
            newState.EXMEM.valB = valB;
            newState.EXMEM.instr = state.IDEX.instr;

            current4InstrSetter(current4InstrArray, &state, 1);
        } else {
            newState.EXMEM.instr = 7<<22;
            current4InstrSetter(current4InstrArray, &state, -2); // set 1st instr = noop
        }

        /* --------------------- MEM stage --------------------- */
        const char* opcode_str = opcode_to_str_map[opcode(state.EXMEM.instr)];
        if (!strcmp(opcode_str, "add") || !strcmp(opcode_str, "nor")) {
            newState.MEMWB.writeData = state.EXMEM.aluResult;
        } else if (!strcmp(opcode_str, "lw")) {
            newState.MEMWB.writeData = state.dataMem[state.EXMEM.aluResult];
        } else if (!strcmp(opcode_str, "sw")) {
            newState.dataMem[state.EXMEM.aluResult] = state.EXMEM.valB;
        }
        newState.MEMWB.instr = state.EXMEM.instr;

        current4InstrSetter(current4InstrArray, &state, 2);

        /* ---------------------- WB stage --------------------- */
        opcode_str = opcode_to_str_map[opcode(state.MEMWB.instr)];
        newState.WBEND.writeData = state.MEMWB.writeData;

        if (!strcmp(opcode_str, "add") || !strcmp(opcode_str, "nor")) {
            newState.reg[field2(state.MEMWB.instr)] = state.MEMWB.writeData;
        } else if (!strcmp(opcode_str, "lw")) {
            newState.reg[field1(state.MEMWB.instr)] = state.MEMWB.writeData;
        }

        newState.WBEND.instr = state.MEMWB.instr;

        current4InstrSetter(current4InstrArray, &state, 3);

        /* ------------------------ END ------------------------ */
        state = newState; /* this is the last statement before end of the loop. It marks the end
        of the cycle and updates the current state with the values calculated in this cycle */
    }
    printf("Machine halted\n");
    printf("Total of %d cycles executed\n", state.cycles);
    printf("Final state of machine:\n");
    printState(&state);
}

/*
* DO NOT MODIFY ANY OF THE CODE BELOW.
*/

void printInstruction(int instr) {
    const char* instr_opcode_str;
    int instr_opcode = opcode(instr);
    if(ADD <= instr_opcode && instr_opcode <= NOOP) {
        instr_opcode_str = opcode_to_str_map[instr_opcode];
    }

    switch (instr_opcode) {
        case ADD:
        case NOR:
        case LW:
        case SW:
        case BEQ:
            printf("%s %d %d %d", instr_opcode_str, field0(instr), field1(instr), convertNum(field2(instr)));
            break;
        case JALR:
            printf("%s %d %d", instr_opcode_str, field0(instr), field1(instr));
            break;
        case HALT:
        case NOOP:
            printf("%s", instr_opcode_str);
            break;
        default:
            printf(".fill %d", instr);
            return;
    }
}

void printState(stateType *statePtr) {
    printf("\n@@@\n");
    printf("state before cycle %d starts:\n", statePtr->cycles);
    printf("\tpc = %d\n", statePtr->pc);

    printf("\tdata memory:\n");
    for (int i=0; i<statePtr->numMemory; ++i) {
        printf("\t\tdataMem[ %d ] = %d\n", i, statePtr->dataMem[i]);
    }
    printf("\tregisters:\n");
    for (int i=0; i<NUMREGS; ++i) {
        printf("\t\treg[ %d ] = %d\n", i, statePtr->reg[i]);
    }

    // IF/ID
    printf("\tIF/ID pipeline register:\n");
    printf("\t\tinstruction = %d ( ", statePtr->IFID.instr);
    printInstruction(statePtr->IFID.instr);
    printf(" )\n");
    printf("\t\tpcPlus1 = %d", statePtr->IFID.pcPlus1);
    if(opcode(statePtr->IFID.instr) == NOOP){
        printf(" (Don't Care)");
    }
    printf("\n");

    // ID/EX
    int idexOp = opcode(statePtr->IDEX.instr);
    printf("\tID/EX pipeline register:\n");
    printf("\t\tinstruction = %d ( ", statePtr->IDEX.instr);
    printInstruction(statePtr->IDEX.instr);
    printf(" )\n");
    printf("\t\tpcPlus1 = %d", statePtr->IDEX.pcPlus1);
    if(idexOp == NOOP){
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\treadRegA = %d", statePtr->IDEX.valA);
    if (idexOp >= HALT || idexOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\treadRegB = %d", statePtr->IDEX.valB);
    if(idexOp == LW || idexOp > BEQ || idexOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\toffset = %d", statePtr->IDEX.offset);
    if (idexOp != LW && idexOp != SW && idexOp != BEQ) {
        printf(" (Don't Care)");
    }
    printf("\n");

    // EX/MEM
    int exmemOp = opcode(statePtr->EXMEM.instr);
    printf("\tEX/MEM pipeline register:\n");
    printf("\t\tinstruction = %d ( ", statePtr->EXMEM.instr);
    printInstruction(statePtr->EXMEM.instr);
    printf(" )\n");
    printf("\t\tbranchTarget %d", statePtr->EXMEM.branchTarget);
    if (exmemOp != BEQ) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\teq ? %s", (statePtr->EXMEM.eq ? "True" : "False"));
    if (exmemOp != BEQ) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\taluResult = %d", statePtr->EXMEM.aluResult);
    if (exmemOp > SW || exmemOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");
    printf("\t\treadRegB = %d", statePtr->EXMEM.valB);
    if (exmemOp != SW) {
        printf(" (Don't Care)");
    }
    printf("\n");

    // MEM/WB
	int memwbOp = opcode(statePtr->MEMWB.instr);
    printf("\tMEM/WB pipeline register:\n");
    printf("\t\tinstruction = %d ( ", statePtr->MEMWB.instr);
    printInstruction(statePtr->MEMWB.instr);
    printf(" )\n");
    printf("\t\twriteData = %d", statePtr->MEMWB.writeData);
    if (memwbOp >= SW || memwbOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");

    // WB/END
	int wbendOp = opcode(statePtr->WBEND.instr);
    printf("\tWB/END pipeline register:\n");
    printf("\t\tinstruction = %d ( ", statePtr->WBEND.instr);
    printInstruction(statePtr->WBEND.instr);
    printf(" )\n");
    printf("\t\twriteData = %d", statePtr->WBEND.writeData);
    if (wbendOp >= SW || wbendOp < 0) {
        printf(" (Don't Care)");
    }
    printf("\n");

    printf("end state\n");
    fflush(stdout);
}

// File
#define MAXLINELENGTH 1000 // MAXLINELENGTH is the max number of characters we read

void readMachineCode(stateType *state, char* filename) {
    char line[MAXLINELENGTH];
    FILE *filePtr = fopen(filename, "r");
    if (filePtr == NULL) {
        printf("error: can't open file %s", filename);
        exit(1);
    }

    printf("instruction memory:\n");
    for (state->numMemory = 0; fgets(line, MAXLINELENGTH, filePtr) != NULL; ++state->numMemory) {
        if (sscanf(line, "%d", state->instrMem+state->numMemory) != 1) {
            printf("error in reading address %d\n", state->numMemory);
            exit(1);
        }
        printf("\tinstrMem[ %d ]\t= 0x%08x\t= %d\t= ", state->numMemory, 
            state->instrMem[state->numMemory], state->instrMem[state->numMemory]);
        printInstruction(state->dataMem[state->numMemory] = state->instrMem[state->numMemory]);
        printf("\n");
    }
}
