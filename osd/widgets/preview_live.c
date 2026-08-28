// End-to-end: build a glyph grid exactly as an FC would send it, run the real
// recogniser over it, then draw the result with the real widget renderer.
#include "osd/elements/osd_elements.h"
#include "osd/widgets/osd_widgets.h"
#include "libpng/lodepng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLS 53
#define ROWS 20
#define CELL_W 36
#define CELL_H 54
#define W (COLS*CELL_W)
#define H 860
static uint16_t grid[ROWS][COLS];
static uint16_t getter(int c,int r,void*x){(void)x;
    if(c<0||c>=COLS||r<0||r>=ROWS)return 0; return grid[r][c];}
static void put(int r,int c,const uint16_t*g,int n){for(int i=0;i<n;i++)grid[r][c+i]=g[i];}

int main(void){
    memset(grid,0x20,sizeof(grid));
    // exactly how INAV encodes these, half-dot pairs included
    { uint16_t g[]={(uint16_t)(0xA1+3),(uint16_t)(0xB1+5),'9',0x1F}; put(1,1,g,4); }  // 3.59V cell
    { uint16_t g[]={'2','0',0x6A};                                   put(5,1,g,3); }  // 20A
    { uint16_t g[]={'1','3','9',0x76};                               put(9,1,g,4); }  // 139m
    { uint16_t g[]={'9','2',0x01};                                   put(13,1,g,3); } // rssi -- leading sym
    { uint16_t g[]={0x01,'9','2'};                                   put(13,1,g,3); }

    osd_element_t els[32];
    int n = osd_elements_scan(getter,NULL,COLS,ROWS,"INAV",els,32);
    printf("recognised %d elements:\n", n);
    for(int i=0;i<n;i++) printf("  %-9s r%02d c%02d = %.2f%s\n",
        osd_element_type_name(els[i].type), els[i].row, els[i].col, els[i].value,
        els[i].is_per_cell?" (per-cell)":"");

    osd_theme_t th; osd_theme_defaults(&th);
    if(!osd_theme_load(&th,"themes/tactical/theme.ini")) printf("!! theme load failed\n");
    osd_font_t *font = osd_font_load(th.font_path);
    if(!font){printf("!! font load failed: %s\n",th.font_path);return 1;}

    uint8_t *buf = calloc((size_t)W*H*4,1);
    osd_surface_t s; osd_surface_init(&s,buf,W,H,W*4);

    osd_widget_state_t st; osd_widgets_state_init(&st);
    osd_widgets_update_arm(&st,true);
    st.current_peak = 67.0f;                    // as if we punched out earlier

    // show the size slider: same voltage element at 0.7x, 1.0x, 1.4x
    th.elem_scale[OSD_ELEM_VOLTAGE] = 0.7f;
    th.elem_scale[OSD_ELEM_CURRENT] = 1.0f;
    th.elem_scale[OSD_ELEM_ALTITUDE] = 1.4f;
    th.elem_scale[OSD_ELEM_RSSI] = 1.0f;
    osd_grid_t g = {CELL_W,CELL_H,8,0};
    int drawn = osd_widgets_draw_all(&s,&th,font,&st,els,n,&g);
    printf("drew %d widgets (peak %.0fA)\n", drawn, st.current_peak);

    // Composite the (transparent) overlay over a stand-in video frame, which is
    // what the hardware does, so cut-outs show the scene rather than white.
    uint8_t *rgba=malloc((size_t)W*H*4);
    for(size_t i=0;i<(size_t)W*H;i++){
        int x=(int)(i%W), y=(int)(i/W);
        int vb=0x14+((x/64+y/64)%2)*0x0A, vg=0x18+((y/160)%3)*0x06, vr=0x10;
        float a=buf[i*4+3]/255.0f;
        rgba[i*4+0]=(uint8_t)(buf[i*4+2]*a+vr*(1-a));
        rgba[i*4+1]=(uint8_t)(buf[i*4+1]*a+vg*(1-a));
        rgba[i*4+2]=(uint8_t)(buf[i*4+0]*a+vb*(1-a));
        rgba[i*4+3]=255;}
    printf(lodepng_encode32_file("widget-live.png",rgba,W,H)?"png err\n":"wrote widget-live.png\n");
    return 0;
}
