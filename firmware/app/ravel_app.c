/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the whole instrument as one unit (the modules share their statics). The platform
 * (plat.h) and the Beads port (core/beads) are built beside it. */
#include "rv_params.c"
#include "rv_synth.c"
#include "rv_strike.c"
#include "rv_mod.c"
#include "rv_tape.c"
#include "rv_bus.c"
#include "rv_seq.c"
#include "rv_engine.c"
#include "rv_presets.c"
#include "rv_project.c"
#include "rv_ui.c"

/* start: the last project saved in slot 0 ("the session"), else the first demo */
void ravel_start(void)
{
    rv_project_init();
    if (rv_load(0) != 0)
        rv_demo(0);
    rv_engine_init();
    ui_init();
}
