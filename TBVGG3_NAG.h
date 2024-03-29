/*
--------------------------------------------------
    James William Fletcher (james@voxdsp.com)
        JANUARY 2021 - TBVGG3_NAG
--------------------------------------------------
    Tiny Binary VGG3
    https://github.com/tfcnn

    This is an adaption inspired by the VGG series of networks.

    This VGG network is designed for binary classification and is
    only three layers deep. It uses Global Average Pooling rather
    than a final fully connected layer, additionally the final
    result is again just an average of the GAP. Essentially making
    this network an FCN version of the VGG network.

    The VGG network was originally created by the Visual Geometry Group
    of Oxford University in the United Kingdom. It was first proposed
    by Karen Simonyan and Andrew Zisserman, the original paper is
    available here; https://arxiv.org/abs/1409.1556

        TBVGG3
        :: ReLU + 0 Padding
        28x28 x32
        > maxpool
        14x14 x64
        > maxpool
        7x7 x128
        > GAP + Average

    I like to call the gradient the error, I am one of those.

    Configuration;
        No batching of the forward passes before backproping the error,
        my argument is that this is a network designed to be light-weight
        and to be used for real-time training.

        Nesterov optimisation of gradient descent, although, I think
        random step size SGD may better fit the reverse/forward
        training model.

        XAVIER GLOROT normal distribution weight initialisation.
        I read some places online that uniform GLOROT works
        better in CNN's, this is something I really need to
        benchmark for myself at some point. Since the original
        VGG paper references GLOROT with normal distribution,
        this is what I chose initially.

        expected input RGB 28x28 pixels;
        float input[3][28][28];

    Preferences;
        You can see that I do not make an active effort to avoid
        branching, when I consider the trade off, such as with the
        TBVGG3_CheckPadded() check, I think to myself do I memcpy()
        to a new buffer with padding or include the padding in the
        original buffer or use branches to check if entering a padded
        coordinate, I chose the latter. I would rather a few extra
        branches than to bloat memory in some scenarios, although
        you can also see in TBVGG3_2x2MaxPool() that I choose a
        negligibly higher use of memory to avoid ALU divisions.

        I didn't think it was a good idea to maxpool the last
        layer because there are no fully connected layers,
        since it's going straight into a GAP it will make
        negligible difference in the final average. Maxpooling
        before a fully connected layer makes sense to reduce the
        amount of parameters to a more important subset. But this
        is a binary decision network, so a fully connected layer
        wont have a profound impact, we just want to know if our
        relevant features / filters had been activated enough to
        signal YES, if not, it's a NO.

    Comments;
        Do I think Bias makes a significant difference? It certainly
        seems to make the network train faster, it is extra parameters
        and 'hassle' to add to a network, it did make me think a little
        as to how best it would be implemented.

        When it came to the back propagation I just worked it out
        using the knowledge and intuition I had gained from implementing
        back propagation in Fully Connected Neural Networks which is a
        in my opinion easier to understand. That's to say I didn't read
        or check any existing documentation for implementing back prop
        in CNN's. To be honest, the problem is something you can just
        see in your minds eye when you think about it. You know that
        you have to push a gradient backward and that process is very
        much the same as in Fully Connected layers.

        It certainly feels like this CNN trains better on image data
        than an FCNN although I do not feel that it is particularly
        easy to train in the real-time manner that I originally proposed
        here, I am going to put some more thought into improving the
        real-time training process but for this existing system
        the only option to improve the training is to variate to order
        of the objects you train on in real-time as if you where feeding
        an neural network for offline training and to fiddle the
        `LEARNING_RATE`, `GAIN`, and, `NAG_MOMENTUM` hyperparameters.
        
        When a ReLU output is fed into a regular Sigmoid function the
        output of the ReLU will always be >0 and thus the output of the
        Sigmoid will always be 0.5 - 1.0, and the Derivative will start
        at 0.25 and then reduce to 0 as the sigmoid input approaches 1.
        As such I have provided a suggested modification to the Sigmoid
        function `1-(1 / exp(x))` which will insure that the output ranges
        from 0 to 1 and that the derivative will output 0.25 with an input
        of 0.5.

    Network size:
        (3x28x28)+(32x3x9)+(64x32x9)+(128x64x9)+(32x3x9)+(64x32x9)+
        (128x64x9)+(32x28x28)+(32x14x14)+(64x14x14)+(64x7x7)+
        (128x7x7)+(32x28x28)+(64x14x14)+(128x7x7)+(32+64+128)+
        (32+64+128)+(32+64+128) = 286,288 floats
        286288*4 = 1,145,152 bytes = 1.092102051 megabytes
*/

