    /*
     *  Monochrome (mfb)
     */

extern struct display_switch fbcon_mfb;
extern void fbcon_mfb_setup(struct display *p);
extern void fbcon_mfb_bmove(struct display *p, int sy, int sx, int dy, int dx,
			    int height, int width);
extern void fbcon_mfb_clear(struct vc_data *conp, struct display *p, int sy,
			    int sx, int height, int width);
extern void fbcon_mfb_putc(struct vc_data *conp, struct display *p, int c,
			   int yy, int xx);
extern void fbcon_mfb_putcs(struct vc_data *conp, struct display *p,
			    const char *s, int count, int yy, int xx);
extern void fbcon_mfb_revc(struct display *p, int xx, int yy);