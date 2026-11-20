/* Jarkins: the 6 Costume Slots Mod (jarkins.plates / plate-colours).
 *
 * Each fighter's name plate takes the colour of the costume slot he came in: the
 * bar's palette and the strip's are rebuilt from the slot's own ramp and written
 * into the game's two plate CLUTs each frame, and every copy of the plate prim the
 * HUD builds is pointed at the slot's palette. The tables, the sweep of the packet
 * area and the HUD-entry hook are the ones the mod has always used; they live here
 * now so the add-on carries the feature itself, on any release, with no change to
 * the game.
 *
 * Where the palettes are: the bar's CLUT 7F10 is VRAM (256,508), the strip's 7F11
 * is (272,508); P2 uses 7F12/7F13 at (288,508)/(304,508). One colour table per
 * family, ten of the sixteen entries the plates read.
 */
#include "mod_plugins.h"
#include "gpu_render.h"
#include "jarkins_video.h"

#include <stdint.h>
#include <stdio.h>

/* One family per costume slot, named by the slot: 1 red, 2 green, 3 blue,
 * 4 yellow, 5 orange, 6 purple. Slots 1-4 keep the game's own palettes (7F10 red,
 * 7F12 blue, 7F14 gold, plus the green the mod added); 5 and 6 have no game table
 * and are built from slot 1's brightness profile. */
enum { PLATE_SLOT1, PLATE_SLOT2, PLATE_SLOT3, PLATE_SLOT4, PLATE_SLOT5, PLATE_SLOT6, PLATE_COUNT };
static const uint16_t plate_tables[PLATE_COUNT][16]={
    /* slot 1, red, the game's 7F10 */
                {0x0000,0xF7BE,0xEB5C,0xDEFB,0xD6B9,0xCA58,0xB9D4,0xB5B4,
                 0xA952,0xA110,0x98CE,0x98CE,0x8C6A,0x9069,0xFFFF,0x0000},
    /* slot 2, green, the mod's own */
                {0x0000,0x77DD,0x6F9A,0x6378,0x5B36,0x5313,0x428F,0x3E8E,
                 0x324B,0x2A09,0x21C7,0x21C7,0x1544,0x1524,0x7FFF,0x0000},
    /* slot 3, blue, the game's 7F12 */
                {0x0000,0xF7BD,0xEF7A,0xE737,0xDEF5,0xD6B2,0xC62E,0xC20D,
                 0xB9CA,0xB188,0xA946,0xA526,0x98C3,0x9CC3,0xFFFF,0x0000},
    /* slot 4, yellow, the game's 7F14 */
                {0x0000,0xF3DE,0xE39D,0xD35C,0xC71B,0xBADA,0xA657,0xA256,
                 0x9A15,0x91D3,0x8D72,0x8D71,0x84EE,0x88ED,0xFFFF,0x0000},
    /* slot 5, orange, built from slot 1's profile */
                {0x0000,0x77BD,0x6F7B,0x6739,0x6318,0x5AD6,0x4E73,0x4631,
                 0x39CE,0x2D6B,0x18C6,0x18C6,0x0C63,0x0842,0x7FFF,0x0000},
    /* slot 6, purple, built from slot 1's profile */
                {0x0000,0x0421,0x0842,0x0C63,0x1084,0x14A5,0x18C6,0x1CE7,
                 0x2108,0x2529,0x6F7B,0x739C,0x77BD,0x7BDE,0x0421,0x7FFF}};
/* The game's own plate palettes, one working slot per player: clut 7F10 is
 * (256,508), 7F12 (288,508) - x = (c & 0x3F) * 16, y = (c >> 6) & 0x1FF. */
static const unsigned plate_slot[2]={0x7F10,0x7F12};
/* The strip under the bar is a polygon of its own (op 0x2D) and reads a palette
 * of its own: 7F11 is (272,508), the slot the game paints the strip with. So
 * each player needs two slots - one for the bar, one for the strip - or the bar
 * recolours while the strip stays the costume's colour. */
static const unsigned strip_slot[2]={0x7F11,0x7F13};
/* The strip's own tables. The strip is drawn additively (op 0x2D) and has a
 * palette of its own - the game's red strip is (272,508):
 *   0000 BDBD B55C A8FC A4DA 9CB8 9877 9455 8C14 8C11 8C0F 880D 880B 0000 ...
 * which is the red bar's ramp made darker and warmer, not the bar's table itself
 * (adding the bar's brightness onto the scene washes the hue out to pink). Each
 * family's strip is the ratio the game puts between the two reds, applied to
 * that family's bar - so red comes out as the game's own strip and the rest keep
 * the same shading. Entries 13 and up are transparent in the game's strip. */
