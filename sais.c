/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> 
   Suftest3.c
   Written by G. Manzini using the suftest.c code 
   by N. Jesper Larsson (Copyright N. Jesper Larsson 1999) 
  
   Program to test suffix sorting and LCP building  
   Reads a sequence of bytes from
   a file and calls the qsufsort algorithm described in the paper
   "Faster Suffix Sorting" by N. Jesper Larsson (jesper@cs.lth.se) and Kunihiko
   Sadakane (sada@is.s.u-tokyo.ac.jp). The source code of the qsufsort 
   algorithm is in the file qsufsort.c.

   This software may be used freely for any purpose. However, when distributed,
   the original source must be clearly stated, and, when the source code is
   distributed, the copyright notice must be retained and any alterations in
   the code must be clearly marked. No warranty is given regarding the quality
   of this software.
   >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <sys/times.h>
#include "include/libsais.h"
#include "include/libsais64.h"


typedef uint8_t  uchar_t;

#ifdef USE_INT64
typedef int64_t  idx_t;
#else
typedef int32_t  idx_t;
#endif

#ifdef USE_INT64
int64_t libsais64(const uint8_t * T, int64_t * SA, int64_t n, int64_t fs, int64_t * freq);
int64_t libsais64_plcp(const uint8_t * T, const int64_t * SA, int64_t * PLCP, int64_t n);
int64_t libsais64_lcp(const int64_t * PLCP, const int64_t * SA, int64_t * LCP, int64_t n);
#else
int32_t libsais(const uint8_t * T, int32_t * SA, int32_t n, int32_t fs, int32_t * freq);
int32_t libsais_plcp(const uint8_t * T, const int32_t * SA, int32_t * PLCP, int32_t n);
int32_t libsais_lcp(const int32_t * PLCP, const int32_t * SA, int32_t * LCP, int32_t n);
#endif

#define DBL_CLK_TCK ((double) sysconf(_SC_CLK_TCK)) // clocks x secs 
#define MIN(a, b) ((a)<=(b) ? (a) : (b))
int Verbose=0;

/* **********************************************************
   open filename and write p[0] .. p[n-1] using 32 bit
   ********************************************************** */
void write_sa(char *filename, idx_t *p, idx_t n)
{
  FILE *sa;

  if(Verbose)
    fprintf(stderr,"Writing sa to file %s\n",filename);
  if((sa=fopen(filename,"wb"))==NULL)  perror(filename);
  size_t c = fwrite(p,sizeof(*p),(size_t)n,sa);
  if(c!=(size_t)n) {
    perror("Error writing the sa file");
    exit(1);
  }  fclose(sa);
}


void write_lcp(char *filename, idx_t *p, idx_t n)
{
  FILE *lcp;

  if(Verbose)
    fprintf(stderr,"Writing lcp to file %s\n",filename);
  if((lcp=fopen(filename,"w"))==NULL) perror(filename);
  fwrite(p,sizeof(*p),(size_t)n,lcp);
  if(ferror(lcp)) {
    perror("Error writing the lcp file");
    exit(1);
  }
  fclose(lcp);
}


int main(int argc, char *argv[])
{
   uchar_t *x;
   idx_t  *p, n;
   int c;
   clock_t end_time,start_time;
   struct tms en, st;   
   double tot_time;
   extern char *optarg;
   extern int optind, optopt;
   char *fnam;
   FILE *f;

  /* ------------ set default values ------------- */
  char *sa_filename = NULL;
  char *lcp_filename = NULL;

  /* ------------- read options from command line ----------- */
  while ((c=getopt(argc, argv, "vw:W:")) != -1) {
    switch (c) 
      {
      case 'w':
        sa_filename = optarg; break;
      case 'W':
        lcp_filename = optarg; break;
      // case 't':
      //   NumTreads = atoi(optarg); break;
      case 'v':
        Verbose++; break;
      case '?':
        fprintf(stderr,"Unknown option: %c -main-\n", optopt);
        exit(1);
      }
  }
  // printf("Verbose: %d, Helper threads: %d, Group threshold: %d, MKq threshold: %d\n", Verbose, NumTreads, GrpThresh, MkqThresh);
  if(optind<argc)
    fnam=argv[optind];
  else {
    fprintf(stderr, "Usage:\n\t%s [-w safile][-t helper_threads][-v] ",argv[0]);
    fprintf(stderr, " file\n\n");
    fprintf(stderr,"\t-w safile   write sa to safile\n");    
    fprintf(stderr,"\t-W lcpfile  write lcp to lcpfile\n");
    // fprintf(stderr,"\t-t threads  # helper threads [def. %d]\n",NumTreads);    
    fprintf(stderr,"\t-v          produces a verbose output\n\n");
    return 0;
  }
  if(Verbose) {
    fprintf(stderr,"Command line: ");
    for(c=0;c<argc;c++)
      fprintf(stderr,"%s ",argv[c]);
    fprintf(stderr,"\n");
  }

   if (! (f=fopen(fnam, "rb"))) {
      perror(fnam);
      return 1;
   }
   if (fseeko(f, 0L, SEEK_END)) {
      perror(fnam);
      return 1;
   }
   n=ftello(f);
   if (n==0) {
      fprintf(stderr, "%s: file empty\n", fnam);
      return 0;
   }
   p=malloc((size_t) (n+1)*sizeof *p);
   x=malloc((size_t) (n+1)*sizeof *x);
   if (! p || ! x) {
      fprintf(stderr, "malloc failed\n");
      return 1;
   }
   // read text
   rewind(f);
   if(fread(x, sizeof *x, (size_t)n, f)!=(size_t)n) {
      perror(fnam);
      return 1;
   }

  /* ---------  start measuring time ------------- */
  start_time = times(&st);
  #ifdef USE_INT64
  libsais64(x, p, n, 1, NULL);
  #else
  libsais(x, p, n, 1, NULL);
  #endif
  end_time = times(&en);
  tot_time =  (end_time - start_time)/DBL_CLK_TCK;
  if(Verbose>1) fprintf(stderr,"Elapsed time: %f seconds (SA)\n", tot_time);
  
  // --------------- write sa to a file 
  if(sa_filename!=NULL) 
    write_sa(sa_filename,p,n); // discard first position since it is the suffix starting at the end of the string

  // --------------- compute lcp and write it to a file 
  if(lcp_filename!=NULL) {
    clock_t lcp_time = times(&st); 
    idx_t *plcp=malloc((n+1)*sizeof *plcp);
    if(!plcp) {
      fprintf(stderr, "malloc failed\n");
      return 1;
    }  
    if(Verbose>1) fprintf(stderr,"Computing lcp and writing it to file %s\n", lcp_filename);
    #ifdef USE_INT64
    libsais64_plcp(x, p, plcp, n);
    libsais64_lcp(plcp, p, p, n);
    #else
    libsais_plcp(x, p, plcp, n);
    libsais_lcp(plcp, p, p, n);
    #endif
    end_time = times(&en);
    tot_time =  (end_time - lcp_time)/DBL_CLK_TCK;
    if(Verbose) fprintf(stderr,"Elapsed time: %f seconds (LCP)\n", tot_time);
    write_lcp(lcp_filename, p, n);
    free(plcp); 
  }
  // now fnam is no longer needed
  fclose(f);
  // free memory
  free(x); free(p);
  tot_time =  (end_time - start_time)/DBL_CLK_TCK;
  printf("Elapsed time: %f seconds.\n", tot_time);
  return 0;
}
