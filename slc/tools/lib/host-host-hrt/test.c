#include <stdlib.h>
#include <stdio.h>

int main(int argc, char** argv) {
  printf("PROGRAM: TESTTESTTESTTEST\n");
  FILE* file = fopen("output.txt", "wt");
  fprintf(file, "blah\n");
  fclose(file);
  //FILE* pipe_out = fopen("/tmp/fifo-to-daemon", "w"); 
  FILE* pipe_out = fopen(argv[2], "w"); 
  printf("PROGRAM: writing to pipe: %s\n", argv[2]);
  fprintf(pipe_out, "kewl");
  fprintf(pipe_out, "\n");
  fclose(pipe_out);
  sleep(2);
  //printf("PROGRAM: exiting\n");
  
  /*
  printf("Program loaded... announcing on pipe %s\n", argv[1]);
  FILE* pipe_out = fopen(argv[1], "r"); 
  fprintf(pipe_out, "kewl\n");
  fclose(pipe_out);
  */
  return 0;
}