static uint16_t strip_tables[PLATE_COUNT][16];
/* The six colours, as the user gave them (0xRRGGBB), and the bar table each one
 * builds. The game's own tables stay the source of the SHADING: every entry keeps
 * its brightness relative to that family's brightest step, and that fraction of
 * the base colour is what gets written, so a plate keeps its bevel, top edge and
 * outline instead of becoming a flat swatch. Brightness only - the hue and
 * saturation are the base colour's throughout, which is what makes red read as
 * red rather than a pale pink highlight.
 *
 * Red, blue, green and gold are re-authored this way on their own game tables.
 * Slots 5 and 6 were black and white; they are orange and purple now, and had no
 * game table of their own, so they borrow the red family's brightness profile and
 * get a pale letter ink built out of their colour - the same look as the other
 * four, white name on a coloured bar. */
static const unsigned char plate_base[PLATE_COUNT][3]={
    {0xD6,0x00,0x00},   /* slot 1 red    #D60000 - square   */
    {0x01,0x89,0x00},   /* slot 2 green  #018900 - triangle */
    {0x00,0x00,0xD1},   /* slot 3 blue   #0000D1 - cross    */
    {0xDA,0xA5,0x1E},   /* slot 4 yellow #DAA51E - circle   */
    {0xDB,0x6D,0x00},   /* slot 5 orange #DB6D00 - Start    */
    {0x70,0x24,0xB7}};  /* slot 6 purple #7024B7 - R1       */
enum { PLATE_BAR_ENTRY=10 };
static unsigned plate_luma(uint16_t c) {
    return (c&0x1F)*3+((c>>5)&0x1F)*6+((c>>10)&0x1F);   /* 0..310 */
}
static uint16_t plate_shade(const unsigned char *base,unsigned luma,unsigned top,uint16_t stp) {
    unsigned r=base[0]*luma/top,g=base[1]*luma/top,b=base[2]*luma/top;   /* 0..255 */
    return (uint16_t)(((r*31+127)/255)|(((g*31+127)/255)<<5)|(((b*31+127)/255)<<10)|stp);
}
static uint16_t plate_bar[PLATE_COUNT][16];
/* The brightness profile a family's ramp is built from: its own game table, or the
 * red one for the two families the game never had (orange, purple). */
static const uint16_t *plate_profile(unsigned f) {
    return (f==PLATE_SLOT5||f==PLATE_SLOT6)?plate_tables[PLATE_SLOT1]:plate_tables[f];
}
static void plate_build_tables(void) {
    static int built;
    if(built)return;
    built=1;
    for(unsigned f=0;f<PLATE_COUNT;f++)
        for(unsigned i=0;i<16;i++)
            plate_bar[f][i]=plate_tables[f][i];
    for(unsigned f=PLATE_SLOT1;f<PLATE_COUNT;f++) {
        /* Entry 10 is the bar body, 11-13 its border and outline; those take the
         * family colour, with 10 written as the colour exactly and the deeper
         * entries keeping the game's relative brightness.
         *
         * Entries 1-9 are the LETTERS' ink (measured on a real plate: 101, 28, 26,
         * 24, 23, 19, 14, 13 and 11 pixels). The four game families keep the game's
         * own near-neutral ramp, which is what gives the name its white inner
         * shading with the family colour only in the letter's shadow. Recolouring
         * them turns the whole name into a flat block of the colour, which is what
         * the first pass did. */
        const uint16_t *p=plate_profile(f);
        unsigned top=plate_luma(p[PLATE_BAR_ENTRY]);
        if(!top)continue;
        for(unsigned i=PLATE_BAR_ENTRY;i<=13;i++)
            plate_bar[f][i]=plate_shade(plate_base[f],plate_luma(p[i]),top,
                                        (uint16_t)(p[i]&0x8000));
        if(p!=plate_tables[f]) {
            /* Orange and purple: the game has no pale ink for them, so entries 1-9
             * are white warmed by the colour, deep enough by entry 9 that the
             * letters keep a shaded edge, and the name entry (14) goes white. */
            for(unsigned i=1;i<PLATE_BAR_ENTRY;i++) {
                unsigned s=10+(i-1)*6;                     /* 10% .. 58% of the colour */
                unsigned char ink[3];
                for(unsigned c=0;c<3;c++)
                    ink[c]=(unsigned char)((255u*(100-s)+plate_base[f][c]*s)/100u);
                plate_bar[f][i]=plate_shade(ink,255,255,(uint16_t)(p[i]&0x8000));
            }
            plate_bar[f][14]=0x7FFF;
            plate_bar[f][15]=0x0000;
        }
    }
}
/* A family's colour across the game's whole ramp, letters included: entry 1 (the
 * brightest step) is the colour exactly and the rest keep the game's brightness
 * relative to it. The bar only takes entries 10-13 of this - 1-9 stay pale so the
 * name prints white - but the strip is drawn from entry 1 up, so it has to come
 * from here or it inherits the letters' pale ink and washes out. */
