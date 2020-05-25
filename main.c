#include "utils.h"
#include "libpopcnt.h"
#include "isd.h"

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>


int main(void) {

    int seed = time(0);
    clock_t start, stop;
    double time_elapsed;

    uint32_t n = 1280; // Size of the instance
    int niter = 10000;
    mzd_t* G = mzd_init(n/2, n);
    mzd_t* H = mzd_init(n/2, n);

    if (!load_challenge("challenges/LW_1280_0", G, H)) {
        return 1;
    }

    srand(seed);
    start = clock();
    mzd_t* min_cw = isd_prange_canteaut(G, niter);

    if (!min_cw) {
        printf("failed, leaving.\n");
        return 0;
    }

    stop = clock();
    time_elapsed = ((double)(stop - start))/CLOCKS_PER_SEC;

    mzd_t* Hct = mzd_mul(NULL, H, mzd_transpose(NULL, min_cw), 0);

    printf("Min codeword found : \n");
    printf("wt : %ld\n", popcnt(mzd_first_row(min_cw), n/8 + (n % 8 != 0)));
    printf("Verif : %s\n" , mzd_is_zero(Hct) ? "ok" : "nok");
    printf("Total time: %.3f\n", time_elapsed);
    printf("Iter/s : %.3f\n", ((double)niter)/time_elapsed);

    mzd_print(min_cw);
    mzd_free(min_cw);
    mzd_free(G);
    mzd_free(H);
}