#ifndef TBVGG3_H
#define TBVGG3_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>

#define uint unsigned int
#define sint int
#define LEARNING_RATE 0.01
#define GAIN          0.0065
#define NAG_MOMENTUM  0.1

/*
--------------------------------------
    structures
--------------------------------------
*/

// network struct
struct
{
    //filters:num, d,  w+b
    float l1f[32 ][3 ][9];
    float l2f[64 ][32][9];
    float l3f[128][64][9];

    // filter momentum's
    float l1fm[32 ][3 ][9];
    float l2fm[64 ][32][9];
    float l3fm[128][64][9];

    //~~ bias
    // filter bias's
    float l1fb[32 ][1];
    float l2fb[64 ][1];
    float l3fb[128][1];

    // filter bias momentum's
    float l1fbm[32 ][1];
    float l2fbm[64 ][1];
    float l3fbm[128][1];

    // filter bias's
    float l1fbe[32 ][1];
    float l2fbe[64 ][1];
    float l3fbe[128][1];
    //~~ bias

    // outputs
    //       d,  y,  x
    float o1[32][28][28];
        float p1[32][14][14]; // pooled
    float o2[64][14][14];
        float p2[64][7][7];   // pooled
    float o3[128][7][7];

    // error gradients
    //       d,  y,  x
    float e1[32][28][28];
    float e2[64][14][14];
    float e3[128][7][7];
}
typedef TBVGG3_Network;

enum 
{
    LEARN_MAX = 1,
    LEARN_MIN = 0,
    NO_LEARN  = -1
}
typedef TBVGG3_LEARNTYPE;

/*
--------------------------------------
    functions
--------------------------------------
*/

float TBVGG3_Process(TBVGG3_Network* net, const float input[3][28][28], const TBVGG3_LEARNTYPE learn);
void  TBVGG3_Reset(TBVGG3_Network* net);
int   TBVGG3_SaveNetwork(TBVGG3_Network* net, const char* file);
int   TBVGG3_LoadNetwork(TBVGG3_Network* net, const char* file);
void  TBVGG3_Dump(TBVGG3_Network* net, const char* file);

/*
--------------------------------------
    the code ...
--------------------------------------
*/

void TBVGG3_Dump(TBVGG3_Network* net, const char* file)
{
    char p[256];
    mkdir(file, 0777);
    sprintf(p, "%s/l1f.txt", file);
    FILE* f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 32; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 3; j++){
                fprintf(f, "D(%u): ", j);
                for(uint k = 0; k < 9; k++)
                    fprintf(f, "%.2f ", net->l1f[i][j][k]);
                fprintf(f, ":: %f\n", net->l1fb[i][0]);
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/l2f.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 64; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 32; j++){
                fprintf(f, "D(%u): ", j);
                for(uint k = 0; k < 9; k++)
                    fprintf(f, "%.2f ", net->l2f[i][j][k]);
                fprintf(f, ":: %f\n", net->l2fb[i][0]);
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/l3f.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 128; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 64; j++){
                fprintf(f, "D(%u): ", j);
                for(uint k = 0; k < 9; k++)
                    fprintf(f, "%.2f ", net->l3f[i][j][k]);
                fprintf(f, ":: %f\n", net->l3fb[i][0]);
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/o1.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 32; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 28; j++){
                fprintf(f, "Y(%u): ", j);
                for(uint k = 0; k < 28; k++)
                    fprintf(f, "%.2f ", net->o1[i][j][k]);
                fprintf(f, "\n");
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/p1.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 32; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 14; j++){
                fprintf(f, "Y(%u): ", j);
                for(uint k = 0; k < 14; k++)
                    fprintf(f, "%.2f ", net->p1[i][j][k]);
                fprintf(f, "\n");
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/o2.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 64; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 14; j++){
                fprintf(f, "Y(%u): ", j);
                for(uint k = 0; k < 14; k++)
                    fprintf(f, "%.2f ", net->o2[i][j][k]);
                fprintf(f, "\n");
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/p2.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 64; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 7; j++){
                fprintf(f, "Y(%u): ", j);
                for(uint k = 0; k < 7; k++)
                    fprintf(f, "%.2f ", net->p2[i][j][k]);
                fprintf(f, "\n");
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/o3.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 128; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 7; j++){
                fprintf(f, "Y(%u): ", j);
                for(uint k = 0; k < 7; k++)
                    fprintf(f, "%.2f ", net->o3[i][j][k]);
                fprintf(f, "\n");
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/e1.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 32; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 28; j++){
                fprintf(f, "Y(%u): ", j);
                for(uint k = 0; k < 28; k++)
                    fprintf(f, "%.2f ", net->e1[i][j][k]);
                fprintf(f, "\n");
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/e2.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 64; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 14; j++){
                fprintf(f, "Y(%u): ", j);
                for(uint k = 0; k < 14; k++)
                    fprintf(f, "%.2f ", net->e2[i][j][k]);
                fprintf(f, "\n");
            }fprintf(f, "\n");
        }fclose(f);}
    sprintf(p, "%s/e3.txt", file);
    f = fopen(p, "w");
    if(f != NULL){
        for(uint i = 0; i < 128; i++){
            fprintf(f, "~~~~~~~~~~~~~~N(%u):\n", i);
            for(uint j = 0; j < 7; j++){
                fprintf(f, "Y(%u): ", j);
                for(uint k = 0; k < 7; k++)
                    fprintf(f, "%f ", net->e3[i][j][k]);
                fprintf(f, "\n");
            }fprintf(f, "\n");
        }fclose(f);}
}

