#include "bloom.h"
extern void ref_add (bloom *bl, char *position);
extern void fastq_add (bloom *bl, char *position);
extern void fasta_add (bloom *bl, char *position);
extern char *fasta_data (bloom *bl_2, char *data);
extern void init_bloom (bloom *bl, BIGNUM capacity, float error_rate, int k_mer);
extern int build(char *ref_name, char *target_path, int k_mer, double error_rate);
extern int build_main(int k_mer, float error_rate, char *source, char *list, char *prefix, char *target_path, int help);