#define __STDC_FORMAT_MACROS
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#define N_ELEMS(N) ((int)pow(2, N)) // Total elements in counter table
#define TBL_LEN(N) ((int)(ceil(N_ELEMS(N) / 4.0))) /* Total slots in char array
                                                      for counter table */
#define STRONGLY_NOT 0
#define WEAKLY_NOT 1
#define WEAKLY_DO 2
#define STRONGLY_DO 3

#define STRONGLY_BIMOD 0
#define WEAKLY_BIMOD 1
#define WEAKLY_GSHARE 2
#define STRONGLY_GSHARE 3

#define MASK 0x3
#define CLEAR0 ~0x3
#define CLEAR1 ~0xC
#define CLEAR2 ~0x30
#define CLEAR3 ~0xC0

#define TRUE 1
#define FALSE 0

#define debug 0

//------------------------------------------------------------------
// Global Data
// -----------------------------------------------------------------

/* Files */
FILE *inputFile;
FILE *outputFile;

/* Counter tables for branch prediction */
char * bimod_table, 
     * gshare_table, 
     * choose_table;

//------------------------------------------------------------------
// Helper Functions
// -----------------------------------------------------------------

void init_brnpred_tables(int bimod_n, int gshare_n, int choose_n){
  fprintf(outputFile, "Initializing tables...\n");

  bimod_table  = calloc(TBL_LEN(bimod_n),  sizeof(char));
  gshare_table = calloc(TBL_LEN(gshare_n), sizeof(char));
  choose_table = calloc(TBL_LEN(choose_n), sizeof(char));
}

int is_mispredicted(char TNnotBranch, char * table, char val){ 
  int8_t ret = FALSE;

  switch (val) {
    case STRONGLY_NOT:
    case WEAKLY_NOT:
      if (TNnotBranch == 'T') {ret = TRUE;}
      break;
    case WEAKLY_DO:
    case STRONGLY_DO:
      if (TNnotBranch == 'N') {ret = TRUE;}
      break;
  }

  return ret;
}

/* Return two bit value out of char array table, given index */
inline char get_counter(char * arr, size_t index){
  return (arr[index/4] >> (2 * (index %4))) & MASK;
}

/* Increment two bit value in counter table */
void inc_counter(char * arr, size_t index){
  char original = arr[index/4];
  char to_inc = (original >> (2 * (index % 4))) & MASK;

  if (to_inc != 3) to_inc++;
  to_inc <<= (2 * (index % 4));

  switch(index % 4){
    case 0 :
      to_inc = (original & CLEAR0) | to_inc;
      break;
    case 1:
      to_inc = (original & CLEAR1) | to_inc;
      break;
    case 2:
      to_inc = (original & CLEAR2) | to_inc;
      break;
    case 3:
      to_inc = (original & CLEAR3) | to_inc;
      break;
  }

  arr[index/4] = to_inc;
}

/* Decrement two bit value in counter table */
void dec_counter(char * arr, size_t index){
  char original = arr[index/4];
  char to_inc = (original >> (2 * (index % 4))) & MASK;

  if (to_inc != 0) to_inc--;
  to_inc <<= (2 * (index % 4));

  switch(index % 4){
    case 0 :
      to_inc = (original & CLEAR0) | to_inc;
      break;
    case 1:
      to_inc = (original & CLEAR1) | to_inc;
      break;
    case 2:
      to_inc = (original & CLEAR2) | to_inc;
      break;
    case 3:
      to_inc = (original & CLEAR3) | to_inc;
      break;
  }

  arr[index/4] = to_inc;
}

//------------------------------------------------------------------
// Pretty Printing
// -----------------------------------------------------------------

/* For a 2bit entry counter table, given length*/
void print_counter(char * arr, int n){
  int  i = 0;
  char val;

  for(i = 0; i < N_ELEMS(n); ++i){
    val = get_counter(arr, i);
    switch (val){
      case STRONGLY_NOT:
        printf("N");
        break;
      case WEAKLY_NOT:
        printf("n");
        break;
      case WEAKLY_DO:
        printf("t");
        break;
      case STRONGLY_DO:
        printf("T");
        break;
    }
  }
}

