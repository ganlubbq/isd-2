#include "libpopcnt.h"
#include "utils.h"

void fisher_yates_shuffle(rci_t* values, size_t size) {

    size_t i,j, tmp;

    for (i = size - 1; i > 0; i--) {
        j  = rand() % (i + 1);
        tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
    }
}

int load_challenge(char* filename, mzd_t* G, mzd_t* H) {

    FILE* fp = NULL;
    rci_t i = 0, j = 0;

    // size of the non-identity part of H
    const int NROWS = G->nrows;
    const int NCOLS = G->ncols / 2;

    char line[NROWS + 2]; // line are at most NROWS char + '\n' + '\0'

    fp = fopen(filename, "r");

    if (!fp) {
        fprintf(stderr, "Error in %s: failed to open the file %s for reading.\n", __func__, filename);
        return 0;
    }

    // Skip the 5 first lines (comments or constant stuff)
    char buf[1200];

    for (i = 0; i < 5; i++) {
        if (!fgets(buf, 1200, fp)) {
            fprintf(stderr, "Error in %s: failed to skip line (perhaps the file is incomplete?).\n", __func__);
            return 0;
        }
    }

    mzd_t* M = mzd_init(NROWS, NCOLS);
    mzd_t* Ik = mzd_init(NROWS, NCOLS);

    for (i = 0; i < NROWS; i++) {

        if (!fgets(line, NROWS + 2, fp)) {
            fprintf(stderr, "Error in %s: failed to read line (perhaps the file is incomplete?).\n", __func__);
            return 0;
        }

        for (j = 0; j < NCOLS; j++) {

            if (i == j) {
                mzd_write_bit(Ik, i, j, 1);
            }

            mzd_write_bit(M, i, j, line[j] == '1' ? 1 : 0);
        }
    }

    mzd_concat(G, M, Ik); // G = [M| Ik]
    mzd_echelonize(G, 1);

    mzd_t* Mt = mzd_transpose(NULL, M);
    mzd_concat(H, Ik, Mt); // H = [Ik | M^t]

    mzd_free(M);
    mzd_free(Ik);
    mzd_free(Mt);

    fclose(fp);

    return 1;
}

int left_is_identity(const mzd_t* M) {

    rci_t n = M->nrows;

    for (rci_t i = 0; i < n; i++) {
        if (mzd_read_bit(M, i, i) != 1 || popcnt(mzd_row(M, i), n/8) > 1) {
            return 0;
        }
    }

    return 1;
}

void print_cw(mzd_t* cw) {
    printf("cw = ");
    for (int i = 0; i < cw->ncols; i++) printf(mzd_read_bit(cw, 0, i) ? "1" : "0");
    printf("\n");
}


void rref_to_systematic(mzd_t* M, rci_t* perms) {

    rci_t n = M->ncols, cur, old;
    mzd_t* Mt = mzd_init(n, n/2);

    while (!left_is_identity(M)) {

        mzd_transpose(Mt, M);

        // find a column with only one 1 in the right part of M
        for (cur = n/2; cur < n && popcnt(mzd_row(Mt, cur), n/16) != 1; cur++);

        // find a column with more than one 1 in the left part of M
        for (old = 0; old < n/2 && popcnt(mzd_row(Mt, old), n/16) == 1; old++);

        // And swap them
        mzd_col_swap(M, old, cur);
        rci_t tmp = perms[cur];
        perms[cur] =  perms[old];
        perms[old] = tmp;

        mzd_echelonize(M, 1);
    }

    mzd_free(Mt);
}

void mxor(uint64_t* dst, uint64_t* src, size_t size) {
    for (size_t i = 0; i < size; i++)
        dst[i] ^= src[i];
}

uint64_t uxor(uint64_t*a, uint64_t*b , size_t nbits) {
    return (a[0] >> (64 - nbits)) ^ (b[0] >> (64 - nbits));
}

void printbin(uint64_t* a, size_t nbits) {

    for (size_t i = 0; i < nbits; i++) {
        printf("%ld", (a[i/64] >> ( 63 - (i%64))) & 1);
    }

    printf("\n");
}



