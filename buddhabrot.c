#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>
#include "tiffio.h"


#define ITERATIONS 4000
#define SCALE 2
#define WIDTH 1440 * SCALE
#define HEIGHT 900 * SCALE


#define RED(x) ((x & 0x00ff0000) >> 16)
#define GREEN(x) ((x & 0x0000ff00) >> 8)
#define BLUE(x) (x & 0x000000ff)


typedef struct _bb {
    char* escapes;
    int* plot;
    char* im;
    long long max;
    int width;
    int height;
    int iterations;
    int max_offs;
    int nebula;
} buddha;


void buddha_init(buddha* b, int width, int height, int iterations, int nebula) {
    b->escapes = (char*)malloc(sizeof(char) * width * height);
    b->plot = (int*)malloc(sizeof(int) * width * height);
    b->im = (char*)malloc(sizeof(char) * width * height * 3);
    b->max = 0;
    b->width = width;
    b->height = height;
    b->iterations = iterations;
    b->max_offs = width * height - 1;
    b->nebula = nebula;
}


void buddha_free(buddha* b) {
    free(b->escapes);
    free(b->plot);
    free(b->im);
}


void err(int code, char* msg) {
    fprintf(stderr, msg);
    fprintf(stderr, "\n");
    exit(code);
}


int rgb(double r, double g, double b) {
    return ((int)(r * 255) << 16) +
        ((int)(g * 255) << 8) + 
        ((int)(b * 255));
}


int getcolor(buddha* b, int count) {
    double a = (double)count / b->max;
    double a2 = a*a, a3 = a2*a;
    return rgb(a3, a3, a);
}


complex double px2cx(buddha* b, int x, int y) {
    return ((3.0 / b->width * (double)x) - 2) + 
        ((2.0 / b->height * (double)y) - 1) * I;
}


void cx2px(buddha* b, complex double z, int* x, int* y) {
    *x = (int)((creal(z) + 2) * b->width / 3);
    *y = (int)((cimag(z) + 1) * b->height / 2);
}


int iterate(buddha* b, int x, int y, void (*cb)(buddha*, complex double)) {
    complex double z = 0, c = px2cx(b, x, y);
    int i = 1;
    for(; i < b->iterations; i++) {
        z = z*z + c;
        if(cabs(z) >= 2) {
            break;
        }
        if(cb != NULL) {
            cb(b, z);
        }
    }
    return i;
}


void putpixel(buddha* b, int c, int x, int y) {
    int offs = y * b->width * 3 + x * 3;
    b->im[offs] = RED(c);
    b->im[offs+1] = GREEN(c);
    b->im[offs+2] = BLUE(c);
}


void buddha_calc_escapes(buddha* b) {
    int x, y;
    for(x = 0; x < b->width; x++) {
        for(y = 0; y < b->height; y++) {
            int offs = y * b->width + x;
            int its = iterate(b, x, y, NULL);
            if(its != ITERATIONS) {
                b->escapes[offs] = 1;
            } else {
                b->escapes[offs] = 0;
            }
        }
    }
}


void buddha_plot_callback(buddha* b, complex double z) {
    int x, y;
    cx2px(b, z, &x, &y);
    
    int offs = y * b->width + x;
    if(offs < 0 || offs > b->max_offs) {
        return;
    }

    b->plot[offs]++;
    
    if(b->plot[offs] > b->max) {
        b->max = b->plot[offs];
    }
}


void buddha_plot_escapes(buddha* b) {
    int x, y;
    for(x = 0; x < b->width; x++) {
        for(y = 0; y < b->height; y++) {
            int offs = y * b->width + x;
            if(b->escapes[offs] == 1) {
                iterate(b, x, y, &buddha_plot_callback);
            }
        }
    }
}


void buddha_draw(buddha* b) {
    int x, y;
    for(x = 0; x < b->width; x++) {
        for(y = 0; y < b->height; y++) {
            int offs = y * b->width + x;
            int count = b->plot[offs];
            int c = getcolor(b, count);
            putpixel(b, c, x, y);
        }
    }
}


void buddha_calculate(buddha* b) {
    buddha_calc_escapes(b);
    buddha_plot_escapes(b);
    buddha_draw(b);
}



void write_tiff(char* raster) {
    TIFF* im = TIFFOpen("mandelbrot.tiff", "w");
    if(im == NULL) {
        err(2, "Could not open output TIFF.");
    }
    
    TIFFSetField(im, TIFFTAG_IMAGEWIDTH, WIDTH);
    TIFFSetField(im, TIFFTAG_IMAGELENGTH, HEIGHT);
    TIFFSetField(im, TIFFTAG_COMPRESSION, COMPRESSION_DEFLATE);
    TIFFSetField(im, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(im, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(im, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(im, TIFFTAG_SAMPLESPERPIXEL, 3);

    if(TIFFWriteEncodedStrip(im, 0, raster, WIDTH*HEIGHT*3) == 0) {
        err(3, "Error writing TIFF.");
    }

    TIFFClose(im);
}


int main() {
    buddha b;
    buddha_init(&b, WIDTH, HEIGHT, ITERATIONS, 0);

    buddha_calculate(&b);
    
    write_tiff(b.im);
    buddha_free(&b);
    return 0;
}