static uint16_t plate_full(unsigned f,unsigned i) {
    const uint16_t *p=plate_profile(f);
    unsigned top=plate_luma(p[1]);
    if(!top)return plate_bar[f][i];
    return plate_shade(plate_base[f],plate_luma(p[i]),top,(uint16_t)(p[i]&0x8000));
}
static void plate_build_strips(void) {
    plate_build_tables();
    static int built;
    if(built)return;
    built=1;
    /* One scale for all three channels. Measured off a stock plate: the game's
     * strip palette sits at roughly 85% of the bar's own ramp (its red strip is
     * BDBD B55C A8FC ... against the bar's F7BE EB5C DEFB ...), and the strip is
     * drawn additively, so the bar's table as-is washes out and a mean-ratio
     * scale came out near 60% - which read as dark maroon on the red plate.
     * Scaling all channels together keeps the family's hue (per channel scaling
     * turned green magenta: the strip's warm shift is red's own). */
    for(unsigned f=0;f<PLATE_COUNT;f++)
        for(unsigned i=0;i<16;i++) {
            uint16_t c=plate_full(f,i);
            if(i==0||i>=13) { strip_tables[f][i]=0; continue; }
            unsigned r=(c&0x1F)*17/20,g=((c>>5)&0x1F)*17/20,b=((c>>10)&0x1F)*17/20;
            if(r>31)r=31; if(g>31)g=31; if(b>31)b=31;
            strip_tables[f][i]=(uint16_t)(r|(g<<5)|(b<<10)|(c&0x8000));
        }
}
enum { PLATE_PALETTE_Y=508, PLATE_PALETTE_STEP=32 };
/* Two frames for a full pass of the packet area. It used to be sixteen, and that
 * was the flash of the game's own colour at the start of a round: the prims move
 * when the plate is rebuilt, and until the sweep reached the new address the
 * plates were still drawn with the costume's palette. */
enum { PLATE_SCAN_FROM=0x800C0000, PLATE_SCAN_TO=0x800E0000, PLATE_SWEEP=16384 };
/* Every copy of a player's plate prim. The HUD keeps more than one packet
 * buffer, so patching one copy leaves the buffer that is actually drawn with the
 * game's own palette - that is what "the colour does not commit" looked like. */
enum { PLATE_SITES=16 };   /* bar and strip, for two players, in more than one buffer */
static uint32_t plate_site[PLATE_SITES];   /* prim address, 0 = empty */
static unsigned plate_side[PLATE_SITES];   /* 0 P1, 1 P2 */
static unsigned plate_next;                /* where a new one goes when full */
static uint32_t plate_sweep=PLATE_SCAN_FROM;
/* The pack's table of which button picked each fighter. Absent on a stock copy,
 * where the costume is all there is to go on. */
extern int tekken3_native_pick_key(unsigned slot, unsigned id) __attribute__((weak));
static unsigned plate_family(unsigned player) {
    /* The plate's colour is the SLOT THE BINDING CHOSE, which is the button that
     * picked the fighter, not the costume he ended up in: triangle and circle ride
     * square's and cross's costumes, and R1 picks no game costume at all, so
     * reading the costume alone painted three of the six slots the wrong colour
     * (R1 always red). The pack keeps the same table; this is the same rule, so
     * the two cannot disagree while both are in a build. */
    if (tekken3_native_pick_key) {
        const unsigned id = psx_mod_read_half(0x800add5c + player*2);
        const int k = tekken3_native_pick_key(player, id);
        /* pick_key: 0 square, 1 cross, 2 triangle, 3 circle, 4 Start, 5 R1. */
        static const unsigned char family_of_key[6] = { PLATE_SLOT1, PLATE_SLOT3,
                                                        PLATE_SLOT2, PLATE_SLOT4,
                                                        PLATE_SLOT5, PLATE_SLOT6 };
        static int told[2];
        if (k >= 0 && k < 6) {
            if (!told[player & 1]) {
                told[player & 1] = 1;
                jarkins_log("jarkins: P%u picked %u with key %d, plate family %u\n",
                            player + 1, id, k, family_of_key[k]);
            }
            return family_of_key[k];
        }
    }
    /* A fighter the game chose for itself - the CPU, a guest, a challenge - has no
     * key, so its plate reads by the costume it is wearing: 1 red, 3 blue, 5
     * orange, the only three the game's own choice can show. */
    switch(psx_mod_read_half(0x800add98+player*2)&3) {
        case 0: return PLATE_SLOT1;     /* costume 1, red */
        case 1: return PLATE_SLOT3;     /* costume 3, blue */
        default: return PLATE_SLOT5;    /* costume 5, orange */
    }
}
/* A piece of a name plate: in the plate's band (y 60..84) and reading one of the
 * six plate palettes (7F10..7F15). *player is the side its x puts it on, *strip
 * which of the two palettes it wants: op 0x65 is the bar, 0x2D the strip below. */