static inline float TBVGG3_RELU(const float x)
{
    if(x < 0){return 0;}
    return x;
}

static inline float TBVGG3_RELU_D(const float x)
{
    if(x > 0){return 1;}
    return 0;
}

static inline float TBVGG3_SIGMOID(const float x)
{
    return 1-(1 / exp(x));
    //return 1 / (1 + exp(-x));
}

static inline float TBVGG3_SIGMOID_D(const float x)
{
    return x * (1 - x);
}

static inline float TBVGG3_SGD(const float input, const float error)
{
    return LEARNING_RATE * error * input;
}

static inline float TBVGG3_NAG(const float input, const float error, float* momentum)
{
    const float v = NAG_MOMENTUM * momentum[0] + ( LEARNING_RATE * error * input );
    const float n = v + NAG_MOMENTUM * (v - momentum[0]);
    momentum[0] = v;
    return n;
}

float TBVGG3_NormalRandom() // Box Muller
{
    static const float rmax = (float)RAND_MAX;
    double u = ( (((float)rand())+1e-7) / rmax) * 2 - 1;
    double v = ( (((float)rand())+1e-7) / rmax) * 2 - 1;
    double r = u * u + v * v;
    while(r == 0 || r > 1)
    {
        u = ( (((float)rand())+1e-7) / rmax) * 2 - 1;
        v = ( (((float)rand())+1e-7) / rmax) * 2 - 1;
        r = u * u + v * v;
    }
    return u * sqrt(-2 * log(r) / r);
}

float TBVGG3_UniformRandom()
{
    float pr = 0;
    while(pr == 0) //never return 0
    {
        const float rv2 = ( ( (((float)rand())+1e-7) / (float)RAND_MAX ) * 2 ) - 1;
        pr = roundf(rv2 * 100) / 100; // two decimals of precision
    }
    return pr;
}

