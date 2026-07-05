#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/times.h>
#include "sacak-lcp.h"


typedef unsigned char uchar_t;

#define DBL_CLK_TCK ((double) sysconf(_SC_CLK_TCK)) // clocks x secs 
int Verbose=0;


/* **********************************************************
   open filename and write p[0] .. p[n-1] using 32 bit
   ********************************************************** */
void write_sa(char *filename, uint_t *p, uint_t n)
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


void write_lcp(char *filename, int_t *p, uint_t n)
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
   uint_t  *p, n;
   int_t *lcp= NULL;
   int c, e;
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
  int compute_avg_lcp = 0;

  /* ------------- read options from command line ----------- */
  while ((c=getopt(argc, argv, "vw:W:a")) != -1) {
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
      case 'a':
        compute_avg_lcp = 1; break;
      case '?':
        fprintf(stderr,"Unknown option: %c -main-\n", optopt);
        exit(1);
      }
  }
  if(optind<argc)
    fnam=argv[optind];
  else {
    fprintf(stderr, "Usage:\n\t%s [-w safile][-v][-a] ",argv[0]);
    fprintf(stderr, " file\n\n");
    fprintf(stderr,"\t-w safile   write sa to safile\n");    
    fprintf(stderr,"\t-W lcpfile  write lcp to lcpfile\n");
    fprintf(stderr,"\t-a          compute and print average LCP value\n"); 
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
	 fclose(f);
	 x[n]=0; // null terminate the string
	 uchar_t maxchar = 0;
	 for(uint_t i=0; i<n; i++){
		  if(x[i]==0) {
				fprintf(stderr,"Error: input string contains a zero byte at position %lld (not allowed in sacak)\n", (long long)i);
				return 1;
		  }
		 if(x[i]>maxchar) maxchar = x[i];
	 }

	 
  /* ---------  start measuring time ------------- */
  start_time = times(&st);
  if(lcp_filename!=NULL || compute_avg_lcp) {
    lcp=malloc((size_t) (n+1)*sizeof *lcp);
    if(!lcp) {
      fprintf(stderr, "malloc failed\n");
      return 1;
    }  
    e = sacak_lcp(x, p, lcp, n+1);
  }
  else {
    e = sacak(x, p, n+1);
  }
	if(e<0) {
		fprintf(stderr,"Error: sacak returned %d\n", e);
		return 1;
	}
  end_time = times(&en);
  tot_time =  (end_time - start_time)/DBL_CLK_TCK;
  if(Verbose>1) 
    fprintf(stderr,"Elapsed time: %f seconds %s \n", tot_time, lcp==NULL ? " (SA)" : " (SA+LCP)");
  
  // --------------- write sa to a file 
  if(sa_filename!=NULL) 
    write_sa(sa_filename,p+1,n); // discard first position since it is the suffix starting at the end of the string

  // --------------- write lcp to a file 
  if(lcp!=NULL) {
    if(lcp_filename!=NULL)
      write_lcp(lcp_filename, lcp+1, n);
    
    if(compute_avg_lcp) {
      double sum = 0.0;
      for(uint_t i = 1; i <= n; i++) {
        sum += lcp[i];
      }
      double avg = (n > 0) ? sum / n : 0.0;
      printf("Average LCP: %f\n", avg);
    }
    free(lcp); 
  }
  // free memory
  free(x); free(p);
  tot_time =  (end_time - start_time)/DBL_CLK_TCK;
  printf("Elapsed time: %f seconds.\n", tot_time);
  return 0;
}
