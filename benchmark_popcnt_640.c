#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "simd.h"
#include "xoshiro256starstar.h"
#include "libpopcnt.h"

void populate(uint64_t* vals, int n) {
    for (int i = 0; i < n; i += 10) {
        vals[i + 0] = xoshiro256starstar_random();
        vals[i + 1] = xoshiro256starstar_random();
        vals[i + 2] = xoshiro256starstar_random();
        vals[i + 3] = xoshiro256starstar_random();
        vals[i + 4] = xoshiro256starstar_random();
        vals[i + 5] = xoshiro256starstar_random();
        vals[i + 6] = xoshiro256starstar_random();
        vals[i + 7] = xoshiro256starstar_random();
        vals[i + 8] = xoshiro256starstar_random();
        vals[i + 9] = xoshiro256starstar_random();
    }
}


int main(void) {

    int n = 10;
    int niter = 1000000000;
    uint64_t seed[4] = {1,2,3,4};

    uint64_t* vals = (uint64_t*) malloc( n * sizeof(uint64_t));

    clock_t start,stop;
    double time_elapsed;
    uint64_t sum = 0;

    xoshiro256starstar_random_set(seed);
    start = clock();
    for (int i = 0; i < niter; i++) {
        populate(vals, n);
        sum +=  my_popcnt(vals, 10*8);
    }

    stop = clock();
    time_elapsed = (((double)(stop - start))/CLOCKS_PER_SEC );
    printf("my_popcnt:\n");
    printf("sum: %lu\n", sum);
    printf("niter: %d\n", niter);
    printf("time: %.3f\n", time_elapsed);
    printf("iter/s: %.3f\n", ((double)niter) / ((double) time_elapsed));
    printf("Gb/s: %.3f\n\n", ((((double)niter) * 10.0)/ (1024.0*1024.0*1024.0)) / time_elapsed);

    sum = 0;
    xoshiro256starstar_random_set(seed);
    start = clock();
    for (int i = 0; i < niter; i++) {
        populate(vals, n);
        sum += popcnt(vals, 10*8);
    }

    stop = clock();
    time_elapsed = (((double)(stop - start))/CLOCKS_PER_SEC);
    printf("libpopcnt classic:\n");
    printf("sum: %lu\n", sum);
    printf("niter: %d\n", niter);
    printf("time: %.3f\n", time_elapsed);
    printf("iter/s: %.3f\n", ((double)niter) / ((double) time_elapsed));
    printf("Gb/s: %.3f\n\n", ((((double)niter) * 10.0)/ (1024.0*1024.0*1024.0)) / time_elapsed);

    sum = 0;
    xoshiro256starstar_random_set(seed);
    start = clock();
    for (int i = 0; i < niter; i++) {
        populate(vals, n);
        sum += popcnt64_unrolled((const uint64_t*)vals, 10);
    }

    stop = clock();
    time_elapsed = (((double)(stop - start))/CLOCKS_PER_SEC);
    printf("libpopcnt advanced calls:\n");
    printf("sum: %lu\n", sum);
    printf("niter: %d\n", niter);
    printf("time: %.3f\n", time_elapsed);
    printf("iter/s: %.3f\n", ((double)niter) / ((double) time_elapsed));
    printf("Gb/s: %.3f\n\n", ((((double)niter) * 10.0)/ (1024.0*1024.0*1024.0)) / time_elapsed);

    sum = 0;
    xoshiro256starstar_random_set(seed);
    start = clock();
    for (int i = 0; i < niter; i++) {
        populate(vals, n);
        sum += popcnt640(vals);
    }

    stop = clock();
    time_elapsed = (((double)(stop - start))/CLOCKS_PER_SEC);
    printf("popcnt640:\n");
    printf("sum: %lu\n", sum);
    printf("niter: %d\n", niter);
    printf("time: %.3f\n", time_elapsed);
    printf("iter/s: %.3f\n", ((double)niter) / ((double) time_elapsed));
    printf("Gb/s: %.3f\n\n", ((((double)niter) * 10.0)/ (1024.0*1024.0*1024.0)) / time_elapsed);


    free(vals);
}