/* For a 2bit entry choose table, given length*/
void print_choose(char * arr, int n){
  int  i = 0;
  char val;

  for(i = 0; i < N_ELEMS(n); ++i){
    val = get_counter(arr, i);
    switch (val){
      case STRONGLY_BIMOD:
        printf("B");
        break;
      case WEAKLY_BIMOD:
        printf("b");
        break;
      case WEAKLY_GSHARE:
        printf("g");
        break;
      case STRONGLY_GSHARE:
        printf("G");
        break;
    }
  }
}

/* For gshare history value */
void print_history(int64_t history, int n2){
  int i = 0;
  int val;

  for(i = 0; i < n2; i++){
    val = (history >> (n2 - i - 1)) & 1;
    if (val == 1) {printf("T");}
    else {printf("N");}
  }
}

void print_predictions(char TNnotBranch, int8_t result, int64_t instructionAddress,
  int64_t mispredicted, int64_t gshare_history, char * table, int history_n, 
  int table_n){
  
  if (!debug) return;

  printf("--------------------------------------------------------\n");

  print_counter(table, table_n);
  if (gshare_history) {
    printf(" ");
    print_history(gshare_history, history_n);
  }

  if (result) {
    if (TNnotBranch == 'T') {
      printf(" | %x T | N incorrect\t%d\n", instructionAddress, mispredicted);
    } else {
      printf(" | %x N | T incorrect\t%d\n", instructionAddress, mispredicted);
    }
  } else {
    if (TNnotBranch == 'T') {
      printf(" | %x T | T correct\t%d\n", instructionAddress, mispredicted);
    } else {
      printf(" | %x N | N correct\t%d\n", instructionAddress, mispredicted);
    }
  }
}


//------------------------------------------------------------------
// Main Loop
// -----------------------------------------------------------------

