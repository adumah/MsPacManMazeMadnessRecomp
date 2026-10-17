/*
 * Ms. Pac-Man Maze Madness: 16:9 widescreen.
 *
 * On PSX, widescreen is owned by the title's mod catalog: the runtime clamps
 * the display aspect to 4:3 unless a trusted plugin requests a wider view.
 * This plugin requests 16:9 when its feature is enabled. The runtime then
 * widens the 3D view, keeps FMV, BIOS and 2D-only frames pillarboxed at 4:3,
 * and game.toml [widescreen] auto_ui_squash keeps the HUD at its original
 * proportions.
 */
#include "mod_plugins.h"
#include "cpu_state.h"

static void mpmm_widescreen_activate(void) {
    (void)psx_mod_set_fixed_display_aspect(16, 9);
}

/*
 * Squash mode ([widescreen] native_wide = false) compresses every GTE
 * projection horizontally and stretches the finished picture back out. The
 * game draws some objects, Ms. Pac-Man's body among them, as screen-aligned
 * billboards: the GTE projects only the centre, and the CPU adds a half-width
 * computed from the depth. That half-width never passes through the GTE, so
 * those billboards came out too wide after the stretch.
 *
 * The billboard builders compute a horizontal extent on the CPU and write
 * centre -/+ extent to the quad. Each hook scales that extent by the squash
 * factor (160 / (160 + squash margin), 0.75 at 16:9) just before it is used:
 *   0x80049F3C  srl $v1,$a0,31   main builder (Ms. Pac-Man's body): full
 *                                width in $a0, halved here
 *   0x80088BD4  addu $v0,$t0,$a1 flat-textured builder: half-width in $a1
 *   0x8008A534  addu $v0,$t0,$a1 Gouraud-textured builder: half-width in $a1
 * Vertical extents are untouched. At 4:3 the margin is zero and the game is
 * unchanged. Native-wide mode does not squash, so this feature must stay
 * disabled there.
 */
#define MPMM_BILLBOARD_ADDU_A1 0x01051021u /* addu $v0, $t0, $a1 */
#define MPMM_BILLBOARD_SRL_A0  0x00041FC2u /* srl  $v1, $a0, 31  */

/* Scale one guest register (a horizontal billboard extent) by the live squash
 * factor. In squash mode psx_mod_widescreen_x_margin() is
 * 160 * (xden - xnum) / xnum (the 320-px-reference margin of the GTE X-squash
 * xnum/xden; this title declares no cull guard pixels), so the factor is
 * 160 / (160 + margin): 0.75 at 16:9. It is 0, and the extent unchanged,
 * whenever the squash is not engaged. */
static void mpmm_squash_extent(struct CPUState* cpu, int reg) {
    const int32_t margin = psx_mod_widescreen_x_margin();
    if (margin <= 0) {
        psx_mod_counter_add("mpmm.billboard.unscaled", 1);
        return;
    }
    psx_mod_counter_add("mpmm.billboard.scaled", 1);
    const int32_t den = 160 + margin;
    const int32_t v = (int32_t)cpu->gpr[reg];
    cpu->gpr[reg] = (uint32_t)((v * 160 + (v >= 0 ? den / 2 : -den / 2)) / den);
}

/* Half-width in $a1 at the 0x954-scaled builders (0x80088BD4, 0x8008A534). */
static void mpmm_squash_half_width_a1(struct CPUState* cpu, uint32_t address) {
    (void)address;
    mpmm_squash_extent(cpu, 5);
}

/* Full width in $a0 at the main billboard builder (0x80049F3C): Ms. Pac-Man's
 * body. It halves $a0 here and writes centre -/+ half to the quad. */
static void mpmm_squash_full_width_a0(struct CPUState* cpu, uint32_t address) {
    (void)address;
    mpmm_squash_extent(cpu, 4);
}

static void mpmm_squash_billboards_activate(void) {
    psx_mod_counter_add("mpmm.billboard.active", 1);
}

PSX_MOD_CONSTRUCTOR(mpmm_register_widescreen_plugin) {
    (void)psx_mod_register_activation_plugin("mpmm.squash-billboards",
                                             mpmm_squash_billboards_activate);
    (void)psx_mod_register_activation_plugin("mpmm.widescreen",
                                             mpmm_widescreen_activate);
    (void)psx_mod_register_instruction_plugin("mpmm.squash-billboards",
        0x80088BD4u, MPMM_BILLBOARD_ADDU_A1, mpmm_squash_half_width_a1);
    (void)psx_mod_register_instruction_plugin("mpmm.squash-billboards",
        0x8008A534u, MPMM_BILLBOARD_ADDU_A1, mpmm_squash_half_width_a1);
    (void)psx_mod_register_instruction_plugin("mpmm.squash-billboards",
        0x80049F3Cu, MPMM_BILLBOARD_SRL_A0, mpmm_squash_full_width_a0);
}
