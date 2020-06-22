#include "stern.h"
#include "utils.h"
#include "xoshiro256starstar.h"
#include "libpopcnt.h"
#include <time.h>
#include <immintrin.h>

#define STERN_GET(tab, index) ((tab[index >> 6]) >> (index & 0x3f)) & 1
#define STERN_SET_ONE(tab, index) (tab[index >> 6]) |= (1ULL << (index & 0x3f))

mzd_t* isd_stern_canteaut_chabaud_p2_sort(mzd_t* G, uint64_t time_sec, uint64_t sigma, uint64_t radix_width, uint64_t radix_nlen) {

    // time mesuring stuff
    clock_t start = clock(), current;
    double elapsed = 0.0;

    // p is the Stern p parameter. (= number of LC to make for each rows subsets)
    uint64_t p = 2, iter = 0;
    rci_t n = G->ncols, comb1[2 /* p */], comb2[2 /* p */], min_comb[4 /* 2*p */],lambda = 0, mu = 0, tmp = 0, i = 0, j = 0;
    rci_t k = n/2; // number of rows in G

    int min_wt = 1000, wt = 0;
    void* row = NULL;
    (void)row; // otherwise gcc is :-(

#if defined(AVX512_ENABLED)
    uint64_t mask[10];
    memset(mask, 0, 80);
#endif

#if defined(DEBUG)
    uint64_t nb_collision = 0, nb_collision_delta = 0, nb_collision_delta_current = 0;
#endif

    uint64_t* word = NULL;
    uint64_t* linear_comb = (uint64_t*)malloc(sizeof(uint64_t) * 10);
    uint64_t* linear_comb_next = (uint64_t*)malloc(sizeof(uint64_t) * 10);
    uint64_t* min_cw = (uint64_t*)malloc(sizeof(uint64_t) * 10);
    rci_t* column_perms_copy =  (rci_t*) malloc(sizeof(rci_t) * n);
    rci_t* column_perms = (rci_t*) malloc(sizeof(rci_t) * n);
    CHECK_MALLOC(column_perms);
    CHECK_MALLOC(column_perms_copy);
    CHECK_MALLOC(linear_comb);
    CHECK_MALLOC(min_cw);

    for (i = 0; i < n; i++) column_perms[i] = i;

    mzd_t* Gtemp = mzd_copy(NULL, G);

    // Ensure that we work with a systematic generator matrix
    rref_to_systematic(Gtemp, column_perms);
    mzd_t* Glw = mzd_submatrix(NULL, Gtemp, 0, k, k, n);

    // nelem is the max number of LCs we will make
    // so its an upper bound for the size of each array
    uint32_t nelem = ((n/4 * (n/4 - 1)) /2);

    // Number of possible dfferent values for delta
    uint32_t nb_keys = 1UL << sigma;
    uint32_t nb_keys_bits = nb_keys / 8;

    uint64_t** collisions_first_pass = (uint64_t**)malloc( m * sizeof(uint64_t*));
    uint64_t** collisions_second_pass = (uint64_t**)malloc( m * sizeof(uint64_t*));
    lc** lc_tab_first = (lc**)malloc(m * sizeof(lc*));
    lc** lc_tab_second = (lc**)malloc(m * sizeof(lc*));
    lc** lc_tab_third = (lc**)malloc(m * sizeof(lc*));
    lc** lc_tab_second_sorted = (lc**)malloc(m * sizeof(lc*));
    lc** lc_tab_third_sorted = (lc**)malloc(m * sizeof(lc*));

    CHECK_MALLOC(collisions_first_pass);
    CHECK_MALLOC(collisions_second_pass);
    CHECK_MALLOC(lc_tab_first);
    CHECK_MALLOC(lc_tab_second);
    CHECK_MALLOC(lc_tab_third);
    CHECK_MALLOC(lc_tab_second_sorted);
    CHECK_MALLOC(lc_tab_third_sorted);

    for (mwin = 0; mwin < m; mwin++) {
        collisions_first_pass[mwin] = (uint64_t*)malloc(nb_keys_bits);
        collisions_second_pass[mwin] = (uint64_t*)malloc(nb_keys_bits);
        lc_tab_first[mwin] = (lc*)malloc(nelem * sizeof(lc));
        lc_tab_second[mwin] = (lc*)malloc(nelem * sizeof(lc));
        lc_tab_third[mwin] = (lc*)malloc(nelem * sizeof(lc));
        lc_tab_second_sorted[mwin] = (lc*)malloc(nelem * sizeof(lc));
        lc_tab_third_sorted[mwin] = (lc*)malloc(nelem * sizeof(lc));

        CHECK_MALLOC(collisions_first_pass);
        CHECK_MALLOC(collisions_second_pass);
        CHECK_MALLOC(lc_tab_first[mwin]);
        CHECK_MALLOC(lc_tab_second[mwin]);
        CHECK_MALLOC(lc_tab_second_sorted[mwin]);
        CHECK_MALLOC(lc_tab_third[mwin]);
        CHECK_MALLOC(lc_tab_third_sorted[mwin]);
    }

    // These arrays hold the size of their corresponding lc_tab
    uint32_t* lc_tab_first_size = (uint32_t*) malloc(m * sizeof(uint32_t));
    uint32_t* lc_tab_second_size = (uint32_t*) malloc(m * sizeof(uint32_t));
    uint32_t* lc_tab_third_size = (uint32_t*) malloc(m * sizeof(uint32_t));
    uint32_t* lc_indexes = (uint64_t*)malloc(m * sizeof(uint32_t));
    CHECK_MALLOC(lc_tab_first_size);
    CHECK_MALLOC(lc_tab_second_size);
    CHECK_MALLOC(lc_tab_third_size);
    CHECK_MALLOC(lc_indexes);

    // Generic alias on the sorted arrays
    lc* lc_tab_alias_second = NULL;
    lc* lc_tab_alias_third_sorted = NULL;

    // Precomputed mask for the window on which we want collision
    uint64_t sigmask = (1 << sigma) - 1;

    // Radixsort offsets array
    uint32_t *aux = (uint32_t*) malloc((1 << radix_width) * sizeof(uint32_t));

    while (1) {

        // TODO: make c iterations here

        /* Start of the Canteaut-Chabaud stuff, to derive a new information set. */
        // Find lambda, mu s.t. Glw[lambda, mu] == 1
        lambda = xoshiro256starstar_random() % k;
        word = Glw->rows[lambda];

        mu = xoshiro256starstar_random() % 10;
        // Assuming a whole row can't be zero
        while (word[mu] == 0) {
            mu = (mu + 1) % 10;
        }

        j  = xoshiro256starstar_random() % 64;
        uint64_t val = ((word[mu] << (64 - j)) | (word[mu] >> j));
        j = (j + _tzcnt_u64(val)) % 64;

#if defined(AVX512_ENABLED)
        uint64_t big_mu = mu, small_mu = j;
#endif
        mu = mu * 64 + j;

       // Log the column swapping
        tmp = column_perms[lambda];
        column_perms[lambda] = column_perms[mu + 640 /* k */];
        column_perms[mu + 640 /* k */] = tmp;

        // Clear the bit at the intersection of the lambda'th row and the mu'th column
        // so we don't have to rewrite a 1 in mu'th column everytime we add the lambda'th row
        mzd_write_bit(Glw, lambda, mu, 0);

#if defined(AVX512_ENABLED)
        void* row_lambda = Glw->rows[lambda];
        __m512i rlambda1 = _mm512_loadu_si512(row_lambda);
        __m128i rlambda2 = _mm_loadu_si128(row_lambda + 64 /* = 512 / (8 * sizeof(void)) */);

        mask[big_mu] = (1ULL << small_mu);
        __m512i m1 = _mm512_loadu_si512(mask);
        __m128i m2 = _mm_loadu_si128(((void*)mask) + 64 /* = 512/(8 * sizeof(void)) */);
#endif

        // Add the lambda'th row to every other row that have a 1 in the mu'th column
        for (j = 0; j < k; j++) {
            row = Glw->rows[j];

#if defined(AVX512_ENABLED)
            // Load the whole row
            __m512i rj1 = _mm512_loadu_si512(row);
            __m128i rj2 = _mm_loadu_si128(row + 64 /* = 512 / (8 * sizeof(void)) */);

            // Check whether there is a one in column mu using the mask
            if (j != lambda && (_mm512_test_epi64_mask(rj1, m1) >= 1 || _mm_test_epi64_mask(rj2, m2) >= 1)) {

                // Perform row addition
                _mm512_storeu_si512(row, _mm512_xor_si512(rlambda1, rj1));
                _mm_storeu_si128(row + 64, _mm_xor_si128(rlambda2, rj2));
#else
            if (j != lambda && mzd_read_bit(Glw, j, mu) == 1) {
                mzd_row_add(Glw, lambda, j);
#endif
            }
        }

         // Unclear the bit we removed earlier
        mzd_write_bit(Glw, lambda, mu, 1);

#if defined(AVX512_ENABLED)
        // Clear the mask for the next iteration
        mask[mu/64] = 0;
#endif

        /* End of the Canteaut-Chabaud stuff. We now have a proper Iset to work with. */

        // Reset all the stuff we will need in the iteration
        memset(lc_indexes, 0, m * sizeof(uint64_t));
        for (mwin = 0; mwin < m; mwin++) {
            memset(collisions_first_pass[mwin], 0, nb_keys_bits);
            memset(collisions_second_pass[mwin], 0, nb_keys_bits);

        }

        // 1st pass, gen all the LC from the first k/2 rows
        for (comb1[0] = 0; comb1[0]  < 320 /* n/4 */; comb1[0]++) {

            uint64_t* row1 = (uint64_t*)Glw->rows[comb1[0]];

            for (comb1[1] = comb1[0] + 1; comb1[1] < n/4; comb1[1]++) {

                uint64_t* row2 = (uint64_t*)Glw->rows[comb1[1]];

                for (mwin = 0; mwin < m; mwin++) {

                    // Compute the first sigma bits of the LC of rows 1 & 2
                    // on the windows mwin
                    // TODO: SIMD xor ?
                    delta = (row1[mwin] ^ row2[mwin]) & sigmask;

                    lc_tab_first[mwin][lc_indexes[mwin]].index1 = comb1[0];
                    lc_tab_first[mwin][lc_indexes[mwin]].index2 = comb1[1];
                    lc_tab_first[mwin][lc_indexes[mwin]].delta = delta;

                    STERN_SET_ONE(collisions_first_pass[mwin], delta);
                    lc_indexes[mwin]++;
                }
            }
        }

        // Save the size of each lc_tab_first elements and reset lc_indexes
        memcpy(lc_tab_first_size, lc_indexes, m * sizeof(uint32_t));
        memset(lc_indexes, 0, m * sizeof(uint64_t));

        // 2nd pass, gen all the LC from the k/2 to k rows but store
        // only the ones that will collides with at least 1 element from lc_tab_first
        for (comb2[0] = 320 /* n/4 */; comb2[0]  < 640 /* n/2 */; comb2[0]++) {

            // Get the first sigma bits of the first row
            uint64_t* row1 = (uint64_t*)Glw->rows[comb2[0]];

            for (comb2[1] = comb2[0] + 1; comb2[1] < 640 /* n/2 */; comb2[1]++) {

                uint64_t* row2 = (uint64_t*)Glw->rows[comb2[1]];

                for (mwin = 0; mwin < m; mwin++) {

                    // Compute the first sigma bits of the LC of rows 1 & 2
                    delta = (row1[mwin] ^ row2[mwin]) & sigmask;

                    if (collision_tab_first[delta]) {

                        lc_tab_second[mwin][lc_indexes[mwin]].index1 = comb2[0];
                        lc_tab_second[mwin][lc_indexes[mwin]].index2 = comb2[1];
                        lc_tab_second[mwin][lc_indexes[mwin]].delta = delta;

                        STERN_SET_ONE(collision_second_pass[mwin], delta);
                        lc_indexes[mwin]++;
                    }
                }
            }
        }

        // Save the size of each lc_tab_first elements and reset lc_indexes
        memcpy(lc_tab_second_size, lc_indexes, m * sizeof(uint32_t));
        memset(lc_indexes, 0, m * sizeof(uint64_t));

        // 3rd pass, copy all the element of first tab that will actually collide in a new array
        for (mwin = 0; mwin < m; mwin++) {
            for (i = 0; i < lc_tab_first_size[mwin]; i++) {
                uint64_t d = lc_tab_first[mwin][i].delta;
                if (STERN_GET(collisions_second_pass[mwin],d)) {
                    memcpy(lc_tab_third[mwin][lc_indexes[mwin]], lc_tab_first[mwin][i], sizeof(lc));
                    lc_indexes[mwin]++;
                }
            }
        }

        // Save the size of each lc_tab_first elements and reset lc_indexes
        memcpy(lc_tab_third_size, lc_indexes, m * sizeof(uint32_t));
        memset(lc_indexes, 0, m * sizeof(uint64_t));

        /* From here, lc_tab_third[i][:lc_tab_third_size[i]] and lc_tab_second[i][:lc_tab_second_size[i]]
        *  contains the LCs that WILL collide.
        * So we sort em all */

        for (mwin = 0; mwin < m; mwin++) {
            lc_tab_alias_second_sorted[mwin] = radixsort(lc_tab_second[mwin], lc_tab_second_sorted[mwin], lc_tab_second_size[mwin], radix_width, radix_nlen, aux);
            lc_tab_alias_third_sorted[mwin] = radixsort(lc_tab_third[mwin], lc_tab_third_sorted[mwin],lc_tab_third_size[mwin] , radix_width, radix_nlen, aux);
        }

        // TODO : "merge" the two lists

        for (comb2[0] = 320 /* n/4 */; comb2[0]  < 640 /* n/2 */; comb2[0]++) {

            uint64_t row1 = ((uint64_t*)Glw->rows[comb2[0]])[0];

            for (comb2[1] = comb2[0] + 1; comb2[1] < 640 /* n/2 */; comb2[1]++) {

                // Compute the "key" of the linear combination
                uint64_t row2 = ((uint64_t*)Glw->rows[comb2[1]])[0];

                // Compute the first sigma bits of the LC of rows 1 & 2
                delta = (row1 ^ row2) & sigmask;

                // And check if some elements from the previous set already had this key
                lc_index  = lc_offsets[delta];

                if (lc_tab[lc_index].delta == delta) {

#if defined(AVX512_ENABLED)
                    void* row3 = Glw->rows[comb2[0]];
                    void* row4 = Glw->rows[comb2[1]];

                    __m512i linear_comb_high = _mm512_loadu_si512(row3);
                    __m128i linear_comb_low = _mm_loadu_si128(row3 + 64);

                    linear_comb_high = _mm512_xor_si512(linear_comb_high, _mm512_loadu_si512(row4));
                    linear_comb_low = _mm_xor_si128(linear_comb_low, _mm_loadu_si128(row4 + 64));
#else

                    mxor(linear_comb,(uint64_t*)linear_comb, 10);
                    mxor(linear_comb, (uint64_t*)Glw->rows[comb2[0]], 10);
                    mxor(linear_comb, (uint64_t*)Glw->rows[comb2[1]], 10);

#endif

#if defined(DEBUG)
                    nb_collision_delta += nb_collision_delta_current;
                    nb_collision_delta_current  = 0;
#endif
                    while (lc_index < nelem && lc_tab[lc_index].delta == delta) {

                        comb1[0] = lc_tab[lc_index].index1;
                        comb1[1] = lc_tab[lc_index].index2;

                        lc_index++;

#if defined(DEBUG)
                        nb_collision++;
                        nb_collision_delta_current = 1;
#endif


#if defined(AVX512_ENABLED)
                        void* row1 = Glw->rows[comb1[0]];
                        void* row2 = Glw->rows[comb1[1]];

                        // Load the two new rows and add them to the LC of the two previous ones.
                        __m512i linear_comb_high_next = _mm512_xor_si512(linear_comb_high, _mm512_loadu_si512(row1));
                        __m128i linear_comb_low_next = _mm_xor_si128(linear_comb_low, _mm_loadu_si128(row1 + 64));

                        linear_comb_high_next = _mm512_xor_si512(linear_comb_high_next, _mm512_loadu_si512(row2));
                        linear_comb_low_next = _mm_xor_si128(linear_comb_low_next, _mm_loadu_si128(row2 + 64));

                        // Save the result of the LC of the 4 rows
                        _mm512_storeu_si512(linear_comb_next, linear_comb_high_next);
                        _mm_storeu_si128((__m128i*)(linear_comb_next + 8), linear_comb_low_next);
#else
                        memcpy(linear_comb_next, linear_comb, 80);
                        mxor(linear_comb_next, (uint64_t*)Glw->rows[comb1[0]], 10);
                        mxor(linear_comb_next, (uint64_t*)Glw->rows[comb1[1]], 10);

#endif
                        //printf("DBG Linear comb is : \n");
                        //printbin(linear_comb_next, 640);

                        wt = popcnt64_unrolled(linear_comb_next, 10);

                        if (wt < min_wt) {

                            // Save the new min weight and the indexes of th e linear combination to obtain it
                            current = clock();
                            elapsed = ((double)(current - start))/CLOCKS_PER_SEC;
                            printf("niter=%lu, time=%.3f, wt=%ld\n", iter, elapsed, wt + 2*p);

                            min_wt = wt;
                            // Save the indexes of the LC
                            memcpy(min_comb, comb1, p * sizeof(rci_t));
                            memcpy(min_comb + p, comb2, p * sizeof(rci_t));

                            memcpy(min_cw, linear_comb_next, 80);
                            memcpy(column_perms_copy, column_perms, n * sizeof(rci_t));

                            mzd_t* cw = stern_reconstruct_cw(min_comb, column_perms_copy, min_cw, p);
                            print_cw(cw);
                            mzd_free(cw);

                            fflush(stdout);
                        }
                    }
                }
            }

        }

        iter++;
        current = clock();
        elapsed = ((double)(current - start))/CLOCKS_PER_SEC;
        if (elapsed > time_sec) {
            break;
        }

    }

#ifdef DEBUG
    printf("# Average number of collisions / iter : %.3f\n", (double)nb_collision/(double)iter);
    printf("# Average number of collision / delta with at least 1 collision: %.3f\n", (double)nb_collision/(double)nb_collision_delta);
    printf("# Average number of delta with at least 1 collision / nb delta : %3.f / %u\n", (double)nb_collision_delta/(double)iter, nelem);
#endif
    printf("# Total iter done : %lu\n", iter);
    printf("# Iter/s : %.3f\n", (double)iter/(double)time_sec);

    mzd_t* result =  stern_reconstruct_cw(min_comb, column_perms_copy, min_cw, p);

    mzd_free(Gtemp);
    mzd_free(Glw);

    free(min_cw);
    free(linear_comb);
    free(linear_comb_next);
    free(column_perms_copy);
    free(column_perms);
    free(lc_offsets);
    free(lc_tab);
    free(aux);

    return result;
}

int compare_lc(const void* a, const void* b) {
    return ((lc*)a)->delta > ((lc*)b)->delta;
}


lc* denomsort_r(lc* T, lc* Ts, int64_t Tlen, uint64_t width, uint64_t pos, uint32_t* Aux) {

    uint32_t mask, k;
    int64_t i;
    k = 1 << width;
    mask = k - 1;

    memset(Aux, 0, k * sizeof(uint32_t));

    for (i = 0; i < Tlen; i++) {
        Aux[ (T[i].delta >> pos) & mask]++;
    }

    for (i = 1; i < k; i++) {
        Aux[i] += Aux[i - 1];
    }

    for (i = Tlen - 1; i >= 0; i--) {
        uint32_t val = (T[i].delta >> pos) & mask;
        Aux[val]--; Ts[ Aux[val]] = T[i];
    }

    return Ts;
}

lc* radixsort(lc* T, lc* Ts, int64_t Tlen, uint64_t width, uint64_t nlen, uint32_t* aux) {

    int i;
    lc* tmp;

    for (i = 0; i < nlen; i++) {
        tmp = T;
        T = denomsort_r(T, Ts, Tlen, width, i*width, aux);
        Ts = tmp;
    }

    return T;
}
