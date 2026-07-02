#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sacak-lcp.h"
#include "experiments/external/malloc_count.h" //memory counter

int main(int argc, char *argv[]){

	printf("sizeof(int_t) = %zu bytes\n", sizeof(int_t));
	unsigned char *Text;

	// intput data
	if(argc==2){
		Text = malloc((strlen(argv[1])+1)*sizeof(unsigned char));
		sscanf(argv[1], "%s", Text);	
	}
	else{
		Text = "banaananaanana";
	}	
	
	printf("Text = %s$\n", Text);
	int n = strlen(Text)+1;
	int i, j;
	
	// allocate
	uint_t *SA = (uint_t *)malloc(n * sizeof(uint_t));
	int_t *LCP = (int_t *)malloc(n * sizeof(int_t));
	
	// sort
	sacak_lcp((unsigned char *)Text, (uint_t*)SA, LCP, n);
	
	// output
	printf("i\tSA\tLCP\tBWT\tsuffixes\n");
	for(i = 0; i < n; ++i) {
	    char j = (SA[i])? Text[SA[i]-1]:'$';
	    printf("%d\t%d\t%d\t%c\t",i, SA[i],LCP[i],j);
	    for(j = SA[i]; j < n; ++j) {
	        printf("%c", Text[j]);
	    }
	    printf("$\n");
	}
/**/
	// deallocate
	free(SA);
	free(LCP);
	if(argc==2) free(Text);
	
return 0;
}
