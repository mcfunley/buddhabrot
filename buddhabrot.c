#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>
#include "tiffio.h"


#define ITERATIONS 20000
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

    // The mean value in the plot array, for values not in the mandelbrot set.  
    long long mean;

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
    // Most (say, 99%) of the points are going to fall below 0.15*max. So 
    // there needs to be a significant amount of color variation in that
    // range. 
    if(count == 0) {
        return 0;
    }

    double twentieth = (double)b->max / 20, c = (double)count;
    double a;
    if(count < twentieth) {
        // counts between 0-5% of max: half through full blue
        a = c / twentieth / 2;
        return rgb(0, 0, 0.5+a);
    }
    if(count < twentieth*2) {
        // counts between 5-10% of max: full blue through full purple
        a = (c - twentieth) / twentieth;
        return rgb(a, 0, 1);
    }
    if(count < twentieth*3) {
        // counts between 10-15% of max: full purple through full red
        a = (c - twentieth*2) / twentieth;
        return rgb(1, 0, 1-a);
    }
    if(count < twentieth*10) {
        // counts between 15-50% of max: full red through full yellow
        a = (c - twentieth*3) / (twentieth*7);
        return rgb(1, a, 0);
    }
    // counts between 50-100% of max: full yellow through white
    a = (c - twentieth*10) / (twentieth*10);
    return rgb(1, 1, a);
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
 * using buddha_plot_callback. This also computes and sets the structure's
 * mean field. 
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

    // compute the mean field in the plot array. 
    int sum = 0, i = 0, n = 0;
    for(i = 0; i <= b->max_offs; i++) {
        if(b->plot[i]) {
            sum += b->plot[i];
            n++;
        }
    }
    b->mean = sum / n;
}


/**
 * Prints out overall stats and a text histogram of the plot counts. 
 */
void buddha_print_stats(buddha* b) {
    printf("Iterations: %d\n", b->iterations);
    printf("Dimensions: %dx%dpx\n", b->width, b->height);
    printf("Mean count: %lld\n", b->mean);
    printf("Max count: %lld\n", b->max);

    int ranges[20] = {0};
    double twentieth = (double)b->max / 20;
    int i = 0, n = 0;
    for(; i <= b->max_offs; i++) {
        int c = b->plot[i];
        if(c != 0) {
            int j = 1;
            for(; j <= 20; j++) {
                if(c < twentieth*j) {
                    break;
                }
            }

            n++;
            ranges[j-1]++;
        }
    }

    double pct_escaped = (double)n / b->max_offs * 100;
    printf("Escaping points: %d (%.2f%%)\n", n, pct_escaped);

    printf("\n");
    float cum_pct = 0;
    for(i = 0; i< 20; i++) {
        int low = twentieth*i;
        int hi = twentieth*(i+1);
        int c = ranges[i];
        float pct = (float)c / n * 100;
        cum_pct += pct;
        printf("%2d %4d   - %4d %15d  %3.2f  %3.2f\n", 
               i+1, low, hi, c, pct, cum_pct);
    }
    printf("\n");
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
    buddha_print_stats(&b);
    
    write_tiff(b.im);
    buddha_free(&b);
    return 0;
}
