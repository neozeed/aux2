#ifndef lint	/* .../sys/psn/io/mk_feature.c */
#define _AC_NAME mk_feature_c
#define _AC_MAIN "@(#) Copyright (c) 1987 Apple Computer Inc., All Rights Reserved.  {Apple version 1.6 89/10/23 16:56:29}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.6 of mk_feature.c on 89/10/23 16:56:29";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)mk_feature.c	UniPlus VVV.2.1.1	*/
extern screen_image[];
extern screen2_image[];
extern screen3_image[];
int width;

main()
{
	width = 512;
	extract(screen_image, "screen_feature1", 0, 0, 8, 22);
	extract(screen_image, "screen_feature2", 512-8, 0, 8, 22);
	extract(screen_image, "screen_feature3", 8, 0, 8, 22);
	extract(screen_image, "screen_feature4", 224, 6, 64, 12);
	extract(screen_image, "screen_feature5", 0, 22, 8, 2);
	extract(screen_image, "screen_feature6", 512-8, 22, 8, 2);
	extract(screen_image, "screen_feature7", 0, 342-6, 8, 6);
	extract(screen_image, "screen_feature8", 8, 342-6, 8, 6);
	extract(screen_image, "screen_feature9", 512-8, 342-6, 8, 6);

	width = 512;
	extract(screen2_image, "screen2_feature1",  40,  56, 424,  1);
	extract(screen2_image, "screen2_feature2",  40,  58, 424,  1);
	extract(screen2_image, "screen2_feature3",  40,  60, 424,  1);
	extract(screen2_image, "screen2_feature4",  64,  73,  32, 31);
	extract(screen2_image, "screen2_feature5", 112,  86, 320, 12);

/* Extract features from the bitmap for the "A/UX Launching .. " bitmap.	-ABS		*/
	width = 512;
	extract(screen3_image, "screen3_feature1", 0, 0, 8, 5); 	/* Top Left Corner 	*/
	extract(screen3_image, "screen3_feature2", 512-8, 0, 8, 5);	/* Top Rt. Corner.	*/
	extract(screen3_image, "screen3_feature3", 8,3,32,11);		/* Apple logo.		*/
	extract(screen3_image, "screen3_feature4", 0, 342-6, 8, 6);	/* Bottom Left Corner.	*/
	extract(screen3_image, "screen3_feature5", 512-8, 342-6, 8, 6); /* Bottom Rt. Corner.	*/
	extract(screen3_image, "screen3_feature6", 0, 19, 8, 1);	/* Line.		*/
	extract(screen3_image, "screen3_feature7", 96, 0, 224, 2);	/* White bit pattern.	*/
	extract(screen3_image, "screen3_feature8", 32, 64, 8, 126);	/* Left edge of box.	*/
	extract(screen3_image, "screen3_feature9", 472, 64, 8, 126);    /* Rt. edge of box.	*/
	extract(screen3_image, "screen3_feature10", 56, 89, 32, 32);	/* A/UX-Mac diagram.	*/
	extract(screen3_image, "screen3_feature11", 192, 98, 128, 9);	/* Welcome to A/UX.	*/
	extract(screen3_image, "screen3_feature12", 208, 114, 88, 12);  /* Launching.		*/
	extract(screen3_image, "screen3_feature13", 72, 146, 384, 20);  /* Progress bar/Cancel.	*/

	return(0);
}


extract(d, name, x, y, len, height)
char *d, *name;
{
	unsigned char *lp;
	int i, j, inc;

	printf("\nunsigned char %s[] = {\n",name);
	inc = 0;
	for (i = 0; i < height; i++) {
		lp = (unsigned char *)(d + (y*width/8) + (x/8));
		for (j = 0; j < len; j+=(8*sizeof(unsigned char))) {
			printf("0x%x, ",*lp++);
			if (inc++ > 8) {
				inc = 0;
				printf("\n");
			}
		}
		y++;
	}
	printf("\n};\n");
}
