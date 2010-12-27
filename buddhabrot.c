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


/**
 * Struct that maintains context for the fractal during a rendering run. 
 */
typedef struct _bb {
    // Map of points that escape (ie those not in the Mandelbrot set). 
    char* escapes;

    // Each element here is a counter, incremented when a point that escapes
    // assumes its value during iteration. 
    int* plot;

    // The final raster image (RGB). 
    char* im;

    // The maximal value in the plot array. 
    long long max;

    int width;
    int height;
    int iterations;
    int max_offs;
    int nebula;
} buddha;


/**
 * Initializes a buddha struct with the given options. 
 */
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


/**
 * Frees the memory allocated using buddha_init. 
 */
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


/**
 * Converts double values (between 0 and 1) into an RGB value. 
 */
int rgb(double r, double g, double b) {
    return ((int)(r * 255) << 16) +
        ((int)(g * 255) << 8) + 
        ((int)(b * 255));
}


/**
 * Gets the color to plot given a counter value. 
 */
int getcolor(buddha* b, int count) {
    double a = (double)count / b->max;
    double a2 = a*a, a3 = a2*a;
    return rgb(a3, a3, a);
}


/**
 * Converts pixel coordinates into complex plane coordinates. 
 */
complex double px2cx(buddha* b, int x, int y) {
    return ((3.0 / b->width * (double)x) - 2) + 
        ((2.0 / b->height * (double)y) - 1) * I;
}


/**
 * Converts complex plane coordinates into pixel coordinates.
 */
void cx2px(buddha* b, complex double z, int* x, int* y) {
    *x = (int)((creal(z) + 2) * b->width / 3);
    *y = (int)((cimag(z) + 1) * b->height / 2);
}


/**
 * Iterates at the given pixel coordinates up to the maximum number of 
 * iterations, or until the point escapes (meaning it is known to not be
 * in the Mandelbrot set). 
 *
 * Optionally, invokes a callback with every iteration, giving the buddha
 * structure along with the current value. 
 *
 * Returns the number of iterations performed, which is either b->iterations
 * if the point is in the Mandelbrot set, or a smaller number otherwise. 
 */
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


/**
 * Plots a pixel in the output image given a coordinate and its count. 
 */
void putpixel(buddha* b, int c, int x, int y) {
    int offs = y * b->width * 3 + x * 3;
    b->im[offs] = RED(c);
    b->im[offs+1] = GREEN(c);
    b->im[offs+2] = BLUE(c);
}


/**
 * Performs the first pass of rendering the fractal. This computes which points 
 * in the image are not in the Mandelbrot set. 
 */
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


/**
 * Called with each iteration while plotting the points that escape. 
 * This increments the appropriate counter for the complex point. It 
 * also keeps track of the maximum counter. 
 */
void buddha_plot_callback(buddha* b, complex double z) {
    int x, y;
    cx2px(b, z, &x, &y);
    
    // Note that it's perfectly acceptable for z to stray outside of 
    // the image bounds. 
    int offs = y * b->width + x;
    if(offs < 0 || offs > b->max_offs) {
        return;
    }

    b->plot[offs]++;
    
    if(b->plot[offs] > b->max) {
        b->max = b->plot[offs];
    }
}


/**
 * Performs a second iteration for each point in the image that is not 
 * in the Mandelbrot set. At each iteration the value of z is counted
 * using buddha_plot_callback. 
 */
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


/**
 * Renders the final image. Used after the escaping values have been
 * found and plotted. 
 */
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


/**
 * Computes and renders the buddhabrot image. 
 */
void buddha_calculate(buddha* b) {
    buddha_calc_escapes(b);
    buddha_plot_escapes(b);
    buddha_draw(b);
}


/**
 * Saves a raw raster image as a TIFF. 
 */
void write_tiff(char* raster) {
    TIFF* im = TIFFOpen("buddhabrot.tiff", "w");
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