void TBVGG3_Reset(TBVGG3_Network* net)
{
    if(net == NULL){return;}

    // seed random
    srand(time(0));

    // XAVIER GLOROT NORMAL
    // Weight Init

    //l1f
    float d = sqrt(2.0 / 35);
    for(uint i = 0; i < 32; i++)
        for(uint j = 0; j < 3; j++)
            for(uint k = 0; k < 9; k++)
                net->l1f[i][j][k] = TBVGG3_NormalRandom() * d;

    //l2f
    d = sqrt(2.0 / 96);
    for(uint i = 0; i < 64; i++)
        for(uint j = 0; j < 32; j++)
            for(uint k = 0; k < 9; k++)
                net->l2f[i][j][k] = TBVGG3_NormalRandom() * d;

    //l3f
    d = sqrt(2.0 / 129);
    for(uint i = 0; i < 128; i++)
        for(uint j = 0; j < 64; j++)
            for(uint k = 0; k < 9; k++)
                net->l3f[i][j][k] = TBVGG3_NormalRandom() * d;
    
    // zero momentum
    memset(net->l1fm, 0, sizeof(net->l1fm));
    memset(net->l2fm, 0, sizeof(net->l2fm));
    memset(net->l3fm, 0, sizeof(net->l3fm));

    // reset bias
    memset(net->l1fb, 0, sizeof(net->l1fb));    // bias
    memset(net->l2fb, 0, sizeof(net->l2fb));
    memset(net->l3fb, 0, sizeof(net->l3fb));
    memset(net->l1fbm, 0, sizeof(net->l1fbm));  // bias momentum
    memset(net->l2fbm, 0, sizeof(net->l2fbm));
    memset(net->l3fbm, 0, sizeof(net->l3fbm));
    memset(net->l1fb, 0, sizeof(net->l1fbe));   // bias error
    memset(net->l2fb, 0, sizeof(net->l2fbe));
    memset(net->l3fb, 0, sizeof(net->l3fbe));

    // zero buffers
    memset(net->p1, 0, sizeof(net->p1));
    memset(net->p2, 0, sizeof(net->p2));

    memset(net->o1, 0, sizeof(net->o1));
    memset(net->o2, 0, sizeof(net->o2));
    memset(net->o3, 0, sizeof(net->o3));

    memset(net->e1, 0, sizeof(net->e1));
    memset(net->e2, 0, sizeof(net->e2));
    memset(net->e3, 0, sizeof(net->e3));
}

