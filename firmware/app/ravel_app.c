/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the whole instrument as one unit (the modules share their statics). The platform
 * (plat.h) and the Beads port (core/beads) are built beside it. */
#include "rv_params.c"
#include "rv_synth.c"
#include "rv_strike.c"
#include "rv_perc.c"
#include "rv_mod.c"
#include "rv_tape.c"
#include "rv_bus.c"
#include "rv_seq.c"
#include "rv_engine.c"
#include "rv_presets.c"
#include "rv_project.c"
#include "rv_ui.c"

/* start: always a blank project */
void ravel_start(void)
{
    rv_demo(2);                     /* a blank project; saved ones stay in their slots (SAVE > LOAD) */
    rv_engine_init();
    ui_init();
}
