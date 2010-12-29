#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <math.h>
#include "tiffio.h"


#define ITERATIONS 40000
#define SCALE 4
#define WIDTH 1440 * SCALE
#define HEIGHT 900 * SCALE


#define RED(x) ((x & 0x00ff0000) >> 16)
#define GREEN(x) ((x & 0x0000ff00) >> 8)
#define BLUE(x) (x & 0x000000ff)


/**
 * Struct that maintains context for the plot during a rendering run. 
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
    int max;

    // Contains an entry for each count up to max, and stores the number of 
    // times that count appears. This information is important in choosing 
    // color ranges. 
    // 
    // (The max increases with iterations, but it tends to stay under a few 
    // thousand even up to high numbers.)
    int* count_frequency; 

    // The number of points in the image that escaped. 
    int num_escaped;

    // Divides the count space into percentiles. 10% of counts are below 
    // percentile_limit[0], 20% of counts are below percentile_limit[1], 
    // and so on. 
    int percentile_limit[10];
    
    // The mean value in the plot array, for values not in the mandelbrot set.  
    int mean;

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

    // This will be allocated later when we know what the max is. 
    b->count_frequency = NULL;
}


/**
 * Frees the memory allocated using buddha_init. 
 */
void buddha_free(buddha* b) {
    free(b->escapes);
    free(b->plot);
    free(b->im);

    if(b->count_frequency) {
        free(b->count_frequency);
    }
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


int rank_in_percentile(buddha* b, int lo, int hi, int c) {
    double cl = b->percentile_limit[lo], 
        ch = b->percentile_limit[hi];
    return ((double)c - cl) / (ch - cl);
}


/**
 * Gets the color to plot given a counter value. 
 */
int getcolor(buddha* b, int count) {
    // Points not visited are black. 
    if(count == 0) {
        return 0;
    }

    // Almost all of the points are going to have relatively low counts. If we 
    // just color the image with a simple range based on the count, it will get
    // darker and darker with more iterations. So we have to apply the 
    // colors where the variation actually exists, and adjust things as 
    // different dimensions and iteration settings produce different results.
    double a;

    // bottom 20% of counts are blue
    if(count <= b->percentile_limit[1]) {
        a = rank_in_percentile(b, 0, 1, count);
        return rgb(0, 0, a);
    }

    // 20th through 30th percentiles are between blue and purple
    if(count <= b->percentile_limit[2]) {
        a = rank_in_percentile(b, 1, 2, count);
        return rgb(a, 0, 1);
    }

    // 30th through 50th percentiles are between purple and red
    if(count <= b->percentile_limit[4]) {
        a = rank_in_percentile(b, 2, 4, count);
        return rgb(1, 0, 1-a);
    }

    // 50th through 60th percentiles are between red and yellow
    if(count <= b->percentile_limit[5]) {
        a = rank_in_percentile(b, 4, 5, count);
        return rgb(1, a, 0);
    }

    // 60th through 70th percentiles are between yellow and green
    if(count <= b->percentile_limit[6]) {
        a = rank_in_percentile(b, 5, 6, count);
        return rgb(1-a, 1, 0);
    }

    // 70th through 80th percentiles are between green and cyan
    if(count < b->percentile_limit[7]) {
        a = rank_in_percentile(b, 6, 7, count);
        return rgb(0, 1, a);
    }

    // 80th through 100th percentiles are between cyan and white
    a = rank_in_percentile(b, 7, 9, count);
    return rgb(a, 1, 1);
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
 * Performs the first pass of rendering. This computes which points 
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
}


/**
 * Prints out overall stats and a text histogram of the plot counts. 
 */
void buddha_print_stats(buddha* b) {
    printf("Iterations: %d\n", b->iterations);
    printf("Dimensions: %dx%dpx\n", b->width, b->height);
    printf("Mean count: %d\n", b->mean);
    printf("Max count: %d\n", b->max);

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

    printf("\nHistogram:\n");
    float cum_pct = 0;
    for(i = 0; i < 20; i++) {
        int low = twentieth*i;
        int hi = twentieth*(i+1);
        int c = ranges[i];
        float pct = (float)c / n * 100;
        cum_pct += pct;
        printf("%2d %4d   - %4d %15d  %3.2f  %3.2f\n", 
               i+1, low, hi, c, pct, cum_pct);
    }

    printf("\nPercentile limits:\n");
    for(i = 0; i < 10; i++) {
        printf("%2d%%  %d\n", (i+1)*10, b->percentile_limit[i]);
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
 * Walks through the plot, calculating the mean value and keeping track 
 * of how often each count appears. 
 *
 * This allocates the count_frequency field. 
 */
void buddha_compute_stats(buddha* b) {
    int i = 0, sum = 0, n = 0;
    b->count_frequency = (int*)malloc(sizeof(int) * b->max);
    for(; i <= b->max_offs; i++) {
        int c = b->plot[i];
        if(c) {
            b->count_frequency[c]++;
            n++;
            sum += c;
        }
    }
    b->mean = (double)sum / n;
    b->num_escaped = n;

    // Calculate the maximal count in for each tenth percentile.
    double d = (double)n / 10, lim = d;
    int cum_freq = 0, p = 0;
    for(i = 0; i < b->max; i++) {
        cum_freq += b->count_frequency[i];
        if(cum_freq > lim) {
            b->percentile_limit[p++] = i;
            lim += d;
        }
        if(p == 10) {
            break;
        }
    }

    // hardcode the 100th percentile to be the max
    b->percentile_limit[9] = b->max;
}


/**
 * Computes and renders the buddhabrot image. 
 */
void buddha_calculate(buddha* b) {
    buddha_calc_escapes(b);
    buddha_plot_escapes(b);
    buddha_compute_stats(b);
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