static int plate_prim_at(uint32_t at,unsigned *player,unsigned *strip) {
    uint32_t xy=psx_mod_read_word(at+4);
    unsigned y=(xy>>16)&0xFF,x=xy&0xFFFF;
    if(y<60||y>84)return 0;
    unsigned clut=psx_mod_read_word(at+8)>>16;
    if(clut<0x7F10||clut>0x7F15)return 0;
    *strip=(psx_mod_read_word(at)>>24)!=0x65;
    *player=x<128?0:1;
    return 1;
}
/* Every prim the game has been seen to build: the HUD rebuilds its packets each
 * frame from its own source, so a prim patched after the frame was submitted is
 * gone by the next build. Patching again at the HUD routine's entry puts the
 * colour in before that build, which is why both hooks exist - the entry one is
 * the one that lands on screen. */
static void plate_patch_prims(void) {
    for(unsigned i=0;i<PLATE_SITES;i++) {
        unsigned side,strip;
        if(!plate_site[i]||!plate_prim_at(plate_site[i],&side,&strip))continue;
        unsigned slot=strip?strip_slot[side]:plate_slot[side];
        uint32_t old=psx_mod_read_word(plate_site[i]+8);
        psx_mod_write_word(plate_site[i]+8,(slot<<16)|(old&0xFFFF));
    }
}
static void plate_remember(uint32_t at,unsigned side) {
    for(unsigned i=0;i<PLATE_SITES;i++)
        if(plate_site[i]==at) { plate_side[i]=side; return; }
    for(unsigned i=0;i<PLATE_SITES;i++)
        if(!plate_site[i]) {
            plate_site[i]=at; plate_side[i]=side;
            fprintf(stderr,"Jarkins plates: P%u prim at %08X, family %u, clut %04X\n",
                    side+1,at,plate_family(side),plate_slot[side]);
            return;
        }
    plate_site[plate_next]=at; plate_side[plate_next]=side;
    plate_next=(plate_next+1)%PLATE_SITES;
}
/* The HUD routine's entry (0x8003D6B0): the game is about to lay out and submit
 * its HUD, so the plates get our colour before it does. */
static void plate_hud_entered(struct CPUState* cpu,uint32_t address) {
    (void)cpu; (void)address;
    if(!psx_mod_game_started())return;
    plate_patch_prims();
}
static void plate_colour_tick(void) {
    if(!psx_mod_game_started())return;
    static int announced;
    if(!announced) {
        announced=1;
        fprintf(stderr,"Jarkins plates: on, P1 family %u, P2 family %u\n",plate_family(0),plate_family(1));
    }
    unsigned family[2]={plate_family(0),plate_family(1)};
    plate_build_strips();
    for(unsigned p=0;p<2;p++) {
        gr_vram_transfer_in(256+p*PLATE_PALETTE_STEP,PLATE_PALETTE_Y,16,1,plate_bar[family[p]]);
        gr_vram_transfer_in(272+p*PLATE_PALETTE_STEP,PLATE_PALETTE_Y,16,1,strip_tables[family[p]]);
    }
    plate_patch_prims();
    /* A slice of the packet area a frame: two players, two prims, and the sweep
     * finds both within a few frames of a match starting. */
    for(unsigned i=0;i<PLATE_SWEEP;i++) {
        unsigned side,strip;
        uint32_t at=plate_sweep;
        plate_sweep+=4;
        if(plate_sweep>=PLATE_SCAN_TO)plate_sweep=PLATE_SCAN_FROM;
        if(!plate_prim_at(at,&side,&strip))continue;
        plate_remember(at,side);
    }
    plate_patch_prims();
}

/* Registered under the id the plates package names, so the launcher finds the
 * implementation it lists. */
PSX_MOD_CONSTRUCTOR(jarkins_register_plates) {
    (void)psx_mod_register_vblank_plugin("tekken3.plate-colours",plate_colour_tick);
    (void)psx_mod_register_function_entry_plugin("tekken3.plate-colours",0x8003D6B0u,plate_hud_entered);
}
