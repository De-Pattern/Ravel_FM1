/* Lookup tables for the fixed-point Beads port. Generated: see tools/gen_bd_tables.py. */
#ifndef BD_TABLES_H
#define BD_TABLES_H
#include <stdint.h>

extern const uint32_t bd_lut_exp2_q30[257];
extern const int32_t bd_lut_softclip_q15[513];
extern const int16_t bd_lut_ulaw_q15[256];
extern const int16_t bd_lut_sine_q15[1281];
extern const int32_t bd_lut_inv_sqrt_q15[65];

#endif
