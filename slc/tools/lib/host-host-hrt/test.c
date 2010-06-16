#include <stdlib.h>
#include <stdio.h>

int main(int argc, char** argv) {
  printf("PROGRAM: TESTTESTTESTTEST\n");
  FILE* file = fopen("output.txt", "wt");
  fprintf(file, "blah\n");
  fclose(file);
//  return 0;
//  *((char*)0x00) = 'a';
//  printf("TESTTESTTESTTEST\n");
  FILE* pipe_out = fopen("/tmp/fifo-to-daemon", "w"); 
  printf("PROGRAM: writing to pipe: %s\n", argv[2]);
  //fprintf(pipe_out, "muie\n");
  fprintf(pipe_out, argv[2]);
  fprintf(pipe_out, "\n");
  fclose(pipe_out);
  sleep(2);
  printf("PROGRAM: exiting\n");
  
  /*
  printf("Program loaded... announcing on pipe %s\n", argv[1]);
  FILE* pipe_out = fopen(argv[1], "r"); 
  fprintf(pipe_out, "kewl\n");
  fclose(pipe_out);
  */
  return 0;
}
