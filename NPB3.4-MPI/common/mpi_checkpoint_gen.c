#include <ctype.h>
#include <stdio.h>
#include <string.h>

const char* symbols[] = {
    "mpi_checkpoint_create",
    "mpi_checkpoint_restore",
    "mpi_checkpoint_close",
    "mpi_checkpoint_init",
    "mpi_checkpoint_finalize",
    "mpi_checkpoint_write",
    "mpi_checkpoint_read",
};

void generate_weak_symbols() {
    const int nsymbols = sizeof(symbols) / sizeof(const char*);
    char newname[4096];
    for (int i=0; i<nsymbols; ++i) {
        const size_t n = strlen(symbols[i]);
        // uppercase
        strncpy(newname, symbols[i], sizeof(newname));
        for (int j=0; j<n; ++j) { newname[j] = toupper(newname[j]); }
        fprintf(stdout, "#pragma weak %s = %s_\n", newname, symbols[i]);
        // lowercase
        strncpy(newname, symbols[i], sizeof(newname));
        fprintf(stdout, "#pragma weak %s = %s_\n", symbols[i], symbols[i]);
        // double underscores
        fprintf(stdout, "#pragma weak %s__ = %s_\n", symbols[i], symbols[i]);
        // underscore + f
        fprintf(stdout, "#pragma weak %s_f = %s_\n", symbols[i], symbols[i]);
        // underscore + f08
        fprintf(stdout, "#pragma weak %s_f08 = %s_\n", symbols[i], symbols[i]);
    }
}

int main() {
    generate_weak_symbols();
    return 0;
}