int TBVGG3_SaveNetwork(TBVGG3_Network* net, const char* file)
{
    if(net == NULL){return -1;}

    FILE* f = fopen(file, "wb");
    if(f == NULL)
        return -1;

    if(fwrite(net, 1, sizeof(TBVGG3_Network), f) != sizeof(TBVGG3_Network))
    {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

int TBVGG3_LoadNetwork(TBVGG3_Network* net, const char* file)
{
    if(net == NULL){return -1;}

    FILE* f = fopen(file, "rb");
    if(f == NULL)
        return -1;

    if(fread(net, 1, sizeof(TBVGG3_Network), f) != sizeof(TBVGG3_Network))
    {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

void TBVGG3_2x2MaxPool(const uint depth, const uint wh, const float input[depth][wh][wh], float output[depth][wh/2][wh/2])
{
    // for every depth
    for(uint d = 0; d < depth; d++)
    {
        // output tracking, more memory for less alu division ops
        uint oi = 0, oj = 0;

        // for every 2x2 chunk of input
        for(uint i = 0; i < wh; i += 2, oi++)
        {
            for(uint j = 0; j < wh; j += 2, oj++)
            {
                // // get 2x2 chunk from input
                // const float f[] = {input[d][i][j], input[d][i+1][j], input[d][i][j+1], input[d][i+1][j+1]};

                // // iterate 2x2 chunk for max val
                // float max = 0;
                // for(uint k = 0; k < 4; k++)
                //     if(f[k] > max)
                //         max = f[k];

                // printf("O0: %u %u\n", depth, wh);
                
                // get max val
                float max = 0;
                if(input[d][i][j] > max)
                    max = input[d][i][j];
                if(input[d][i][j+1] > max)
                    max = input[d][i][j+1];
                if(input[d][i+1][j] > max)
                    max = input[d][i+1][j];
                if(input[d][i+1][j+1] > max)
                    max = input[d][i+1][j+1];

                // printf("O1: %ux%u : %.2f %.2f %.2f %.2f\n", i, j, input[d][i][j], input[d][i][j+1], input[d][i+1][j], input[d][i+1][j+1]);

                // output max val
                output[d][oi][oj] = max;

                // printf("O2: %ux%u : %.2f %.2f\n", oi, oj, output[d][oi][oj], max);
                // char in[256];
                // fgets(in, 256, stdin);
            }
            oj = 0;
        }
    }
}

static inline uint TBVGG3_CheckPadded(const sint x, const sint y, const uint wh)
{
    if(x < 0 || y < 0 || x >= wh || y >= wh)
        return 1;
    return 0;
}

float TBVGG3_3x3Conv(const uint depth, const uint wh, const float input[depth][wh][wh], const uint y, const uint x, const float filter[depth][9], const float* filter_bias)
{
    // input depth needs to be same as filter depth
    // This will return a single float output. Call this x*y times per filter.
    // It's zero padded so if TBVGG3_CheckPadded() returns 1 it's a no operation
    float ro = 0;
    sint nx = 0, ny = 0;
    for(uint i = 0; i < depth; i++)
    {
        // lower row
        nx = x-1, ny = y-1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            ro += input[i][ny][nx] * filter[i][0];

        nx = x,   ny = y-1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            ro += input[i][ny][nx] * filter[i][1];

        nx = x+1, ny = y-1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            ro += input[i][ny][nx] * filter[i][2];

        // middle row
        nx = x-1, ny = y;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            ro += input[i][ny][nx] * filter[i][3];

        nx = x,   ny = y;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            ro += input[i][ny][nx] * filter[i][4];

        nx = x+1, ny = y;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            ro += input[i][ny][nx] * filter[i][5];

        // top row
        nx = x-1, ny = y+1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            ro += input[i][ny][nx] * filter[i][6];

        nx = x,   ny = y+1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            ro += input[i][ny][nx] * filter[i][7];

        nx = x+1, ny = y+1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            ro += input[i][ny][nx] * filter[i][8];
    }

    // bias
    ro += filter_bias[0];

    // return output
    return TBVGG3_RELU(ro);
}

void TBVGG3_3x3ConvB(const uint depth, const uint wh, const float input[depth][wh][wh], const float error[depth][wh][wh], const uint y, const uint x, float filter[depth][9], float filter_momentum[depth][9])
{
    // backprop version
    sint nx = 0, ny = 0;
    for(uint i = 0; i < depth; i++)
    {
        // lower row
        nx = x-1, ny = y-1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            filter[i][0] += TBVGG3_NAG(input[i][ny][nx], error[i][y][x], &filter_momentum[i][0]);
            
        nx = x,   ny = y-1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            filter[i][1] += TBVGG3_NAG(input[i][ny][nx], error[i][y][x], &filter_momentum[i][1]);

        nx = x+1, ny = y-1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            filter[i][2] += TBVGG3_NAG(input[i][ny][nx], error[i][y][x], &filter_momentum[i][2]);

        // middle row
        nx = x-1, ny = y;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            filter[i][3] += TBVGG3_NAG(input[i][ny][nx], error[i][y][x], &filter_momentum[i][3]);

        nx = x,   ny = y;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            filter[i][4] += TBVGG3_NAG(input[i][ny][nx], error[i][y][x], &filter_momentum[i][4]);

        nx = x+1, ny = y;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            filter[i][5] += TBVGG3_NAG(input[i][ny][nx], error[i][y][x], &filter_momentum[i][5]);

        // top row
        nx = x-1, ny = y+1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            filter[i][6] += TBVGG3_NAG(input[i][ny][nx], error[i][y][x], &filter_momentum[i][6]);

        nx = x,   ny = y+1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            filter[i][7] += TBVGG3_NAG(input[i][ny][nx], error[i][y][x], &filter_momentum[i][7]);

        nx = x+1, ny = y+1;
        if(TBVGG3_CheckPadded(nx, ny, wh) == 0)
            filter[i][8] += TBVGG3_NAG(input[i][ny][nx], error[i][y][x], &filter_momentum[i][8]);
    }
}

float TBVGG3_Process(TBVGG3_Network* net, const float input[3][28][28], const TBVGG3_LEARNTYPE learn)
{
    if(net == NULL){return -1;}

    // convolve input with 32 filters
    for(uint i = 0; i < 32; i++) // num filter
    {
        for(uint j = 0; j < 28; j++) // height
        {
            for(uint k = 0; k < 28; k++) // width
            {
                net->o1[i][j][k] = TBVGG3_3x3Conv(3, 28, input, j, k, net->l1f[i], net->l1fb[i]);
            }
        }
    }

    // max pool the output
    TBVGG3_2x2MaxPool(32, 28, net->o1, net->p1);

    // convolve output with 64 filters
    for(uint i = 0; i < 64; i++) // num filter
    {
        for(uint j = 0; j < 14; j++) // height
        {
            for(uint k = 0; k < 14; k++) // width
            {
                net->o2[i][j][k] = TBVGG3_3x3Conv(32, 14, net->p1, j, k, net->l2f[i], net->l2fb[i]);
            }
        }
    }

    // max pool the output
    TBVGG3_2x2MaxPool(64, 14, net->o2, net->p2);

    // convolve output with 128 filters
    for(uint i = 0; i < 128; i++) // num filter
    {
        for(uint j = 0; j < 7; j++) // height
        {
            for(uint k = 0; k < 7; k++) // width
            {
                net->o3[i][j][k] = TBVGG3_3x3Conv(64, 7, net->p2, j, k, net->l3f[i], net->l3fb[i]);
            }
        }
    }

    // global average pooling
    float gap[128] = {0};
    for(uint i = 0; i < 128; i++)
    {
        for(uint j = 0; j < 7; j++)
            for(uint k = 0; k < 7; k++)
                gap[i] += net->o3[i][j][k];
        gap[i] /= 49;
    }

    // average final activation
    float output = 0;
    for(uint i = 0; i < 128; i++)
        output += gap[i];
    output /= 128;

    output = TBVGG3_SIGMOID(output);

    // return activation else backprop
    if(learn == NO_LEARN)
    {
        return output;
    }
    else
    {
        // error/gradient slope scaled by derivative
        const float g0 = TBVGG3_SIGMOID_D(output) * (learn - output);

        // ********** Gradient Back Pass **********

        // layer 3
        float l3er = 0;
        for(uint i = 0; i < 128; i++) // num filter
        {
            net->l3fbe[i][0] = GAIN * g0; // bias error

            for(uint j = 0; j < 7; j++) // height
            {
                for(uint k = 0; k < 7; k++) // width
                {
                    // set error
                    net->e3[i][j][k] = GAIN * TBVGG3_RELU_D(net->o3[i][j][k]) * g0;

                    // every output error gradient for every filter weight :: per filter
                    for(uint d = 0; d < 64; d++) // depth
                        for(uint w = 0; w < 9; w++) // weight
                            l3er += net->l3f[i][d][w] * net->e3[i][j][k];
                    l3er += GAIN * net->l3fb[i][0] * g0;
                }
            }
        }

        // layer 2
        float l2er = 0;
        for(uint i = 0; i < 64; i++) // num filter
        {
            net->l2fbe[i][0] = GAIN * l3er; // bias error

            for(uint j = 0; j < 14; j++) // height
            {
                for(uint k = 0; k < 14; k++) // width
                {
                    // set error
                    net->e2[i][j][k] = GAIN * TBVGG3_RELU_D(net->o2[i][j][k]) * l3er;

                    // every output error gradient for every filter weight :: per filter
                    for(uint d = 0; d < 32; d++) // depth
                        for(uint w = 0; w < 9; w++) // weight
                            l2er += net->l2f[i][d][w] * net->e2[i][j][k];
                    l2er += GAIN * net->l2fb[i][0] * l3er;
                }
            }
        }

        // layer 1
        for(uint i = 0; i < 32; i++) // num filter
        {
            net->l1fbe[i][0] = GAIN * l2er; // bias error

            for(uint j = 0; j < 28; j++) // height
                for(uint k = 0; k < 28; k++) // width
                    net->e1[i][j][k] = GAIN * TBVGG3_RELU_D(net->o1[i][j][k]) * l2er; // set error
        }

        // ********** Weight Nudge Forward Pass **********
        
        // convolve filter 1 with layer 1 error gradients
        for(uint i = 0; i < 32; i++) // num filter
        {
            net->l1fb[i][0] += TBVGG3_NAG(1, net->l1fbe[i][0], &net->l1fbm[i][0]);

            for(uint j = 0; j < 28; j++) // height
                for(uint k = 0; k < 28; k++) // width
                    TBVGG3_3x3ConvB(3, 28, input, net->e1, j, k, net->l1f[i], net->l1fm[i]);
        }

        // convolve filter 2 with layer 2 error gradients
        for(uint i = 0; i < 64; i++) // num filter
        {
            net->l2fb[i][0] += TBVGG3_NAG(1, net->l2fbe[i][0], &net->l2fbm[i][0]);

            for(uint j = 0; j < 14; j++) // height
                for(uint k = 0; k < 14; k++) // width
                    TBVGG3_3x3ConvB(32, 14, net->o1, net->e2, j, k, net->l2f[i], net->l2fm[i]);
        }

        // convolve filter 3 with layer 3 error gradients
        for(uint i = 0; i < 128; i++) // num filter
        {
            net->l3fb[i][0] += TBVGG3_NAG(1, net->l3fbe[i][0], &net->l3fbm[i][0]);

            for(uint j = 0; j < 7; j++) // height
                for(uint k = 0; k < 7; k++) // width
                    TBVGG3_3x3ConvB(64, 7, net->o2, net->e3, j, k, net->l3f[i], net->l3fm[i]);
        }
        
        // weights nudged
    }

    // return activation
    return output;
}

#endif