void simulate(int bimod_n, int gshare_n, int history_n, int choose_n)
{
  // See the documentation to understand what these variables mean.
  int32_t microOpCount;
  uint64_t instructionAddress;
  int32_t sourceRegister1;
  int32_t sourceRegister2;
  int32_t destinationRegister;
  char conditionRegister;
  char TNnotBranch;
  char loadStore;
  int64_t immediate;
  uint64_t addressForMemoryOp;
  uint64_t fallthroughPC;
  uint64_t targetAddressTakenBranch;
  char macroOperation[12];
  char microOperation[23];

  int64_t totalMicroops = 0;
  int64_t totalMacroops = 0;

  // Added variable bindings
  int8_t  bimod_result,
          gshare_result;
  int64_t mispredicted_static = 0,
          mispredicted_bimod = 0,
          mispredicted_gshare = 0,
          mispredicted_tourney = 0,
          total_branches = 0;
  char    val_bimod, val_gshare, val_choose;
  int64_t gshare_history = 0; 

  /* Initialize branch prediction tables */
  init_brnpred_tables(bimod_n, gshare_n, choose_n);
  
  fprintf(outputFile, "Processing trace...\n");
  while (true) {
    int result = fscanf(inputFile, 
                        "%" SCNi32
                        "%" SCNx64 
                        "%" SCNi32
                        "%" SCNi32
                        "%" SCNi32
                        " %c"
                        " %c"
                        " %c"
                        "%" SCNi64
                        "%" SCNx64
                        "%" SCNx64
                        "%" SCNx64
                        "%11s"
                        "%22s",
                        &microOpCount,
                        &instructionAddress,
                        &sourceRegister1,
                        &sourceRegister2,
                        &destinationRegister,
                        &conditionRegister,
                        &TNnotBranch,
                        &loadStore,
                        &immediate,
                        &addressForMemoryOp,
                        &fallthroughPC,
                        &targetAddressTakenBranch,
                        macroOperation,
                        microOperation);
                        
    if (result == EOF) {
      break;
    }

    if (result != 14) {
      fprintf(stderr, "Error parsing trace at line %" PRIi64 "\n", totalMicroops);
      abort();
    }

    // Count micro and macro ops
    totalMicroops++;
    if (microOpCount == 1) {
      totalMacroops++;
    }

    // Branch prediction statistics
    if (conditionRegister == 'R' && TNnotBranch != '-'){
      total_branches++;

      // Static prediction
      if (TNnotBranch == 'N') mispredicted_static++;

      // Bimodal prediction
      val_bimod   = get_counter(bimod_table, instructionAddress % N_ELEMS(bimod_n)); 
      bimod_result = is_mispredicted(TNnotBranch, bimod_table, val_bimod);
      
      if (bimod_result) {mispredicted_bimod++;}
      print_predictions(
          TNnotBranch, 
          bimod_result, 
          instructionAddress, 
          mispredicted_bimod, 
          0,
          bimod_table,
          history_n,
          bimod_n
      );

      if (TNnotBranch == 'T'){
        inc_counter(bimod_table, instructionAddress % N_ELEMS(bimod_n));
      } else {
        dec_counter(bimod_table, instructionAddress % N_ELEMS(bimod_n));
      }

      // Gshare prediction
      val_gshare  = get_counter(gshare_table, (instructionAddress ^ gshare_history) % N_ELEMS(gshare_n)); 
      gshare_result = is_mispredicted(TNnotBranch, gshare_table, val_gshare);

      if (gshare_result) {mispredicted_gshare++;}
      print_predictions(
          TNnotBranch, 
          gshare_result, 
          instructionAddress, 
          mispredicted_gshare, 
          gshare_history,
          gshare_table,
          history_n,
          gshare_n
      );

      if (TNnotBranch == 'T'){
        inc_counter(gshare_table, (instructionAddress ^ gshare_history) % N_ELEMS(gshare_n));
        gshare_history = ((gshare_history << 1) % N_ELEMS(history_n)) | 0x1; 
      } else {
        dec_counter(gshare_table, (instructionAddress ^ gshare_history) % N_ELEMS(gshare_n));
        gshare_history = (gshare_history << 1) % N_ELEMS(history_n); 
      }

      // Tournament prediction
     /* 
      print_choose(choose_table, choose_n);
      printf(" ");
      print_counter(bimod_table, bimod_n);
      printf(" ");*/
      val_choose = get_counter(choose_table, instructionAddress % N_ELEMS(choose_n));
      switch (val_choose) {
        case STRONGLY_BIMOD:
        case WEAKLY_BIMOD:
          if (bimod_result) {mispredicted_tourney++;}
          print_predictions(
              TNnotBranch,
              bimod_result,
              instructionAddress,
              mispredicted_tourney,
              gshare_history,
              gshare_table,
              history_n,
              gshare_n
          );
          break;
        case WEAKLY_GSHARE:
        case STRONGLY_GSHARE:
          if (gshare_result) {mispredicted_tourney++;}
          print_predictions(
              TNnotBranch,
              gshare_result,
              instructionAddress,
              mispredicted_tourney,
              gshare_history,
              gshare_table,
              history_n,
              gshare_n
          );
          break;
      }

      if (bimod_result != gshare_result) {
        if (!bimod_result) {
          dec_counter(choose_table, instructionAddress % N_ELEMS(choose_n));
        } else {
          inc_counter(choose_table, instructionAddress % N_ELEMS(choose_n));
        }
      }
    }
  }
  
  fprintf(outputFile, "Processed %" PRIi64 " trace records.\n", totalMicroops);

  fprintf(outputFile, "Micro-ops: %" PRIi64 "\n", totalMicroops);
  fprintf(outputFile, "Macro-ops: %" PRIi64 "\n", totalMacroops);

  fprintf(outputFile, "---HW2----------------------------------------------\n");
  fprintf(outputFile, "Static misprediction rate: %f\n", (mispredicted_static + 0.0) / total_branches); 
  fprintf(outputFile, "Bimodal misprediction rate: %f\n", (mispredicted_bimod + 0.0) / total_branches); 
  fprintf(outputFile, "Gshare misprediction rate: %f\n", (mispredicted_gshare + 0.0) / total_branches); 
  fprintf(outputFile, "Tournament misprediction rate: %f\n", (mispredicted_tourney + 0.0) / total_branches); 
  fprintf(outputFile, "Tournament misprediction num: %d\n", (mispredicted_tourney)); 
}

int main(int argc, char *argv[]) 
{
  int bimod_n = 2, gshare_n = 2, history_n = 2, choose_n = 2;

  inputFile  = stdin;
  outputFile = stdout;

  if (argc >=2) bimod_n   = atoi(argv[1]);
  if (argc >=3) gshare_n  = atoi(argv[2]);
  if (argc >=4) history_n = atoi(argv[3]);
  if (argc >=5) choose_n  = atoi(argv[4]);

  printf("%d, %d, %d, %d\n", bimod_n, gshare_n, history_n, choose_n);
  
  simulate(bimod_n, gshare_n, history_n, choose_n);
  return 0;
}
